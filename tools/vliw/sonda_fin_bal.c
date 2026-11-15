/* Contrasta ASM/BALB y observa control de flujo público sin sustituir S6/M1/M2. */
#include "codificacion_binaria.h"
#include "sim_ejecucion.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_ASM = 2097152, MAX_EVENTOS = 128 };
typedef struct {
    /*
     * traza conserva buses y ocupaciones anteriores al flanco; registros y pendientes
     * describen el estado posterior capturado.
     */
    SimCiclo traza;
    SimInstruccion bru;
    uint32_t registros[32];
    unsigned pendientes[3];
    SimPipelineEstado estado;
    SimMetricasResumen metricas;
    bool metricas_validas;
} Evento;

/*
 * Acepta únicamente dígitos decimales dentro del límite; publica el entero solo si la
 * conversión completa no falla.
 */
static bool numero(const char *s, uint64_t maximo, uint64_t *n)
{
    if (!s || !*s) return false;
    for (const char *p=s; *p; ++p) if (*p<'0' || *p>'9') return false;
    errno=0; char *fin; uintmax_t v=strtoumax(s,&fin,10);
    if (errno || *fin || v>maximo) return false;
    *n=(uint64_t)v; return true;
}
/*
 * Lee el ASM acotado para contrastar instrucciones con la imagen; devuelve almacenamiento
 * propio que el llamador debe liberar.
 */
static char *leer(const char *ruta)
{
    FILE *f=fopen(ruta,"rb"); if (!f) return NULL;
    char *s=malloc(MAX_ASM+1u); if (!s) { fclose(f); return NULL; }
    size_t n=fread(s,1,MAX_ASM+1u,f);
    bool ok=!ferror(f) && n<=MAX_ASM && !memchr(s,0,n);
    if (fclose(f)) ok=false;
    if (!ok) { free(s); return NULL; }
    s[n]=0; return s;
}
/*
 * Busca en los bundles de CB el primero cuya línea alcanza la anotación; devuelve SIZE_MAX si
 * no hay correspondencia.
 */
static size_t indice(const CbPrograma *p, uint64_t linea)
{
    if (!linea) return SIZE_MAX;
    /* CbBundle.linea pertenece al ensamblador aprobado: no se cuenta una
     * línea como bundle ni se asignan direcciones con otro ensamblador. */
    for (size_t i=0;i<p->cantidad;++i) if (p->bundles[i].linea>=linea) return i;
    return SIZE_MAX;
}
/*
 * Comprueba que las cuatro palabras del bundle sean cero para reconocer un NOP completo en la
 * imagen.
 */
static bool vacio(const uint32_t *s) { return !(s[0]|s[1]|s[2]|s[3]); }
/*
 * Toma una muestra del estado público y de los buses del flanco para registrar efectos
 * anteriores y posteriores al control.
 */
static void capturar(Evento *e, const SimPipeline *s)
{
    e->traza=s->ultimo_ciclo; e->estado=s->estado; e->metricas_validas=false;
    memcpy(e->registros,s->registros,sizeof e->registros);
    e->pendientes[0]=s->if_id.valido; e->pendientes[1]=s->id_ex.valido;
    e->pendientes[2]=s->ex_wb.valido;
    memset(&e->bru,0,sizeof e->bru);
    if (e->traza.ex.valido && e->traza.ex.pc/16<1024)
        (void)sim_decode_palabra(s->imagen->imem[e->traza.ex.pc/16][2],2,&e->bru);
}
/*
 * Serializa un evento de control con su contexto de flanco y efectos públicos; no lo
 * interpreta como terminación semántica.
 */
