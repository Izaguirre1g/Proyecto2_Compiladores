/* Captura un flanco drenado tras un retorno candidato, sin ejecutar la sentinela. */
#include "sim_ejecucion.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define DESCONOCIDO UINT32_MAX
enum { MAX_MUESTRAS = 16 };
typedef struct {
    /*
     * ciclo identifica el flanco observado, no el número de llamada ni una terminación
     * semántica.
     */
    uint64_t ciclo;
    uint32_t pc, destino, enlace;
    bool valido;
} Control;
typedef struct {
    uint64_t ciclo;
    /*
     * Las banderas distinguen ocupación de etapas de efectos aún pendientes: una etapa válida
     * con NOP puede ser segura para el corte.
     */
    bool ifid, idex, exwb, wb_gpr, wb_crypto, ex_lsu, ex_crypto, ruta, retorno_wb;
    uint32_t pc_ifid, pc_idex, pc_exwb;
} Pendientes;

/*
 * Acepta únicamente dígitos decimales dentro del límite; publica el entero solo si la
 * conversión completa no falla.
 */
static bool numero(const char *s, uint64_t limite, uint64_t *n)
{
    if (!s || !*s) return false;
    for (const char *p=s;*p;++p) if (*p<'0' || *p>'9') return false;
    errno=0; char *fin;uintmax_t v=strtoumax(s,&fin,10);
    if (errno || *fin || v>limite) return false;
    *n=(uint64_t)v;return true;
}
/*
 * Acepta una dirección de bundle alineada o el marcador desconocido; rechaza valores fuera de
 * IMEM.
 */
static bool direccion(const char *s,uint32_t *v)
{
    uint64_t n;
    if (!strcmp(s,"-")) {*v=DESCONOCIDO;return true;}
    if (!numero(s,16368,&n) || n%16) return false;
    *v=(uint32_t)n;return true;
}
/*
 * Decodifica el slot del bundle indicado si pertenece a la imagen cargada; devuelve false ante
 * ausencia o codificación inválida.
 */
static bool instruccion(const SimImagen *i,uint32_t pc,unsigned slot,SimInstruccion *d)
{
    return pc/16<i->cantidad_bundles &&
        sim_decode_palabra(i->imem[pc/16][slot],slot,d)==SIM_DECODE_OK;
}
/*
 * Comprueba con S2 que el bundle completo sea NOP, sin confundir un PC no cargado con código
 * demostrado.
 */
static bool nop(const SimImagen *i,uint32_t pc)
{
    if (pc/16>=i->cantidad_bundles) return false;
    SimInstruccion d[4];
    return sim_decode_bundle(i->imem[pc/16],d)==SIM_DECODE_NOP;
}
/*
 * Comprueba que ALU, LSU y CRIPTO estén vacíos en el bundle; la validez de BRU se comprueba
 * por separado.
 */
static bool solo_bru(const SimImagen *i,uint32_t pc)
{
    return pc/16<i->cantidad_bundles &&
        !(i->imem[pc/16][0]|i->imem[pc/16][1]|i->imem[pc/16][3]);
}
/*
 * Comprueba que la continuación entre la llamada y la sentinela contenga exclusivamente
 * bundles NOP.
 */
static bool corredor(const SimImagen *i,uint32_t llamada,uint32_t sent)
{
    if (llamada==DESCONOCIDO || llamada>=sent) return false;
    for (uint32_t pc=llamada+16;pc<sent;pc+=16) if (!nop(i,pc)) return false;
    return true;
}
/*
 * Acepta únicamente NOP del corredor demostrado o la sentinela aún pendiente; no presume
 * inocuo código ajeno a la continuación.
 */
static bool permitido(const SimImagen *i,uint32_t pc,uint32_t cont,uint32_t sent)
{
    /* Solo se aceptan bundles de la continuación verificada: NOP o la propia
     * sentinela aún pendiente. Una instrucción posterior desconocida no se
     * presume inocua ni descartable para obtener un corte favorable. */
    return pc>=cont && pc<=sent && (pc==sent || nop(i,pc));
}
/*
 * Captura validez, PC y efectos aún pendientes de WB/EX sin acceder a la bóveda ni aplicar
 * operaciones; devuelve evidencia por valor.
 */
