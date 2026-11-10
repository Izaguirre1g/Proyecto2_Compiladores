/* Ejecuta con M2, compara datos públicos y delega las diferencias a M1. */
#define _POSIX_C_SOURCE 200809L
#include "sim_comparador.h"
#include <inttypes.h>
#include <locale.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

struct SimComparador {
    SimComparadorResumen r;
    char *rutas[2];
    SimMetricaTrap *traps[2];
    size_t cantidades[2];
};

/* Libera rutas e historial de ambos lados y el resultado recibido. Admite
 * salida NULL o vacía y deja el puntero del llamador en NULL. */
void sim_comparador_liberar(SimComparador **salida)
{
    if (!salida || !*salida) return;
    SimComparador *c=*salida;
    for (unsigned i=0;i<2;++i) { free(c->rutas[i]); free(c->traps[i]); }
    free(c); *salida=NULL;
}
/* Libera la cadena propia exportada por B1 y deja *s en NULL. No requiere
 * que el comparador siga vivo; un doble puntero NULL no produce efectos. */
void sim_comparador_liberar_reporte(char **s)
{
    if (s) { free(*s); *s=NULL; }
}
static bool inicial(const SimMetricasResumen *r)
{
    if (!r->valido || r->estado!=SIM_PIPE_ACTIVO || r->ciclos || r->pc ||
        r->admitidos || r->completados || r->anulados || r->pendientes ||
        r->traps || r->trap_count32 || r->trap_pc || r->estado_crypto!=SIM_CRYPTO_INIT)
        return false;
    for (unsigned i=0;i<32;++i) if (r->registros[i]) return false;
    return true;
}

/* M2 puede detenerse después de aceptar un flanco pero antes de observarlo.
 * Reintentar solo esa observación conserva sus efectos. Tras resolverla se
 * consulta el reloj para consumir únicamente el presupuesto todavía restante. */
static SimEjecucionCodigo ejecutar(SimEjecucion *s,uint64_t presupuesto,unsigned maximo)
{
    unsigned intentos=0; uint64_t restantes=presupuesto;
    for (;;) {
        SimEjecucionCodigo e=sim_ejecucion_ejecutar(s,restantes);
        while (e==SIM_EJECUCION_PENDIENTE && intentos<maximo) {
            ++intentos; e=sim_ejecucion_reintentar(s);
        }
        if (e!=SIM_EJECUCION_OK) return e;
        SimMetricasResumen r;
        if (sim_ejecucion_resultados(s,&r)!=SIM_METRICAS_OK) return SIM_EJECUCION_METRICAS;
        if (r.estado==SIM_PIPE_TERMINADO) return SIM_EJECUCION_OK;
        if (r.ciclos>=presupuesto) return sim_ejecucion_ejecutar(s,0);
        restantes=presupuesto-r.ciclos;
    }
}

/* Comparar cada byte detecta también cambios fuera de la zona esperada; FNV
 * no sustituye esta prueba. El primer índice es siempre dirección de byte. */
static size_t memoria(const uint8_t *a,const uint8_t *b,size_t *primero)
{
    size_t distintos=0; *primero=0;
    for (size_t i=0;i<SIM_CARGADOR_DMEM_BYTES;++i) if (a[i]!=b[i]) {
        if (!distintos) *primero=i;
        ++distintos;
    }
    return distintos;
}

/* Crea dos sesiones propias y verifica el reset público y toda DMEM inicial
 * antes de ejecutar. El presupuesto se aplica por separado a cada programa.
 * Ambas deben terminar para comparar 32 GPR, toda DMEM, AUTH/INIT y traps.
 * PC/ciclos/layout no son criterios funcionales. Las diferencias de métricas
 * se delegan a M1, incluso si la ejecución limitada resulta INCONCLUSA.
 * salida comienza en NULL. Solo se publica un resultado íntegro, dueño de
 * rutas, snapshots e historial. CARGA/ejecución producen un resultado ERROR;
 * argumentos/reservas de B1 fallidos devuelven código sin publicar salida.
 * diagnosticos es prestado y admite NULL. No se modifica ningún componente. */
