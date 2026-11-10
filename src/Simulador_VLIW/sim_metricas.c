/* Observa el flanco ya publicado: usa señales EX/WB y decodificación S2.
 * Ningún camino modifica el simulador, sus memorias o su bóveda. */
#include "sim_metricas.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static const char *const unidades[] = {"ALU", "LSU", "BRU", "CRIPTO"};
static const char *const estados[] = {"ACTIVO", "TERMINADO", "LIMITE", "ARGUMENTO", "PC",
    "DECODIFICACION", "NO_SOPORTADA", "ALU", "LSU", "BRU", "CRYPTO"};
static bool sumar(uint64_t *a, uint64_t b)
{
    if (b > UINT64_MAX - *a) return false;
    *a += b; return true;
}
static bool simulador_valido(const SimPipeline *s)
{
    return s && s->inicializado && s->imagen && s->imagen->cantidad_bundles &&
        s->imagen->cantidad_bundles <= SIM_CARGADOR_IMEM_BUNDLES && !s->imagen->entrada &&
        (unsigned)s->estado <= SIM_PIPE_CRYPTO && (unsigned)s->modo_busqueda <= SIM_BUSQUEDA_FISICA;
}
static uint64_t ocupacion(const SimPipeline *s)
{
    return (unsigned)s->if_id.valido + (unsigned)s->id_ex.valido + (unsigned)s->ex_wb.valido;
}
static bool invariante(const SimPipeline *s)
{
    uint64_t total = s->bundles_completados;
    return sumar(&total,s->bundles_anulados) && sumar(&total,ocupacion(s)) && total == s->bundles_admitidos;
}
static void capturar(SimMetricasResumen *r, const SimPipeline *s)
{
    r->estado = s->estado; r->pc = s->pc; r->estado_crypto = s->estado_crypto;
    r->trap_count32 = s->trap_count; r->trap_pc = s->trap_pc;
    memcpy(r->registros,s->registros,sizeof r->registros);
    r->pendientes_por_etapa[0] = s->if_id.valido;
    r->pendientes_por_etapa[1] = s->id_ex.valido;
    r->pendientes_por_etapa[2] = s->ex_wb.valido;
    r->pendientes = ocupacion(s);
}
/* Duplicados toleran cambios administrativos de estado, pero no otro flanco
 * ni cambios externos en PC, GPR, etapas, contadores o AUTH/INIT observados. */
static bool coincide(const SimMetricasResumen *r, const SimPipeline *s)
{
    return r->admitidos == s->bundles_admitidos && r->completados == s->bundles_completados &&
        r->anulados == s->bundles_anulados && r->pc == s->pc && r->estado_crypto == s->estado_crypto &&
        r->trap_count32 == s->trap_count && r->trap_pc == s->trap_pc &&
        r->pendientes_por_etapa[0] == (unsigned)s->if_id.valido &&
        r->pendientes_por_etapa[1] == (unsigned)s->id_ex.valido &&
        r->pendientes_por_etapa[2] == (unsigned)s->ex_wb.valido &&
        !memcmp(r->registros,s->registros,sizeof r->registros);
}
static SimMetricasEstado enlace(const SimMetricas *m, const SimPipeline *s)
{
    if (!m || !m->iniciado || !simulador_valido(s)) return SIM_METRICAS_ARGUMENTO;
    if (m->simulador != s || m->imagen != s->imagen || m->modo != s->modo_busqueda) return SIM_METRICAS_EPOCA;
    if (s->ciclos < m->acumulado.ciclos) return SIM_METRICAS_EPOCA;
    if (!invariante(s)) return SIM_METRICAS_INCONSISTENCIA;
    return SIM_METRICAS_OK;
}
/* Solo inicia en ciclo cero, sin etapas ni contadores pendientes. Cada inicio
 * reemplaza la época anterior y libera su historial. Copiar resultados o
 * exportar antes del reset si deben conservarse. No toca el simulador. */