static void evento(const Evento *e, bool detalle)
{
    const SimCiclo *t=&e->traza;
    printf("{\"ciclo\":%"PRIu64",\"ex_pc\":%"PRIu32",\"bru_valida\":%s,\"operacion\":\"%s\","
           "\"tomado\":%s,\"destino\":%"PRIu32",\"excepcion\":%s,\"redirect_pc\":%"PRIu32","
           "\"rd\":%u,\"rs1\":%u,\"inmediato\":%"PRId32",\"enlace\":%"PRIu32,
           t->ciclo,t->ex.pc,t->bru_ex_valido?"true":"false",sim_operacion_nombre(e->bru.operacion),
           t->ex_bru.tomado?"true":"false",t->ex_bru.destino,t->excepcion?"true":"false",
           t->redirect_pc,e->bru.rd,e->bru.rs1,e->bru.inmediato,t->ex_bru.valor_enlace);
    if (detalle) {
        /* La traza describe ocupación ANTES del flanco; banco y pendientes
         * capturados describen DESPUÉS. WB anterior puede escribir en ese flanco. */
        printf(",\"estado_post\":%d,\"etapas_pre\":[%s,%s,%s,%s],\"pendientes_post\":[%u,%u,%u],"
               "\"wb_pre\":{\"pc\":%"PRIu32",\"alu\":%s,\"lsu\":%s,\"bru\":%s,\"crypto\":[%s,%s]},\"registros_post\":[",
               (int)e->estado,t->if_.valido?"true":"false",t->id.valido?"true":"false",
               t->ex.valido?"true":"false",t->wb.valido?"true":"false",
               e->pendientes[0],e->pendientes[1],e->pendientes[2],t->wb.pc,
               t->wb_escribe?"true":"false",t->lsu_wb_escribe?"true":"false",
               t->bru_wb_escribe?"true":"false",t->crypto_wb_escribe[0]?"true":"false",
               t->crypto_wb_escribe[1]?"true":"false");
        for (unsigned i=0;i<32;++i) printf("%s%"PRIu32,i?",":"",e->registros[i]);
        putchar(']');
    }
    if (detalle && e->metricas_validas)
        printf(",\"metricas_post\":{\"ciclos\":%"PRIu64",\"bundles\":%"PRIu64",\"instrucciones\":%"PRIu64",\"ipc\":%.17g}",
               e->metricas.ciclos,e->metricas.completados,e->metricas.instrucciones_utiles,e->metricas.ipc);
    putchar('}');
}
/*
 * Conserva una observación destacada separada del prefijo acotado para no perder eventos
 * relevantes tardíos.
 */
static void destacar(Evento *destino, const Evento *origen, const SimMetricas *m)
{
    *destino=*origen;
    /* El snapshot procede de M1 después de observar el mismo flanco. Solo
     * se consulta en puntos destacados, sin mantener contadores alternativos. */
    destino->metricas_validas=sim_metricas_resultados(m,&destino->metricas)==SIM_METRICAS_OK;
}
/*
 * Escribe null para un evento no observado o serializa la observación disponible; la ausencia
 * no se sustituye por ceros.
 */
static void opcional(bool existe, const Evento *e) { if (existe) evento(e,true); else fputs("null",stdout); }

/*
 * Contrasta las posiciones ASM con el BALB ejecutado, registra control y flancos posteriores y
 * compara M1/DMEM con M2 al mismo horizonte; produce un diagnóstico, no una terminación
 * artificial.
 */