SimComparadorCodigo sim_comparador_ejecutar(const SimComparadorOpciones *o,
    SimComparador **salida,FILE *diag)
{
    if (!o || !salida || *salida || !o->referencia.ruta || !*o->referencia.ruta ||
        !o->candidato.ruta || !*o->candidato.ruta ||
        (unsigned)o->referencia.formato>SIM_EJECUCION_BALB ||
        (unsigned)o->candidato.formato>SIM_EJECUCION_BALB ||
        (unsigned)o->modo>SIM_BUSQUEDA_FISICA || o->reintentos>16)
        return SIM_COMPARADOR_ARGUMENTO;
    SimComparador *c=calloc(1,sizeof *c);
    if (!c) return SIM_COMPARADOR_MEMORIA;
    c->r.resultado=SIM_COMPARADOR_ERROR; c->r.lado_error=-1;
    SimEjecucion *s[2]={NULL,NULL}; uint8_t *datos=NULL;
    SimComparadorCodigo codigo=SIM_COMPARADOR_OK;
    SimComparadorPrograma programas[2]={o->referencia,o->candidato};
    /* Los nombres se poseen: el resultado no depende del argv o de buffers
     * del cliente. Los dos buffers DMEM son temporales y se reutilizan. */
    for (unsigned i=0;i<2;++i) {
        size_t n=strlen(programas[i].ruta);
        if (n==SIZE_MAX || !(c->rutas[i]=malloc(n+1))) goto sin_memoria;
        memcpy(c->rutas[i],programas[i].ruta,n+1);
    }
    datos=malloc(2u*SIM_CARGADOR_DMEM_BYTES);
    if (!datos) goto sin_memoria;
    for (unsigned i=0;i<2;++i) {
        SimEjecucionCodigo e=sim_ejecucion_crear(programas[i].ruta,programas[i].formato,o->modo,&s[i],diag);
        if (e!=SIM_EJECUCION_OK) {
            c->r.motivo=SIM_COMPARADOR_CARGA; c->r.lado_error=(int)i;
            c->r.error_ejecucion=e; goto terminar;
        }
        c->r.creadas[i]=true;
    }
    for (unsigned i=0;i<2;++i) {
        c->r.error_metricas=sim_ejecucion_resultados(s[i],&c->r.resumenes[i]);
        if (c->r.error_metricas!=SIM_METRICAS_OK ||
            sim_ejecucion_memoria(s[i],datos+i*SIM_CARGADOR_DMEM_BYTES,SIM_CARGADOR_DMEM_BYTES)!=0) {
            c->r.motivo=SIM_COMPARADOR_CONSULTA; c->r.lado_error=(int)i; goto terminar;
        }
        c->r.resumen_disponible[i]=true;
    }
    c->r.bytes_iniciales_distintos=memoria(datos,datos+SIM_CARGADOR_DMEM_BYTES,&c->r.primer_byte_inicial);
    c->r.iniciales_compatibles=!c->r.bytes_iniciales_distintos &&
        inicial(&c->r.resumenes[0]) && inicial(&c->r.resumenes[1]);
    if (!c->r.iniciales_compatibles) {
        /* No se igualan datos silenciosamente ni se ejecutan entradas distintas.
         * Esto es falta de comparabilidad, no una diferencia funcional final. */
        c->r.resultado=SIM_COMPARADOR_INCONCLUSO; c->r.motivo=SIM_COMPARADOR_INICIALES;
        goto terminar;
    }
    for (unsigned i=0;i<2;++i) {
        c->r.ejecutadas[i]=true; c->r.resumen_disponible[i]=false;
        SimEjecucionCodigo e=ejecutar(s[i],o->presupuesto,o->reintentos);
        c->r.error_metricas=sim_ejecucion_resultados(s[i],&c->r.resumenes[i]);
        c->r.resumen_disponible[i]=c->r.error_metricas==SIM_METRICAS_OK;
        if (e!=SIM_EJECUCION_OK || !c->r.resumen_disponible[i]) {
            c->r.lado_error=(int)i; c->r.error_ejecucion=e;
            c->r.motivo=e==SIM_EJECUCION_PENDIENTE?SIM_COMPARADOR_OBSERVACION:SIM_COMPARADOR_EJECUCION;
            goto terminar;
        }
    }
    c->r.error_metricas=sim_metricas_comparar(&c->r.resumenes[0],&c->r.resumenes[1],&c->r.metricas);
    if (c->r.error_metricas!=SIM_METRICAS_OK) { c->r.motivo=SIM_COMPARADOR_CONSULTA; goto terminar; }
    c->r.metricas_disponibles=true;
    /* Se conservan los eventos como diagnósticos propios antes de destruir
     * las sesiones. Entre layouts solo se comparan cantidad y vector en orden. */
    for (unsigned i=0;i<2;++i) {
        uint64_t n=c->r.resumenes[i].traps;
        if (n>SIZE_MAX/sizeof(SimMetricaTrap)) goto sin_memoria;

        if (n) {
            c->traps[i]=malloc((size_t)n*sizeof *c->traps[i]);
            if (!c->traps[i]) goto sin_memoria;
            for (size_t j=0;j<(size_t)n;++j) {
                c->r.error_metricas=sim_ejecucion_trap(s[i],j,&c->traps[i][j]);
                if (c->r.error_metricas!=SIM_METRICAS_OK) { c->r.motivo=SIM_COMPARADOR_CONSULTA; goto terminar; }
                ++c->cantidades[i];
            }
        }
    }
    if (c->r.resumenes[0].estado!=SIM_PIPE_TERMINADO || c->r.resumenes[1].estado!=SIM_PIPE_TERMINADO) {
        c->r.resultado=SIM_COMPARADOR_INCONCLUSO; c->r.motivo=SIM_COMPARADOR_NO_TERMINADO;
        goto terminar;
    }
    c->r.comparacion_realizada=true;
    for (unsigned i=0;i<32;++i) if (c->r.resumenes[0].registros[i]!=c->r.resumenes[1].registros[i]) {
        if (!c->r.registros_distintos) {
            c->r.primer_registro=i;
            for (unsigned j=0;j<2;++j) c->r.valores_registro[j]=c->r.resumenes[j].registros[i];
        }
        ++c->r.registros_distintos;
    }
    for (unsigned i=0;i<2;++i) if (sim_ejecucion_memoria(s[i],datos+i*SIM_CARGADOR_DMEM_BYTES,SIM_CARGADOR_DMEM_BYTES)!=0) {
        c->r.resultado=SIM_COMPARADOR_ERROR; c->r.motivo=SIM_COMPARADOR_CONSULTA; goto terminar;
    }
    c->r.bytes_distintos=memoria(datos,datos+SIM_CARGADOR_DMEM_BYTES,&c->r.primer_byte);
    if (c->r.bytes_distintos) for (unsigned i=0;i<2;++i)
        c->r.valores_byte[i]=datos[i*SIM_CARGADOR_DMEM_BYTES+c->r.primer_byte];
    c->r.crypto_distinto=c->r.resumenes[0].estado_crypto!=c->r.resumenes[1].estado_crypto;
    c->r.cantidad_traps_distinta=c->cantidades[0]!=c->cantidades[1];
    size_t n=c->cantidades[0]<c->cantidades[1]?c->cantidades[0]:c->cantidades[1];
    for (size_t i=0;i<n;++i) if (c->traps[0][i].vector!=c->traps[1][i].vector) {
        c->r.vector_distinto=true; c->r.primer_vector=i;
        for (unsigned j=0;j<2;++j) c->r.valores_vector[j]=c->traps[j][i].vector;
        break;
    }
    /* No se compara PC final ni trap_pc: identifican posiciones del layout.
     * Tampoco se ignoran temporales: otro renombramiento necesita otro contrato. */
    c->r.resultado=c->r.registros_distintos || c->r.bytes_distintos || c->r.crypto_distinto ||
        c->r.cantidad_traps_distinta || c->r.vector_distinto ? SIM_COMPARADOR_DIFERENTE:SIM_COMPARADOR_EQUIVALENTE;
terminar:
    for (unsigned i=0;i<2;++i) {
        if (s[i]) (void)sim_ejecucion_estado(s[i],&c->r.estados[i]);
        sim_ejecucion_liberar(&s[i]);
    }
    free(datos);
    if (codigo==SIM_COMPARADOR_OK) *salida=c;
    else sim_comparador_liberar(&c);
    return codigo;
sin_memoria:
    codigo=SIM_COMPARADOR_MEMORIA; goto terminar;
}