static Pendientes pendientes(const SimPipeline *s,bool ruta,bool retirado)
{
    const SimExWb *w=&s->ex_wb;
    Pendientes p={.ciclo=s->ciclos,.ifid=s->if_id.valido,.idex=s->id_ex.valido,
        .exwb=w->valido,.pc_ifid=s->if_id.pc,.pc_idex=s->id_ex.pc,.pc_exwb=w->pc,
        .ruta=ruta,.retorno_wb=retirado};
    // En EX/WB examinamos cada puerto de escritura y excluimos x0: su descarte no cambia GPR.
    // AUTH/INIT sí alteran el estado público y se revisan aparte; no basta con que ALU no
    // escriba.
    p.wb_gpr=w->valido && ((w->operacion!=SIM_OP_NOP && w->destino!=0) ||
        (w->lsu.valida && w->lsu.destino!=0) || (w->bru.valida && w->bru.destino!=0) ||
        (w->crypto.par && (w->crypto.rd!=0 || w->crypto.rd2!=0)));
    p.wb_crypto=w->valido && (w->crypto.auth_we || w->crypto.init_clr);
    /* LSU y efectos privados/públicos de CRIPTO se aplican en EX; si la
     * operación sigue en ID/EX todavía no se presume publicada. No se lee
     * el contenido de la bóveda ni se preparan/aplican efectos desde B5. */
    p.ex_lsu=s->id_ex.valido && s->id_ex.lsu.valido;
    p.ex_crypto=s->id_ex.valido && s->id_ex.crypto.valido;
    return p;
}
/*
 * Serializa validez, PC y nombres de las cuatro operaciones de una etapa usando la imagen
 * prestada.
 */
static void etapa(bool valida,uint32_t pc,const SimImagen *imagen)
{
    printf("{\"valido\":%s,\"pc\":%"PRIu32",\"operaciones\":",valida?"true":"false",pc);
    // Solo decodificamos operaciones cuando la etapa es válida y su PC pertenece al código
    // cargado. Una etapa inválida exporta operaciones null; no fabricamos instrucciones
    // pendientes a partir de su PC residual.
    if (valida && pc/16<imagen->cantidad_bundles) {
        SimInstruccion d[4];(void)sim_decode_bundle(imagen->imem[pc/16],d);
        putchar('[');
        for (unsigned j=0;j<4;++j)printf("%s\"%s\"",j?",":"",sim_operacion_nombre(d[j].operacion));
        putchar(']');
    } else fputs("null",stdout);
    putchar('}');
}
/*
 * Escribe la muestra de pipeline y sus garantías de drenado; describe los efectos observados
 * sin modificar etapas.
 */
static void escribir_pendientes(const Pendientes *p,const SimImagen *imagen)
{
    printf("{\"ciclo\":%"PRIu64",\"if_id\":",p->ciclo);etapa(p->ifid,p->pc_ifid,imagen);
    fputs(",\"id_ex\":",stdout);etapa(p->idex,p->pc_idex,imagen);
    fputs(",\"ex_wb\":",stdout);etapa(p->exwb,p->pc_exwb,imagen);
    printf(",\"wb_gpr_pendiente\":%s,\"wb_crypto_pendiente\":%s,\"ex_lsu_pendiente\":%s,"
           "\"ex_crypto_pendiente\":%s,\"ruta_continuacion\":%s,\"retorno_retirado_wb\":%s}",
           p->wb_gpr?"true":"false",p->wb_crypto?"true":"false",p->ex_lsu?"true":"false",
           p->ex_crypto?"true":"false",p->ruta?"true":"false",p->retorno_wb?"true":"false");
}
/*
 * Escribe el evento de llamada/retorno observado o null si falta; un dato no observado no se
 * fabrica a partir del PC.
 */
static void control(const Control *c)
{
    if (!c->valido) {fputs("null",stdout);return;}
    printf("{\"ciclo\":%"PRIu64",\"ex_pc\":%"PRIu32",\"destino\":%"PRIu32",\"enlace\":%"PRIu32"}",
           c->ciclo,c->pc,c->destino,c->enlace);
}
/*
 * Valida BALB y el corredor de continuación, observa el retiro del retorno en WB y exige
 * ausencia de efectos pendientes; contrasta GPR, métricas y DMEM con M2 antes de publicar una
 * captura ACTIVA. Libera todos los recursos si falla.
 */
