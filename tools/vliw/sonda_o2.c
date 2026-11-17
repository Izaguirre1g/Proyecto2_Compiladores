/* Observa el retorno REAL de main al arranque, sin inventar HALT ni cambiar
 * M2/B1. Consume imagen MEM e identidades PC emitidas por el enlace O2. Usa
 * S1/S6/M1 públicos, registra transferencias y exige SP/GP/RA y s0..s15.
 * Publica dumps al corte posterior al retorno; sigue ACTIVO arquitectónico.
 * Las salidas pertenecen al directorio externo proporcionado por el llamador. */
#include "sim_pipeline.h"
#include "sim_metricas.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>

/* No acepta números truncados, negativos ni fuera de uint32 para PCs. */
static int numero(const char *s,uint64_t *n)
{
    if(!s || !*s || *s=='-')return 0;
    errno=0;char *fin;unsigned long long v=strtoull(s,&fin,10);
    if(errno || *fin)return 0;
    *n=(uint64_t)v;return 1;
}
static FILE *abrir(const char *dir,const char *nombre)
{
    size_t n=strlen(dir)+strlen(nombre)+2;char *ruta=malloc(n);if(!ruta)return NULL;
    snprintf(ruta,n,"%s/%s",dir,nombre);FILE *f=fopen(ruta,"w");free(ruta);return f;
}
/* Error común: todos los recursos son locales, incluidos traza y reportes. */
int main(int argc,char **argv)
{
    uint64_t fin,inicio,limite,presupuesto;
    if(argc!=7 || !numero(argv[2],&fin) || !numero(argv[3],&inicio) || !numero(argv[4],&limite) || !numero(argv[6],&presupuesto) || fin>16368 || inicio>=limite || limite>16384 || !presupuesto || presupuesto>1000000)return 2;
    SimImagen *im=NULL;SimPipeline *p=NULL;SimMetricas m={0};FILE *traza=NULL,*f=NULL;char *json=NULL;int rc=1;
    if(sim_cargar_mem(argv[1],&im,stderr)!=SIM_CARGA_OK)goto salir;
    p=calloc(1,sizeof *p);if(!p || sim_pipeline_iniciar(p,im)!=SIM_PIPE_ACTIVO || sim_pipeline_configurar_busqueda(p,SIM_BUSQUEDA_FISICA)!=SIM_PIPE_ACTIVO)goto salir;
    for(unsigned r=16;r<32;++r)p->registros[r]=UINT32_C(0x4a560000)+r;
    if(sim_metricas_iniciar(&m,p)!=SIM_METRICAS_OK)goto salir;
    traza=abrir(argv[5],"transferencias.csv");if(!traza)goto salir;
    fprintf(traza,"ciclo,pc_ex,operacion,tomado,destino,sp,ra\n");
    uint64_t retorno=0,corte=0;
    for(uint64_t i=0;i<presupuesto;++i) {
        if(sim_avanzar_ciclo(p)!=SIM_PIPE_ACTIVO || sim_metricas_observar(&m,p)!=SIM_METRICAS_OK)goto salir;
        const SimCiclo *c=&p->ultimo_ciclo;
        if(c->bru_ex_valido) {
            SimInstruccion d;
            if(sim_decode_palabra(im->imem[(c->ex.pc>>4)&1023u][2],2,&d)!=SIM_DECODE_OK)goto salir;
            fprintf(traza,"%"PRIu64",%u,%d,%d,%u,%u,%u\n",p->ciclos,c->ex.pc,(int)d.operacion,c->ex_bru.tomado,c->ex_bru.destino,p->registros[2],p->registros[1]);
            if(!retorno && c->ex.pc>=inicio && c->ex.pc<limite && d.operacion==SIM_OP_SYER && d.rs1==1 && d.rd==0 && c->ex_bru.tomado && c->ex_bru.destino==fin) {
                if(p->registros[2]!=65536 || p->registros[3]!=16384 || p->registros[1]!=fin)goto salir;
                retorno=p->ciclos;corte=retorno+8;
            }
        }
        if(retorno && p->ciclos==corte)break;
    }
    if(!retorno || p->ciclos!=corte || p->trap_count) {fprintf(stderr,"sin retorno acreditado de main dentro del presupuesto\n");goto salir;}
    for(unsigned r=16;r<32;++r)if(p->registros[r]!=UINT32_C(0x4a560000)+r){fprintf(stderr,"registro preservado x%u destruido\n",r);goto salir;}
    f=abrir(argv[5],"sim-registros.json");if(!f)goto salir;
    fputs("[",f);for(unsigned r=0;r<32;++r)fprintf(f,"%s%u",r?",":"",p->registros[r]);fputs("]\n",f);if(fclose(f)){f=NULL;goto salir;}f=NULL;
    f=abrir(argv[5],"sim-memoria.bin");if(!f || fwrite(p->memoria.bytes,1,65536,f)!=65536)goto salir;if(fclose(f)){f=NULL;goto salir;}f=NULL;
    if(sim_metricas_exportar(&m,SIM_METRICAS_JSON,&json)!=SIM_METRICAS_OK)goto salir;
    f=abrir(argv[5],"sim-metricas.json");if(!f || fputs(json,f)<0)goto salir;if(fclose(f)){f=NULL;goto salir;}f=NULL;
    f=abrir(argv[5],"retorno.json");if(!f)goto salir;
    fprintf(f,"{\"retorno_main\":true,\"ciclo_retorno\":%"PRIu64",\"corte\":%"PRIu64",\"pc_fin\":%"PRIu64",\"estado_arquitectonico\":\"ACTIVO\",\"B1_equivalente\":false}\n",retorno,corte,fin);
    if(fclose(f)){f=NULL;goto salir;}f=NULL;rc=0;
salir:
    if(f)fclose(f);
    if(traza && fclose(traza))rc=1;
    sim_metricas_liberar_reporte(&json);sim_metricas_liberar(&m);free(p);sim_liberar_imagen(&im);return rc;
}