/* Copias propias; no entregan acceso a sesiones ya destruidas ni a la bóveda.
 * Solo se ofrece contrato estricto: otro renombramiento requiere acuerdos
 * futuros, no se ignoran temporales ni registros de enlace arbitrariamente. */
SimComparadorCodigo sim_comparador_resumen(const SimComparador *c,SimComparadorResumen *r)
{
    if (!c || !r) return SIM_COMPARADOR_ARGUMENTO;
    *r=c->r; return SIM_COMPARADOR_OK;
}
/* Copia el trap indicado del lado 0 o 1, sin entregar almacenamiento interno.
 * Punteros nulos, lado o índice inválidos devuelven ARGUMENTO sin escribir salida. */
SimComparadorCodigo sim_comparador_trap(const SimComparador *c,unsigned lado,size_t i,SimMetricaTrap *t)
{
    if (!c || !t || lado>1 || i>=c->cantidades[lado] || !c->traps[lado]) return SIM_COMPARADOR_ARGUMENTO;
    *t=c->traps[lado][i]; return SIM_COMPARADOR_OK;
}

/* El reporte tiene almacenamiento privado hasta completar su generación.
 * La lista blanca evita serializar sesiones, memoria o bóveda completas. */
typedef struct { char *s; size_t n,cap; SimComparadorCodigo error; } Texto;
static void agregar(Texto *b,const char *f,...)
{
    if (b->error) return;
    va_list a,c; va_start(a,f); va_copy(c,a); int n=vsnprintf(NULL,0,f,a); va_end(a);
    if (n<0) b->error=SIM_COMPARADOR_GENERACION;
    else if ((size_t)n>SIZE_MAX-b->n-1) b->error=SIM_COMPARADOR_MEMORIA;
    else {
        size_t requerido=b->n+(size_t)n+1;
        if (requerido>b->cap) {
            size_t cap=b->cap?b->cap:512;
            while (cap<requerido) cap=cap>SIZE_MAX/2?requerido:cap*2;
            char *s=realloc(b->s,cap);
            if (!s) b->error=SIM_COMPARADOR_MEMORIA;
            else { b->s=s; b->cap=cap; }
        }
        if (!b->error) {
            if (vsnprintf(b->s+b->n,(size_t)n+1,f,c)!=n) b->error=SIM_COMPARADOR_GENERACION;
            else b->n+=(size_t)n;
        }
    }
    va_end(c);
}
static void cadena(Texto *b,const char *s)
{
    agregar(b,"\"");
    for (const unsigned char *p=(const unsigned char *)s;*p;++p) {
        if (*p=='"' || *p=='\\') agregar(b,"\\%c",*p);
        else if (*p<32) agregar(b,"\\u%04x",*p);
        else if (*p>=128) {
            /* Conservar nombres UTF-8 válidos; un byte inválido se escapa
             * individualmente para no producir un documento JSON mal formado. */
            unsigned n=*p>=0xc2 && *p<=0xdf?2:*p>=0xe0 && *p<=0xef?3:*p>=0xf0 && *p<=0xf4?4:0;
            bool valido=n!=0;
            for (unsigned j=1;j<n;++j) if (p[j]<0x80 || p[j]>0xbf) { valido=false; break; }
            if (valido && n>=3) valido=!((*p==0xe0 && p[1]<0xa0) || (*p==0xed && p[1]>=0xa0) ||
                (*p==0xf0 && p[1]<0x90) || (*p==0xf4 && p[1]>0x8f));
            if (valido) { for (unsigned j=0;j<n;++j) agregar(b,"%c",p[j]); p+=n-1; }
            else agregar(b,"\\u%04x",*p);
        } else agregar(b,"%c",*p);
    }
    agregar(b,"\"");
}
static const char *resultado(SimComparadorResultado r)
{
    static const char *const nombres[]={"EQUIVALENTE","DIFERENTE","INCONCLUSO","ERROR"};
    return nombres[r];
}
static const char *estado(SimPipelineEstado e)
{
    static const char *const nombres[]={"ACTIVO","TERMINADO","LIMITE","ARGUMENTO","PC",
        "DECODIFICACION","NO_SOPORTADA","ALU","LSU","BRU","CRYPTO"};
    return nombres[e];
}
static const char *motivo(SimComparadorMotivo m)
{
    static const char *const nombres[]={"sin impedimento","condiciones iniciales incompatibles",
        "ejecución sin terminar","carga fallida","ejecución fallida","observación pendiente sin recuperar","consulta fallida"};
    return nombres[m];
}
static void delta(Texto *b,SimMetricasDiferencia d,bool json)
{
    if (json) agregar(b,"{\"negativa\":%s,\"magnitud\":%" PRIu64 "}",d.negativa?"true":"false",d.magnitud);
    else agregar(b,"%s%" PRIu64,d.negativa?"-":"",d.magnitud);
}
static void programa(Texto *b,const SimComparador *c,unsigned i)
{
    const SimMetricasResumen *r=&c->r.resumenes[i];
    agregar(b,"{\"ruta\":"); cadena(b,c->rutas[i]);
    agregar(b,",\"creada\":%s,\"ejecutada\":%s,\"estado\":",c->r.creadas[i]?"true":"false",c->r.ejecutadas[i]?"true":"false");
    if (c->r.creadas[i]) agregar(b,"%u",(unsigned)c->r.estados[i].pipeline); else agregar(b,"null");
    agregar(b,",\"metricas\":");
    if (!c->r.resumen_disponible[i]) agregar(b,"null");
    else agregar(b,"{\"ciclos\":%" PRIu64 ",\"bundles\":%" PRIu64 ",\"instrucciones\":%" PRIu64
        ",\"ipc\":%.17g,\"pc_diagnostico\":%" PRIu32 ",\"estado_crypto\":%" PRIu32 ",\"traps\":%" PRIu64 "}",
        r->ciclos,r->completados,r->instrucciones_utiles,r->ipc,r->pc,r->estado_crypto,r->traps);
    agregar(b,",\"excepciones\":[");
    for (size_t j=0;j<c->cantidades[i];++j) {
        const SimMetricaTrap *t=&c->traps[i][j];
        agregar(b,"%s{\"ciclo\":%" PRIu64 ",\"pc\":%" PRIu32 ",\"vector\":%" PRIu32 "}",j?",":"",t->ciclo,t->pc,t->vector);
    }
    agregar(b,"]}");
}
/* Reporte B1 nuevo TEXTO/JSON v1; no cambia ni reproduce el JSON completo M1.
 * Salida inicialmente NULL, publicación transaccional. Liberar la cadena con
 * esta API, independientemente de la vida del resultado. */