SimMetricasEstado sim_metricas_iniciar(SimMetricas *m, const SimPipeline *s)
{
    if (!m || !simulador_valido(s)) return SIM_METRICAS_ARGUMENTO;
    if (s->ciclos || s->bundles_admitidos || s->bundles_completados || s->bundles_anulados ||
        ocupacion(s) || s->trap_count || (s->estado != SIM_PIPE_ACTIVO && s->estado != SIM_PIPE_LIMITE))
        return SIM_METRICAS_EPOCA;
    if (m->acumulado.epoca == UINT64_MAX) return SIM_METRICAS_DESBORDAMIENTO;
    uint64_t epoca = m->acumulado.epoca + 1;
    free(m->historial);
    *m = (SimMetricas){.simulador=s,.imagen=s->imagen,.modo=s->modo_busqueda,.iniciado=true};
    m->acumulado.valido = true; m->acumulado.epoca = epoca;
    capturar(&m->acumulado,s); return SIM_METRICAS_OK;
}
/* Se consulta la imagen con la misma selección física del simulador. Una
 * dirección no cargada equivale a cuatro NOP, incluso tras aliasing de PC. */
static SimMetricasEstado retiradas(SimMetricasResumen *r, const SimPipeline *s)
{
    const SimCiclo *c = &s->ultimo_ciclo;
    if (!c->wb.valido) return SIM_METRICAS_OK;
    size_t b = s->modo_busqueda == SIM_BUSQUEDA_FISICA ?
        (c->wb.pc >> 4) & (SIM_CARGADOR_IMEM_BUNDLES-1u) : c->wb.pc / 16u;
    if (s->modo_busqueda == SIM_BUSQUEDA_FINITA && ((c->wb.pc & 15u) || b >= s->imagen->cantidad_bundles))
        return SIM_METRICAS_INCONSISTENCIA;
    unsigned utiles = 0;
    for (unsigned slot=0; slot<4; ++slot) {
        uint32_t palabra = b < s->imagen->cantidad_bundles ? s->imagen->imem[b][slot] : 0;
        SimInstruccion i; SimDecodeEstado e = sim_decode_palabra(palabra,slot,&i);
        if (e == SIM_DECODE_NOP) continue;
        if (e != SIM_DECODE_OK) return SIM_METRICAS_DECODIFICACION;
        if (!sumar(&r->instrucciones_por_slot[slot],1) || !sumar(&r->operaciones_isa[i.operacion],1))
            return SIM_METRICAS_DESBORDAMIENTO;
        ++utiles;
    }
    if (!sumar(&r->instrucciones_utiles,utiles) ||
        !sumar(utiles ? &r->bundles_utiles : &r->bundles_nop,1)) return SIM_METRICAS_DESBORDAMIENTO;
    return SIM_METRICAS_OK;
}
/* Observar después de CADA flanco aceptado, antes de avanzar otro. Un duplicado,
 * un límite o error sin flanco devuelve SIN_CICLO sin sumar eventos. Un salto
 * de ciclos devuelve SECUENCIA. Reset exige iniciar una época nueva; no puede
 * reconstruirse una época omitida desde el último SimCiclo. MEMORIA permite
 * reintentar el mismo flanco antes de continuar. IMEM es prestada e inmutable. */