int main(int argc, char **argv)
{
    /*
     * Las posiciones de línea recibidas se contrastan con CB; no se convierten directamente en
     * direcciones de instrucciones.
     */
    uint64_t ls,lm,presupuesto;
    if (argc!=6 || !numero(argv[3],UINT32_MAX,&ls) || !numero(argv[4],UINT32_MAX,&lm) ||
        !numero(argv[5],1000000,&presupuesto)) {
        fputs("uso: sonda_fin_bal archivo.asm archivo.bin linea_sentinela linea_main presupuesto(0..1000000)\n",stderr); return 64;
    }
    int salida=1; char *fuente=leer(argv[1]), *reporte=NULL, *r2=NULL;
    CbPrograma codigo={0}; SimImagen *imagen=NULL; SimPipeline *s=NULL;
    SimMetricas m={0}; SimEjecucion *m2=NULL;
    if (!fuente || cb_ensamblar(fuente,&codigo,stderr) ||
        sim_cargar_balb(argv[2],&imagen,stderr)!=SIM_CARGA_OK) goto liberar;
    /* Se compara TODO el código, no solo la palabra con apariencia de salto.
     * La etiqueta/nota ASM no autentica su origen: esta prueba certifica bytes. */
    if (codigo.cantidad!=imagen->cantidad_bundles) goto discordancia;
    for (size_t i=0;i<codigo.cantidad;++i)
        if (memcmp(codigo.bundles[i].slots,imagen->imem[i],sizeof imagen->imem[i])) goto discordancia;
    // CB conserva la línea de origen de cada bundle; con ella localizamos sentinela y main.
    // Recorremos hacia atrás los NOP para buscar la forma de llamada, sin convertir directamente
    // una línea ASM en dirección.
    size_t sent=indice(&codigo,ls), main_=indice(&codigo,lm), llamada=SIZE_MAX;
    SimInstruccion b={0}; bool patron=false;
    if (sent!=SIZE_MAX) {
        const uint32_t *w=imagen->imem[sent];
        patron=sim_decode_palabra(w[2],2,&b)==SIM_DECODE_OK && b.operacion==SIM_OP_SYE &&
            b.rd==0 && b.inmediato==0 && !(w[0]|w[1]|w[3]);
        size_t i=sent;
        while (i && vacio(imagen->imem[i-1])) --i;
        if (i) {
            SimInstruccion call;
            const uint32_t *wcall=imagen->imem[i-1];
            if (sim_decode_palabra(wcall[2],2,&call)==SIM_DECODE_OK &&
                call.operacion==SIM_OP_SYER && call.rd==1 && call.rs1==13 && call.inmediato==0 &&
                !(wcall[0]|wcall[1]|wcall[3])) llamada=i-1;
        }
    }
    s=calloc(1,sizeof *s);
    if (!s || sim_pipeline_iniciar(s,imagen)!=SIM_PIPE_ACTIVO || sim_metricas_iniciar(&m,s)!=SIM_METRICAS_OK) goto liberar;
    Evento eventos[MAX_EVENTOS],call={0},ret={0},directo={0},bucle={0},posteriores[2];
    size_t cantidad=0,despues=0; uint64_t total=0;
    bool vio_call=false,vio_ret=false,vio_directo=false,vio_bucle=false;
    SimMetricasEstado observacion=SIM_METRICAS_OK;
    for (uint64_t i=0;i<presupuesto;++i) {
        uint64_t anterior=s->ciclos;
        (void)sim_avanzar_ciclo(s);
        /* Solo el cambio de ciclos demuestra flanco aceptado. Un intento
         * rechazado no repite la última traza ni añade eventos o métricas. */
        if (s->ciclos!=anterior) {
            observacion=sim_metricas_observar(&m,s);
            for (unsigned k=0;k<2 && observacion==SIM_METRICAS_MEMORIA;++k)
                observacion=sim_metricas_observar(&m,s);
            if (observacion!=SIM_METRICAS_OK) break;
            Evento e; capturar(&e,s);
            // Las dos muestras posteriores al primer salto final se conservan si hay
            // presupuesto. No cortamos aquí la ejecución: el salto es un hecho de control, no
            // una orden de terminar el simulador.
            if (vio_bucle && despues<2) destacar(&posteriores[despues++],&e,&m);
            const SimCiclo *t=&e.traza;
            // El prefijo conserva hasta 128 eventos y el total sigue aumentando. Llamada,
            // retorno y sentinela tienen copias destacadas aparte para que un evento tardío no
            // se pierda por llenar el prefijo.
            if (t->bru_ex_valido || t->excepcion) {
                ++total;
                if (cantidad<MAX_EVENTOS) eventos[cantidad++]=e;
            }
            if (patron && t->bru_ex_valido && t->ex_bru.tomado && !t->excepcion) {
                if (!vio_call && llamada!=SIZE_MAX && main_!=SIZE_MAX &&
                    t->ex.pc==llamada*16 && t->ex_bru.destino==main_*16) {destacar(&call,&e,&m);vio_call=true;}
                if (!vio_directo && t->ex.pc!=sent*16 && t->ex_bru.destino==sent*16) {destacar(&directo,&e,&m);vio_directo=true;}
                /* Es un candidato compatible con ret hacia el enlace inicial.
                 * rs1=x1 y destino correcto no prueban identidad de la llamada:
                 * las interfaces no poseen un identificador dinámico de frames. */
                if (!vio_ret && vio_call && t->ciclo>call.traza.ciclo && e.bru.operacion==SIM_OP_SYER &&
                    e.bru.rd==0 && e.bru.rs1==1 && e.bru.inmediato==0 &&
                    t->ex_bru.destino==call.traza.ex_bru.valor_enlace) {destacar(&ret,&e,&m);vio_ret=true;}
                if (!vio_bucle && t->ex.pc==sent*16 && e.bru.operacion==SIM_OP_SYE &&
                    t->ex_bru.destino==sent*16) {destacar(&bucle,&e,&m);vio_bucle=true;}
            }
        }
        if (s->estado!=SIM_PIPE_ACTIVO && s->estado!=SIM_PIPE_LIMITE) break;
    }
    // La pausa administrativa de presupuesto no agrega un flanco. Solo después de observar M1
    // consultamos el resumen; un error de observación impide fingir que las métricas están
    // completas.
    if (observacion==SIM_METRICAS_OK) (void)sim_pipeline_ejecutar(s,0);
    bool dinamico_error=s->estado>SIM_PIPE_LIMITE || observacion!=SIM_METRICAS_OK;
    if (observacion==SIM_METRICAS_OK && sim_metricas_exportar(&m,SIM_METRICAS_JSON,&reporte)!=SIM_METRICAS_OK) goto liberar;
    bool comparado=false,igual_memoria=false,igual_metricas=false,igual_estado=false;
    // M2 carga una sesión nueva y ejecuta el mismo presupuesto. Sus reintentos registran el
    // flanco pendiente y descuentan los ciclos ya aceptados antes de continuar; comparamos
    // estado, JSON M1 y DMEM completa.
    if (!dinamico_error && sim_ejecucion_crear(argv[2],SIM_EJECUCION_BALB,SIM_BUSQUEDA_FINITA,&m2,stderr)==SIM_EJECUCION_OK) {
        SimEjecucionCodigo c=sim_ejecucion_ejecutar(m2,presupuesto);
        for (unsigned k=0;k<2 && c==SIM_EJECUCION_PENDIENTE;++k) {
            c=sim_ejecucion_reintentar(m2);
            if (c==SIM_EJECUCION_OK) {
                SimMetricasResumen parcial;
                if (sim_ejecucion_resultados(m2,&parcial)!=SIM_METRICAS_OK || parcial.ciclos>presupuesto) break;
                c=sim_ejecucion_ejecutar(m2,presupuesto-parcial.ciclos);
            }
            /* Se reobserva el flanco pendiente antes de ejecutar el presupuesto
             * restante; nunca se repite la instrucción que ya aceptó S6. */
        }
        if (c==SIM_EJECUCION_OK) {
            uint8_t memoria[SIM_CARGADOR_DMEM_BYTES]; SimEjecucionEstado estado;
            if (sim_ejecucion_memoria(m2,memoria,sizeof memoria)==SIM_EJECUCION_OK &&
                sim_ejecucion_estado(m2,&estado)==SIM_EJECUCION_OK &&
                sim_ejecucion_exportar(m2,SIM_METRICAS_JSON,&r2)==SIM_METRICAS_OK) {
                comparado=true;igual_memoria=!memcmp(memoria,s->memoria.bytes,sizeof memoria);
                igual_metricas=reporte && !strcmp(reporte,r2);igual_estado=estado.pipeline==s->estado;
            }
        }
    }
    printf("{\"version\":1,\"codigo_coincide\":true,\"patron_valido\":%s,\"direccion_sentinela\":",patron?"true":"false");
    if (sent!=SIZE_MAX) printf("%zu",sent*16); else fputs("null",stdout);
    printf(",\"linea_bundle\":%u,\"eventos_totales\":%"PRIu64",\"eventos_truncados\":%s,\"eventos_control\":[",
           sent==SIZE_MAX?0:codigo.bundles[sent].linea,total,total>cantidad?"true":"false");
    for (size_t i=0;i<cantidad;++i) {if(i)putchar(',');evento(&eventos[i],false);}
    fputs("],\"llamada_inicial_observada\":",stdout);opcional(vio_call,&call);
    fputs(",\"retorno_candidato\":",stdout);opcional(vio_ret,&ret);
    fputs(",\"redireccion_a_sentinela\":",stdout);opcional(vio_directo,&directo);
    fputs(",\"bucle_final_observado\":",stdout);opcional(vio_bucle,&bucle);
    fputs(",\"flancos_posteriores\":[",stdout);
    for(size_t i=0;i<despues;++i) {if(i)putchar(',');evento(&posteriores[i],true);}
    printf("],\"estado_simulador\":%d,\"error_dinamico\":%s,\"observacion_m1\":%d,"
           "\"comparacion_m2\":{\"disponible\":%s,\"memoria_completa_igual\":%s,\"metricas_iguales\":%s,\"estado_igual\":%s},\"metricas\":%s}\n",
           (int)s->estado,dinamico_error?"true":"false",(int)observacion,
           comparado?"true":"false",igual_memoria?"true":"false",igual_metricas?"true":"false",
           igual_estado?"true":"false",reporte?reporte:"null");
    salida=ferror(stdout) || fflush(stdout) ? 1 : 0;
    goto liberar;
discordancia:
    fputs("sonda: ASM y BALB no corresponden\n",stderr);
liberar:
    // La misma limpieza atiende éxito o error: destruye reportes, sesión M2, historial M1,
    // núcleo, imagen S1 y ensamblado CB. Ningún objeto prestado queda accesible después de
    // liberar su propietario.
    if (salida && !ferror(stderr)) fputs("sonda: análisis incompleto; no se certifica finalización\n",stderr);
    sim_metricas_liberar_reporte(&reporte);sim_metricas_liberar_reporte(&r2);
    sim_ejecucion_liberar(&m2);sim_metricas_liberar(&m);free(s);
    sim_liberar_imagen(&imagen);cb_liberar(&codigo);free(fuente);return salida;
}