int main(int argc,char **argv)
{
    uint32_t sent,call,main_;uint64_t presupuesto;
    if (argc!=7 || !direccion(argv[2],&sent) || !direccion(argv[3],&call) ||
        !direccion(argv[4],&main_) || !numero(argv[5],1000000,&presupuesto)) {
        fputs("uso: sonda_corte_bal imagen.bin pc_sentinela|- pc_llamada|- pc_main|- presupuesto salida.dmem\n",stderr);return 64;
    }
    int salida=1;SimImagen *imagen=NULL;SimPipeline *s=NULL;SimMetricas m={0};
    SimEjecucion *m2=NULL;char *reporte=NULL,*r2=NULL;
    if (sim_cargar_balb(argv[1],&imagen,stderr)!=SIM_CARGA_OK) goto liberar;
    SimInstruccion ds,dc;
    /*
     * Primero demostramos la forma estática de la sentinela y de la llamada; un PC repetido
     * por sí solo no satisface ese contrato.
     */
    bool patron=sent!=DESCONOCIDO && instruccion(imagen,sent,2,&ds) &&
        solo_bru(imagen,sent) && ds.operacion==SIM_OP_SYE && ds.rd==0 && ds.inmediato==0;
    bool contrato=patron && main_!=DESCONOCIDO && main_/16<imagen->cantidad_bundles &&
        call!=DESCONOCIDO && instruccion(imagen,call,2,&dc) && solo_bru(imagen,call) &&
        dc.operacion==SIM_OP_SYER && dc.rd==1 && dc.rs1==13 && dc.inmediato==0 && corredor(imagen,call,sent);
    s=calloc(1,sizeof *s);
    if (!s || sim_pipeline_iniciar(s,imagen)!=SIM_PIPE_ACTIVO || sim_metricas_iniciar(&m,s)!=SIM_METRICAS_OK) goto liberar;
    Control llamada={0},ret={0},bucle={0};Pendientes muestras[MAX_MUESTRAS];size_t cantidad=0;
    bool corte=false,ruta=false,retirado=false,impedido=false,excepcion=false;
    uint32_t esperado=0;SimMetricasEstado observacion=SIM_METRICAS_OK;
    // Observamos un intento a la vez y distinguimos el flanco aceptado comparando ciclos. M1
    // debe registrar ese flanco antes de avanzar; sus reintentos por memoria no ejecutan de
    // nuevo la CPU.
    for (uint64_t n=0;n<presupuesto;++n) {
        uint64_t antes=s->ciclos;
        (void)sim_avanzar_ciclo(s);
        if (s->ciclos!=antes) {
            observacion=sim_metricas_observar(&m,s);
            for (unsigned k=0;k<2 && observacion==SIM_METRICAS_MEMORIA;++k)
                observacion=sim_metricas_observar(&m,s);
            if (observacion!=SIM_METRICAS_OK) break;
            const SimCiclo *t=&s->ultimo_ciclo;
            excepcion|=t->excepcion;
            SimInstruccion b;
            // Un evento de control exige BRU válida, salto tomado y ausencia de excepción. Una
            // redirección criptográfica no es un retorno; tampoco se deduce un evento de un PC
            // repetido.
            bool bru=t->bru_ex_valido && t->ex_bru.tomado && !t->excepcion &&
                instruccion(imagen,t->ex.pc,2,&b);
            // Si ya ejecutamos el salto de la sentinela, este recorrido no puede ofrecer un
            // corte anterior. Conservamos el hecho observado y terminamos la búsqueda sin
            // fabricar una captura.
            if (patron && bru && t->ex.pc==sent && t->ex_bru.destino==sent) {
                bucle=(Control){t->ciclo,t->ex.pc,t->ex_bru.destino,t->ex_bru.valor_enlace,true};
                break;
            }
            if (contrato && bru && !llamada.valido && t->ex.pc==call && t->ex_bru.destino==main_)
                llamada=(Control){t->ciclo,t->ex.pc,t->ex_bru.destino,t->ex_bru.valor_enlace,true};
            // Tras observar la llamada buscamos SYER x0,x1,0 hacia su enlace. Guardamos ese
            // candidato y comprobamos el flush y el PC de continuación; esto prueba una ruta
            // local, sin autenticar la función que retorna.
            if (contrato && bru && llamada.valido && !ret.valido && t->ciclo>llamada.ciclo &&
                b.operacion==SIM_OP_SYER && b.rd==0 && b.rs1==1 && b.inmediato==0 &&
                t->ex_bru.destino==llamada.enlace) {
                ret=(Control){t->ciclo,t->ex.pc,t->ex_bru.destino,t->ex_bru.valor_enlace,true};
                ruta=t->flush && !s->if_id.valido && !s->id_ex.valido && s->pc==llamada.enlace;
                esperado=llamada.enlace;
                /* ret hacia x1 solo demuestra compatibilidad con el enlace.
                 * No se reconstruyen frames ni se certifica identidad de main. */
            } else if (ret.valido) {
                // Después del retorno exigimos búsquedas secuenciales en la continuación y
                // ningún flush adicional. El WB de su mismo PC confirma el retiro: el evento EX
                // por sí solo todavía no acredita drenado.
                if (t->flush) ruta=false;
                if (t->if_.valido && !t->if_descartado) {
                    ruta &= t->if_.pc==esperado && t->if_.pc<=sent;
                    esperado+=16;
                }
                retirado |= t->wb.valido && t->wb.pc==ret.pc;
            }
            if (ret.valido) {
                // Conservamos hasta 16 muestras de etapas y efectos. Las marcas de ocupación
                // permiten auditar el pipeline ACTIVO; las marcas de efectos determinan si
                // todavía podría cambiar el estado arquitectónico.
                Pendientes p=pendientes(s,ruta,retirado);
                if (cantidad<MAX_MUESTRAS) muestras[cantidad++]=p;
                /*
                 * Después del retorno, solo aceptamos etapas dentro de la continuación
                 * verificada; un efecto anterior todavía pendiente impide capturar.
                 */
                bool id_seguro=!s->if_id.valido || permitido(imagen,s->if_id.pc,llamada.enlace,sent);
                bool ex_seguro=!s->id_ex.valido || permitido(imagen,s->id_ex.pc,llamada.enlace,sent);
                bool wb_seguro=!s->ex_wb.valido || nop(imagen,s->ex_wb.pc);
                /* Se espera evidencia del retiro del propio bundle de retorno.
                 * En orden, al publicarse ese WB ya se publicaron los anteriores.
                 * No se exige pipeline vacío: puede estar IF/ID con un NOP. */
                if (ruta && retirado && s->if_id.valido && s->if_id.pc==llamada.enlace &&
                    id_seguro && ex_seguro && wb_seguro && !p.wb_gpr && !p.wb_crypto &&
                    !p.ex_lsu && !p.ex_crypto && !excepcion && !t->excepcion) {
                    corte=true;break;
                }
                impedido |= !ruta || !id_seguro || !ex_seguro || excepcion;
            }
        }
        if (s->estado>SIM_PIPE_LIMITE || s->estado==SIM_PIPE_TERMINADO) break;
    }
    bool error=observacion!=SIM_METRICAS_OK || s->estado>SIM_PIPE_LIMITE;
    bool comparado=false,mem_igual=false,metricas_iguales=false,estado_igual=false;
    /*
     * Antes de publicar, ejecutamos M2 independientemente hasta el mismo número de flancos y
     * exigimos igualdad de estado, métricas y toda DMEM.
     */
    if (corte && !error) {
        if (sim_metricas_exportar(&m,SIM_METRICAS_JSON,&reporte)!=SIM_METRICAS_OK) goto liberar;
        if (sim_ejecucion_crear(argv[1],SIM_EJECUCION_BALB,SIM_BUSQUEDA_FINITA,&m2,stderr)!=SIM_EJECUCION_OK) goto liberar;
        /* Avanzar M2 exactamente el mismo número de flancos conserva ACTIVO.
         * No se añade una pausa administrativa ni se asigna estado a S6. */
        for (uint64_t j=0;j<s->ciclos;++j) {
            SimEjecucionCodigo c=sim_ejecucion_avanzar(m2);
            for (unsigned k=0;k<2 && c==SIM_EJECUCION_PENDIENTE;++k)c=sim_ejecucion_reintentar(m2);
            if (c!=SIM_EJECUCION_OK) goto liberar;
        }
        uint8_t copia[SIM_CARGADOR_DMEM_BYTES];SimEjecucionEstado estado;
        if (sim_ejecucion_memoria(m2,copia,sizeof copia)!=SIM_EJECUCION_OK ||
            sim_ejecucion_estado(m2,&estado)!=SIM_EJECUCION_OK ||
            sim_ejecucion_exportar(m2,SIM_METRICAS_JSON,&r2)!=SIM_METRICAS_OK) goto liberar;
        // Comparamos los 65536 bytes y el JSON M1 completo, que incluye los 32 GPR, además del
        // estado S6. Igualar solo un hash o unos registros no ofrecería el mismo alcance de
        // contraste.
        comparado=true;mem_igual=!memcmp(copia,s->memoria.bytes,sizeof copia);
        metricas_iguales=!strcmp(reporte,r2);estado_igual=estado.pipeline==s->estado;
        error=!(mem_igual && metricas_iguales && estado_igual);
        if (!error) {
            /* La copia se publica solo tras contrastar los 65536 bytes con M2.
             * No se utiliza un hash como sustituto de esa comparación. */
            FILE *f=fopen(argv[6],"wbx");if (!f) goto liberar;
            bool ok=fwrite(s->memoria.bytes,1,sizeof s->memoria.bytes,f)==sizeof s->memoria.bytes;
            if (fclose(f))ok=false;
            if (!ok) goto liberar;
        }
    }
    // La clasificación separa errores, corte probado localmente, ausencia de prueba y
    // presupuesto agotado. Solo el candidato sin error lleva punto y métricas; ningún resultado
    // cambia S6 a TERMINADO.
    const char *conclusion=error?"ERROR":corte?"CORTE_ARQUITECTONICO_CANDIDATO":
        (!patron || bucle.valido || impedido || excepcion || s->estado==SIM_PIPE_TERMINADO)?"CORTE_NO_DEMOSTRADO":"PRESUPUESTO_AGOTADO";
    fputs("{\"direccion_sentinela\":",stdout);
    if(sent==DESCONOCIDO) fputs("null",stdout); else printf("%"PRIu32,sent);
    printf(",\"version\":1,\"contrato_local\":%s,\"patron_local\":%s,\"retorno_main_certificado\":false,"
           "\"conclusion\":\"%s\",\"ciclos_observados\":%"PRIu64",\"estado_simulador\":%d,\"observacion_m1\":%d,"
           "\"excepciones_observadas\":%s,\"llamada_inicial\":",contrato?"true":"false",patron?"true":"false",
           conclusion,s->ciclos,(int)s->estado,(int)observacion,excepcion?"true":"false");control(&llamada);
    fputs(",\"retorno_candidato\":",stdout);control(&ret);
    fputs(",\"primer_bucle_final\":",stdout);control(&bucle);
    fputs(",\"punto_corte\":",stdout);
    if (corte && !error) printf("{\"ciclo\":%"PRIu64",\"flanco_aceptado\":true,\"observado_m1\":true}",s->ciclos);
    else fputs("null",stdout);
    fputs(",\"evidencia_pipeline\":[",stdout);
    for(size_t i=0;i<cantidad;++i){if(i)putchar(',');escribir_pendientes(&muestras[i],imagen);}
    printf("],\"comparacion_m2\":{\"disponible\":%s,\"memoria_completa_igual\":%s,\"metricas_iguales\":%s,"
           "\"estado_igual\":%s},\"metricas\":%s}\n",comparado?"true":"false",mem_igual?"true":"false",
           metricas_iguales?"true":"false",estado_igual?"true":"false",corte && !error?reporte:"null");
    salida=ferror(stdout) || fflush(stdout)?1:0;
/*
 * La limpieza común libera reportes, sesión, historial, núcleo e imagen incluso si el análisis
 * falló a mitad de la preparación.
 */
liberar:
    if(salida)fputs("sonda corte: análisis incompleto; ninguna captura válida publicada\n",stderr);
    sim_metricas_liberar_reporte(&reporte);sim_metricas_liberar_reporte(&r2);
    sim_ejecucion_liberar(&m2);sim_metricas_liberar(&m);free(s);sim_liberar_imagen(&imagen);return salida;
}