SimMetricasEstado sim_metricas_observar(SimMetricas *m, const SimPipeline *s)
{
    SimMetricasEstado e = enlace(m,s);
    if (e != SIM_METRICAS_OK) return e;
    const SimMetricasResumen *previo = &m->acumulado;
    if (s->ciclos == previo->ciclos)
        return coincide(previo,s) ? SIM_METRICAS_SIN_CICLO : SIM_METRICAS_EPOCA;
    if (previo->ciclos == UINT64_MAX || s->ciclos - previo->ciclos != 1) return SIM_METRICAS_SECUENCIA;
    const SimCiclo *c = &s->ultimo_ciclo;
    bool salto = c->bru_ex_valido && c->ex_bru.tomado;
    if (c->ciclo != s->ciclos || c->flush != (c->excepcion || salto) ||
        (c->excepcion && (!c->ex.valido || c->bru_ex_valido || c->ex_lsu.valido || c->ex_crypto.memoria.valido || c->redirect_pc != SIM_TRAP_VECTOR)))
        return SIM_METRICAS_INCONSISTENCIA;
    SimMetricasResumen r = *previo;
    /* Contrastar cada delta con la traza impide confundir IF descartado con
     * admisión o el culpable EX con el WB anterior que sí pudo completarse. */
    if (!sumar(&r.admitidos,c->if_.valido && !c->if_descartado) ||
        !sumar(&r.completados,c->wb.valido) || !sumar(&r.anulados,(unsigned)c->id_descartado + (unsigned)c->excepcion))
        return SIM_METRICAS_DESBORDAMIENTO;
    if (r.admitidos != s->bundles_admitidos || r.completados != s->bundles_completados || r.anulados != s->bundles_anulados ||
        s->trap_count != previo->trap_count32 + (uint32_t)c->excepcion) return SIM_METRICAS_INCONSISTENCIA;
    e = retiradas(&r,s); if (e != SIM_METRICAS_OK) return e;
#define A(campo,valor) do { if (!sumar(&r.campo,(uint64_t)(valor))) return SIM_METRICAS_DESBORDAMIENTO; } while (0)
    A(bru_ex,c->bru_ex_valido); A(saltos_tomados,salto); A(saltos_no_tomados,c->bru_ex_valido && !salto);
    A(flush_salto,salto); A(busquedas_descartadas,c->if_descartado);
    A(anulados_salto,salto && c->id_descartado);
    A(anulados_excepcion,c->excepcion ? 1u+(unsigned)c->id_descartado : 0u);
    A(traps,c->excepcion);
    A(lsu_lecturas_ex,c->ex_lsu.valido && c->ex_lsu.carga);
    A(lsu_escrituras_ex,c->ex_lsu.valido && c->ex_lsu.escritura);
    A(puerto_b_operaciones_ex,c->ex_crypto.memoria.valido);
    A(puerto_b_lecturas_ex,c->ex_crypto.memoria.valido);
    A(puerto_b_escrituras_ex,c->ex_crypto.memoria.valido && c->ex_crypto.memoria.escritura);
    A(conflictos_memoria,c->conflicto_memoria); A(conflictos_registro,c->conflicto_wb);
    const bool we[] = {c->wb_escribe,c->lsu_wb_escribe,c->bru_wb_escribe,c->crypto_wb_escribe[0],c->crypto_wb_escribe[1]};
    for (unsigned p=0; p<5; ++p) A(escrituras_por_puerto[p],we[p]);
    A(descartes_x0_por_slot[0],c->wb_descarta_x0);
    A(descartes_x0_por_slot[1],c->lsu_wb_descarta_x0);
    A(descartes_x0_por_slot[2],c->bru_wb_descarta_x0);
    A(descartes_x0_por_slot[3],c->wb_crypto.par ? (unsigned)(c->wb_crypto.rd==0)+(unsigned)(c->wb_crypto.rd2==0) : 0u);
#undef A
    r.ciclos = s->ciclos; capturar(&r,s);
    if (c->excepcion) {
        /* Última operación falible: si falta memoria, reintentar este mismo
         * flanco es seguro y no duplica ni contadores ni historial. */
        if (m->cantidad_traps == m->capacidad_traps) {
            size_t capacidad = m->capacidad_traps ? m->capacidad_traps : 4;
            if (m->capacidad_traps) {
                if (capacidad > SIZE_MAX/2) return SIM_METRICAS_DESBORDAMIENTO;
                capacidad *= 2;
            }
            if (capacidad > SIZE_MAX/sizeof(SimMetricaTrap)) return SIM_METRICAS_DESBORDAMIENTO;
            SimMetricaTrap *p = realloc(m->historial,capacidad*sizeof *p);
            if (!p) return SIM_METRICAS_MEMORIA;
            m->historial = p; m->capacidad_traps = capacidad;
        }
        r.ultimo_trap = (SimMetricaTrap){s->ciclos,c->ex.pc,c->redirect_pc};
        m->historial[m->cantidad_traps++] = r.ultimo_trap;
    }
    m->acumulado = r; return SIM_METRICAS_OK;
}
/* Snapshot propio, sin secretos: exige que no falten ciclos por observar.
 * Calcula FNV-1a sobre los 65536 bytes DMEM actuales; no copia esa memoria.
 * Puede reflejar un estado LIMITE/error administrativo sin sumar un flanco. */