SimComparadorCodigo sim_comparador_exportar(const SimComparador *c,SimMetricasFormato f,char **salida)
{
    if (!c || !salida || *salida || (unsigned)f>SIM_METRICAS_JSON) return SIM_COMPARADOR_ARGUMENTO;
    /* El locale por hilo evita cambiar el locale global del cliente. Se usa C
     * también para texto: IPC y porcentajes siempre tienen punto decimal. */
    locale_t loc=newlocale(LC_NUMERIC_MASK,"C",(locale_t)0);
    if (!loc) return SIM_COMPARADOR_GENERACION;
    locale_t previo=uselocale(loc);
    if (!previo) { freelocale(loc); return SIM_COMPARADOR_GENERACION; }
    const SimComparadorResumen *r=&c->r; Texto b={0}; bool json=f==SIM_METRICAS_JSON;
    if (json) {
        agregar(&b,"{\"version\":1,\"contrato\":\"estricto-publico\",\"resultado_comparacion\":");cadena(&b,resultado(r->resultado));
        agregar(&b,",\"motivo\":%u,\"referencia\":",(unsigned)r->motivo);programa(&b,c,0);
        agregar(&b,",\"candidato\":");programa(&b,c,1);
        agregar(&b,",\"iniciales_compatibles\":%s,\"bytes_iniciales_distintos\":%zu,\"primer_byte_inicial\":%zu,",
            r->iniciales_compatibles?"true":"false",r->bytes_iniciales_distintos,r->primer_byte_inicial);
        agregar(&b,"\"diferencias_arquitectonicas\":{\"comparadas\":%s,\"registros\":%u,\"primer_registro\":%u,\"valores_registro\":[%" PRIu32 ",%" PRIu32
            "],\"bytes\":%zu,\"primer_byte\":%zu,\"valores_byte\":[%u,%u],\"crypto\":%s,\"cantidad_traps\":%s,\"vector\":%s,\"primer_vector\":%zu,\"valores_vector\":[%" PRIu32 ",%" PRIu32 "]},",
            r->comparacion_realizada?"true":"false",r->registros_distintos,r->primer_registro,r->valores_registro[0],r->valores_registro[1],
            r->bytes_distintos,r->primer_byte,(unsigned)r->valores_byte[0],(unsigned)r->valores_byte[1],r->crypto_distinto?"true":"false",
            r->cantidad_traps_distinta?"true":"false",r->vector_distinto?"true":"false",r->primer_vector,r->valores_vector[0],r->valores_vector[1]);
        agregar(&b,"\"error\":{\"lado\":%d,\"ejecucion\":%u,\"metricas\":%u},\"diferencias_metricas\":",r->lado_error,(unsigned)r->error_ejecucion,(unsigned)r->error_metricas);
        if (!r->metricas_disponibles) agregar(&b,"null");
        else {
            agregar(&b,"{\"ciclos\":");delta(&b,r->metricas.ciclos,true);
            agregar(&b,",\"bundles\":");delta(&b,r->metricas.bundles,true);
            agregar(&b,",\"instrucciones\":");delta(&b,r->metricas.instrucciones,true);
            agregar(&b,",\"ipc\":%.17g,\"porcentaje_definido\":%s,\"porcentaje_ciclos\":",r->metricas.diferencia_ipc,r->metricas.porcentaje_definido?"true":"false");
            if (r->metricas.porcentaje_definido) agregar(&b,"%.17g",r->metricas.cambio_porcentual_ciclos); else agregar(&b,"null");
            agregar(&b,"}");
        }
        agregar(&b,",\"limites\":[\"boveda privada no observable\",\"32 GPR sin correspondencia de renombramiento\",\"PC y ciclos de traps solo diagnosticos\"]}\n");
    } else {
        agregar(&b,"Comparación VLIW estricta: %s\nReferencia: %s\nCandidato: %s\n",resultado(r->resultado),c->rutas[0],c->rutas[1]);
        agregar(&b,"Estado referencia/candidato: %s / %s; motivo: %s\n",r->creadas[0]?estado(r->estados[0].pipeline):"SIN SESION",r->creadas[1]?estado(r->estados[1].pipeline):"SIN SESION",motivo(r->motivo));
        if (r->motivo==SIM_COMPARADOR_INICIALES) agregar(&b,"Condiciones iniciales no comparables: %zu bytes distintos; primer byte 0x%08zx.\n",r->bytes_iniciales_distintos,r->primer_byte_inicial);
        if (r->resultado==SIM_COMPARADOR_ERROR) agregar(&b,"Error de ejecución/carga/consulta: lado %d, M2 %u, M1 %u.\n",r->lado_error,(unsigned)r->error_ejecucion,(unsigned)r->error_metricas);
        if (r->motivo==SIM_COMPARADOR_NO_TERMINADO) agregar(&b,"Comparación final inconclusa: falta terminar al menos un programa.\n");
        if (r->registros_distintos) agregar(&b,"Registro x%u: referencia %" PRIu32 ", candidato %" PRIu32 "; %u registros distintos.\n",r->primer_registro,r->valores_registro[0],r->valores_registro[1],r->registros_distintos);
        if (r->bytes_distintos) agregar(&b,"Memoria byte 0x%08zx: referencia 0x%02x, candidato 0x%02x; %zu bytes distintos.\n",r->primer_byte,(unsigned)r->valores_byte[0],(unsigned)r->valores_byte[1],r->bytes_distintos);
        if (r->crypto_distinto) agregar(&b,"Estado criptográfico: referencia %" PRIu32 ", candidato %" PRIu32 ".\n",r->resumenes[0].estado_crypto,r->resumenes[1].estado_crypto);
        if (r->cantidad_traps_distinta) agregar(&b,"Excepciones: referencia %" PRIu64 ", candidato %" PRIu64 ".\n",r->resumenes[0].traps,r->resumenes[1].traps);
        if (r->vector_distinto) agregar(&b,"Vector de excepción %zu: referencia 0x%08" PRIx32 ", candidato 0x%08" PRIx32 ".\n",r->primer_vector,r->valores_vector[0],r->valores_vector[1]);
        if (r->metricas_disponibles) {
            agregar(&b,"Métrica | Referencia | Candidato | Candidato - referencia\nCiclos | %" PRIu64 " | %" PRIu64 " | ",r->resumenes[0].ciclos,r->resumenes[1].ciclos);delta(&b,r->metricas.ciclos,false);
            agregar(&b,"\nBundles | %" PRIu64 " | %" PRIu64 " | ",r->resumenes[0].completados,r->resumenes[1].completados);delta(&b,r->metricas.bundles,false);
            agregar(&b,"\nInstrucciones | %" PRIu64 " | %" PRIu64 " | ",r->resumenes[0].instrucciones_utiles,r->resumenes[1].instrucciones_utiles);delta(&b,r->metricas.instrucciones,false);
            agregar(&b,"\nIPC | %.12f | %.12f | %.12f\n",r->resumenes[0].ipc,r->resumenes[1].ipc,r->metricas.diferencia_ipc);
            if (r->metricas.porcentaje_definido) agregar(&b,"Variación de ciclos respecto de referencia: %.12f%%\n",r->metricas.cambio_porcentual_ciclos);
            else agregar(&b,"Variación porcentual no definida: referencia con cero ciclos.\n");
        }
        agregar(&b,"Límites: bóveda privada no observable; se comparan 32 GPR sin correspondencia de renombramiento. PC final y PC/ciclo de traps solo son diagnósticos.\n");
    }
    (void)uselocale(previo);freelocale(loc);
    if (b.error) { free(b.s); return b.error; }
    *salida=b.s;return SIM_COMPARADOR_OK;
}