SimMetricasEstado sim_metricas_resultados(const SimMetricas *m, SimMetricasResumen *salida)
{
    if (!salida) return SIM_METRICAS_ARGUMENTO;
    SimMetricasEstado e = enlace(m,m ? m->simulador : NULL);
    if (e != SIM_METRICAS_OK) return e;
    const SimPipeline *s = m->simulador;
    if (s->ciclos != m->acumulado.ciclos) return SIM_METRICAS_SECUENCIA;
    if (!coincide(&m->acumulado,s)) return SIM_METRICAS_EPOCA;
    SimMetricasResumen r = m->acumulado; r.estado = s->estado;
    /* FNV-1a es una huella pública de DMEM, no de IMEM ni de la bóveda.
     * El producto uint64_t envuelve módulo 2^64, según su definición estable. */
    r.dmem_fnv1a64 = UINT64_C(14695981039346656037);
    for (size_t j=0; j<SIM_CARGADOR_DMEM_BYTES; ++j)
        r.dmem_fnv1a64 = (r.dmem_fnv1a64 ^ s->memoria.bytes[j]) * UINT64_C(1099511628211);
    r.ipc = r.ciclos ? (double)r.instrucciones_utiles/(double)r.ciclos : 0;
    r.bundles_por_ciclo = r.ciclos ? (double)r.completados/(double)r.ciclos : 0;
    *salida = r; return SIM_METRICAS_OK;
}
/* Copia el evento i del historial iniciado. Devuelve ARGUMENTO para índice
 * inválido o punteros nulos y no entrega acceso al arreglo interno. */
SimMetricasEstado sim_metricas_trap(const SimMetricas *m, size_t i, SimMetricaTrap *salida)
{
    if (!m || !m->iniciado || !salida || i>=m->cantidad_traps) return SIM_METRICAS_ARGUMENTO;
    *salida = m->historial[i]; return SIM_METRICAS_OK;
}
/* Libera el historial reservado y reinicia la estructura recibida. Admite
 * NULL; no libera el objeto del llamador ni modifica el pipeline observado. */
void sim_metricas_liberar(SimMetricas *m)
{
    if (m) { free(m->historial); *m = (SimMetricas){0}; }
}
/* Libera el texto generado por M1 y deja *s en NULL. La cadena es propia
 * del llamador y puede liberarse sin conservar el recolector. */
void sim_metricas_liberar_reporte(char **s) { if (s) { free(*s); *s=NULL; } }
static SimMetricasDiferencia diferencia(uint64_t a,uint64_t b)
{
    return (SimMetricasDiferencia){b<a,b<a ? a-b : b-a};
}
/* Solo compara métricas: NO certifica equivalencia funcional ni una optimización.
 * Comparar GPR y los 65536 bytes DMEM por separado. Base cero -> porcentaje no
 * definido y valor 0. Los resúmenes deben proceder de resultados válidos. */
SimMetricasEstado sim_metricas_comparar(const SimMetricasResumen *a, const SimMetricasResumen *b, SimMetricasComparacion *salida)
{
    if (!a || !b || !salida || !a->valido || !b->valido) return SIM_METRICAS_ARGUMENTO;
    SimMetricasComparacion r = {.ciclos=diferencia(a->ciclos,b->ciclos),
        .bundles=diferencia(a->completados,b->completados),.instrucciones=diferencia(a->instrucciones_utiles,b->instrucciones_utiles),
        .porcentaje_definido=a->ciclos!=0};
    r.diferencia_ipc = (b->ciclos ? (double)b->instrucciones_utiles/(double)b->ciclos : 0) -
                      (a->ciclos ? (double)a->instrucciones_utiles/(double)a->ciclos : 0);
    if (a->ciclos) r.cambio_porcentual_ciclos = (r.ciclos.negativa ? -1.0 : 1.0) *
        100.0*(double)r.ciclos.magnitud/(double)a->ciclos;
    *salida = r; return SIM_METRICAS_OK;
}

/* Buffer privado y lista blanca de campos públicos. Nunca se serializa
 * SimPipeline ni SimCiclo completos: podrían contener operandos sensibles. */
typedef struct { char *datos; size_t longitud,capacidad; SimMetricasEstado estado; } Buffer;
static bool reservar(Buffer *b,size_t n)
{
    if (b->estado) return false;
    if (n>SIZE_MAX-b->longitud-1) { b->estado=SIM_METRICAS_DESBORDAMIENTO; return false; }
    size_t minimo=b->longitud+n+1;
    if (minimo>b->capacidad) {
        size_t cap=b->capacidad?b->capacidad:256;
        while (cap<minimo) cap=cap>SIZE_MAX/2?minimo:cap*2;
        char *p=realloc(b->datos,cap);
        if (!p) { b->estado=SIM_METRICAS_MEMORIA; return false; }
        b->datos=p; b->capacidad=cap;
    }
    return true;
}
static void texto(Buffer *b,const char *s)
{
    size_t n=strlen(s); if (!reservar(b,n)) return;
    memcpy(b->datos+b->longitud,s,n+1); b->longitud+=n;
}
static void formato(Buffer *b,const char *f,...)
{
    if (b->estado) return;
    va_list a,c; va_start(a,f); va_copy(c,a); int n=vsnprintf(NULL,0,f,a); va_end(a);
    if (n<0) b->estado=SIM_METRICAS_GENERACION;
    else if (reservar(b,(size_t)n)) {
        if (vsnprintf(b->datos+b->longitud,(size_t)n+1,f,c)!=n) b->estado=SIM_METRICAS_GENERACION;
        else b->longitud+=(size_t)n;
    }
    va_end(c);
}
/* División larga sin multiplicar resto*10: evita overflow incluso con
 * contadores uint64_t máximos. Nueve decimales truncados, siempre con punto. */
static void cociente(Buffer *b,uint64_t n,uint64_t d)
{
    if (!d) { texto(b,"0.000000000"); return; }
    formato(b,"%" PRIu64 ".",n/d); uint64_t resto=n%d;
    for (unsigned j=0;j<9;++j) {
        uint64_t nuevo=0; unsigned digito=0;
        for (unsigned k=0;k<10;++k) {
            if (nuevo>=d-resto) { nuevo-=d-resto; ++digito; } else nuevo+=resto;
        }
        formato(b,"%u",digito); resto=nuevo;
    }
}
static void cadena(Buffer *b,const char *s)
{
    texto(b,"\"");
    for (const unsigned char *p=(const unsigned char *)s;*p;++p) {
        if (*p=='"' || *p=='\\') formato(b,"\\%c",*p);
        else if (*p<32) formato(b,"\\u%04x",*p);
        else { char t[]={(char)*p,0}; texto(b,t); }
    }
    texto(b,"\"");
}
typedef struct { const char *nombre; size_t offset; } Campo;
#define F(x) {#x,offsetof(SimMetricasResumen,x)}
static const Campo campos[] = {
    F(epoca),F(ciclos),F(admitidos),F(completados),F(anulados),F(pendientes),F(bundles_nop),F(bundles_utiles),F(instrucciones_utiles),
    F(bru_ex),F(saltos_tomados),F(saltos_no_tomados),F(flush_salto),F(anulados_salto),F(anulados_excepcion),F(busquedas_descartadas),F(traps),
    F(lsu_lecturas_ex),F(lsu_escrituras_ex),F(puerto_b_operaciones_ex),F(puerto_b_lecturas_ex),F(puerto_b_escrituras_ex),F(conflictos_memoria),F(conflictos_registro)
};
#undef F
/* Salida propia, inicialmente NULL; publicación atómica. Liberar con la función
 * específica. El reporte incluye historial de traps, nunca bóveda/contraseña. */
SimMetricasEstado sim_metricas_exportar(const SimMetricas *m,SimMetricasFormato f,char **salida)
{
    if (!salida || *salida || (unsigned)f>SIM_METRICAS_JSON) return SIM_METRICAS_ARGUMENTO;
    SimMetricasResumen r; SimMetricasEstado e=sim_metricas_resultados(m,&r); if (e) return e;
    Buffer b={0};
    if (f==SIM_METRICAS_JSON) texto(&b,"{\"version\":1,\"naturaleza\":\"dinamica\",\"estado\":");
    else texto(&b,"Métricas dinámicas VLIW\nEstado: ");
    if (f==SIM_METRICAS_JSON) cadena(&b,estados[r.estado]); else texto(&b,estados[r.estado]);
    for (size_t j=0;j<sizeof campos/sizeof *campos;++j) {
        uint64_t valor; memcpy(&valor,(const char *)&r+campos[j].offset,sizeof valor);
        formato(&b,f==SIM_METRICAS_JSON ? ",\"%s\":%" PRIu64 : "\n%s: %" PRIu64,campos[j].nombre,valor);
    }
    texto(&b,f==SIM_METRICAS_JSON ? ",\"ipc\":" : "\nIPC: "); cociente(&b,r.instrucciones_utiles,r.ciclos);
    texto(&b,f==SIM_METRICAS_JSON ? ",\"bundles_por_ciclo\":" : "\nBundles/ciclo: "); cociente(&b,r.completados,r.ciclos);
    if (f==SIM_METRICAS_JSON) texto(&b,",\"instrucciones_por_slot\":{");
    else texto(&b,"\nInstrucciones completadas WB: ");
    for (unsigned j=0;j<4;++j) formato(&b,f==SIM_METRICAS_JSON ? "%s\"%s\":%" PRIu64 : "%s%s=%" PRIu64,j?",":"",unidades[j],r.instrucciones_por_slot[j]);
    if (f==SIM_METRICAS_JSON) texto(&b,"},\"operaciones_isa\":{"); else texto(&b,"\nOperaciones WB: ");
    for (unsigned j=SIM_OP_SUMA;j<SIM_METRICAS_OPERACIONES;++j) {
        if (f==SIM_METRICAS_JSON) { if (j!=SIM_OP_SUMA) texto(&b,","); cadena(&b,sim_operacion_nombre((SimOperacion)j)); formato(&b,":%" PRIu64,r.operaciones_isa[j]); }
        else if (r.operaciones_isa[j]) formato(&b,"%s=%" PRIu64 " ",sim_operacion_nombre((SimOperacion)j),r.operaciones_isa[j]);
    }
    if (f==SIM_METRICAS_JSON) texto(&b,"},\"escrituras_por_puerto\":["); else texto(&b,"\nEscrituras habilitadas P0..P4: ");
    for (unsigned j=0;j<5;++j) formato(&b,"%s%" PRIu64,j?",":"",r.escrituras_por_puerto[j]);
    if (f==SIM_METRICAS_JSON) texto(&b,"],\"descartes_x0_por_slot\":["); else texto(&b,"\nDescartes x0 ALU/LSU/BRU/CRIPTO: ");
    for (unsigned j=0;j<4;++j) formato(&b,"%s%" PRIu64,j?",":"",r.descartes_x0_por_slot[j]);
    if (f==SIM_METRICAS_JSON) texto(&b,"],\"pendientes_por_etapa\":["); else texto(&b,"\nPendientes IFID/IDEX/EXWB: ");
    for (unsigned j=0;j<3;++j) formato(&b,"%s%u",j?",":"",r.pendientes_por_etapa[j]);
    if (f==SIM_METRICAS_JSON) texto(&b,"],\"excepciones\":["); else texto(&b,"\nExcepciones EX (ciclo,PC,vector):\n");
    for (size_t j=0;j<m->cantidad_traps;++j) {
        const SimMetricaTrap *t=&m->historial[j];
        if (f==SIM_METRICAS_JSON) formato(&b,"%s{\"ciclo\":%" PRIu64 ",\"pc\":%" PRIu32 ",\"vector\":%" PRIu32 "}",j?",":"",t->ciclo,t->pc,t->vector);
        else formato(&b,"%" PRIu64 ",0x%08" PRIx32 ",0x%08" PRIx32 "\n",t->ciclo,t->pc,t->vector);
    }
    formato(&b,f==SIM_METRICAS_JSON ? "],\"arquitectura\":{\"pc\":%" PRIu32 ",\"estado_crypto\":%" PRIu32 ",\"trap_count32\":%" PRIu32 ",\"trap_pc\":%" PRIu32 ",\"dmem_fnv1a64\":\"%016" PRIx64 "\",\"registros\":[" : "PC: 0x%08" PRIx32 "\nAUTH/INIT: %" PRIu32 "\nTraps físicos: %" PRIu32 "\nTrap PC: 0x%08" PRIx32 "\nDMEM FNV-1a64: %016" PRIx64 "\nRegistros: ",r.pc,r.estado_crypto,r.trap_count32,r.trap_pc,r.dmem_fnv1a64);
    for (unsigned j=0;j<32;++j) formato(&b,"%s%" PRIu32,j?",":"",r.registros[j]);
    texto(&b,f==SIM_METRICAS_JSON ? "]}}\n" : "\n");
    if (b.estado) { free(b.datos); return b.estado; }
    *salida=b.datos; return SIM_METRICAS_OK;
}
