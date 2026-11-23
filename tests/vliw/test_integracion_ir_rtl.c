/* Contrasta el productor IR/spilling real con la cadena física y M2/M1.
 * Recibe nombres de escenarios y directorios externos. Publica IR, código y
 * observables propios; no traduce IR ni modifica fuentes del equipo. Los
 * resultados aritméticos se fijan al preparar cada caso, fuera del scheduler.
 * El arnés añade SP y un punto de observación; no acredita retorno de main. */
#include "ir_vliw.h"
#include "sim_ejecucion.h"
#include "dce.h"
#include "reordenamiento.h"
#include "loop_unrolling.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#define REQ(x) do { if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);} } while(0)
#ifdef I3_FALLOS
static bool interceptar;static size_t reservas,fallar;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
/* Las reservas del fixture y los oráculos quedan fuera de la inyección;
 * únicamente se falla la construcción pública del productor y consumidores. */
void *__wrap_malloc(size_t n){if(interceptar && ++reservas==fallar)return NULL;return __real_malloc(n);}
/* calloc se usa también al adquirir hijos: el rechazo no debe publicar padres. */
void *__wrap_calloc(size_t n,size_t s){if(interceptar && ++reservas==fallar)return NULL;return __real_calloc(n,s);}
/* realloc fallido conserva el bloque original para la liberación heredada. */
void *__wrap_realloc(void *p,size_t n){if(interceptar && ++reservas==fallar)return NULL;return __real_realloc(p,n);}
#endif
/* Duplica los operandos del fixture; IrPrograma adquiere su memoria y se
 * destruye con ir_liberar. Fallar al preparar el oráculo es un fallo de prueba. */
static char *copiar(const char *s){if(!s)return NULL;size_t n=strlen(s)+1;char *p=malloc(n);REQ(p);memcpy(p,s,n);return p;}
/* La IR se prepara con la representación pública, sin máscaras/aristas físicas.
 * Cada cadena pertenece a p. Las líneas sirven para identificar rechazos. */
static void poner(IrPrograma *p,IrOp op,const char *dst,const char *a,const char *b,const char *operador)
{
    if(p->cantidad==p->capacidad){size_t n=p->capacidad?p->capacidad*2:32;IrInstr *v=realloc(p->codigo,n*sizeof *v);REQ(v);p->codigo=v;p->capacidad=n;}
    size_t i=p->cantidad++;p->codigo[i]=(IrInstr){op,copiar(dst),copiar(a),copiar(b),copiar(operador),i+1};
}
/* Los valores esperados corresponden a ranuras lógicas de temporales definidos
 * en orden. No se deducen de los registros elegidos por el asignador. Se omiten
 * ranuras no observables o de ramas no ejecutadas con la máscara comprobar. */
typedef struct {uint32_t valor[16];unsigned comprobar;bool presion;} Oraculo;
static Oraculo preparar(const char *nombre,IrPrograma *p)
{
    Oraculo o={{0},0,false};poner(p,IR_FUNC,"main",NULL,NULL,NULL);
    if(!strcmp(nombre,"presion")){
        for(unsigned i=0;i<7;++i){char t[20],v[20];(void)snprintf(t,sizeof t,"$t%u",i);(void)snprintf(v,sizeof v,"%u",i+1);poner(p,IR_COPY,t,v,NULL,NULL);o.valor[i]=i+1;}
        poner(p,IR_BIN,"$t7","$t0","$t6","gauss");poner(p,IR_BIN,"$t8","$t1","$t5","gauss");
        poner(p,IR_BIN,"$t9","$t2","$t4","gauss");poner(p,IR_BIN,"$t10","$t7","$t8","gauss");poner(p,IR_BIN,"$t11","$t10","$t9","gauss");
        o.valor[7]=o.valor[8]=o.valor[9]=8;o.valor[10]=16;o.valor[11]=24;o.comprobar=4095;o.presion=true;
    }else if(!strcmp(nombre,"backedge")){
        poner(p,IR_COPY,"$t0","3",NULL,NULL);poner(p,IR_LABEL,"$L0",NULL,NULL,NULL);
        poner(p,IR_BIN,"$t0","$t0","1","neumann");poner(p,IR_JTRUE,"$L0","$t0",NULL,NULL);o.comprobar=1;
    }else if(!strcmp(nombre,"tomado") || !strcmp(nombre,"no_tomado")){
        bool tomado=!strcmp(nombre,"tomado");poner(p,IR_COPY,"$t0",tomado?"0":"1",NULL,NULL);
        poner(p,IR_JFALSE,"$L1","$t0",NULL,NULL);poner(p,IR_COPY,"$t1","99",NULL,NULL);poner(p,IR_JUMP,"$L2",NULL,NULL,NULL);
        poner(p,IR_LABEL,"$L1",NULL,NULL,NULL);poner(p,IR_COPY,"$t1","42",NULL,NULL);poner(p,IR_LABEL,"$L2",NULL,NULL,NULL);
        o.valor[0]=tomado?0:1;o.valor[1]=tomado?42:99;o.comprobar=3;
    }else if(!strcmp(nombre,"directo")){
        poner(p,IR_COPY,"$t0","7",NULL,NULL);poner(p,IR_JUMP,"$L0",NULL,NULL,NULL);poner(p,IR_COPY,"$t0","99",NULL,NULL);poner(p,IR_LABEL,"$L0",NULL,NULL,NULL);o.valor[0]=7;o.comprobar=1;
    }else if(!strcmp(nombre,"dce")){
        poner(p,IR_COPY,"$t0","7",NULL,NULL);poner(p,IR_COPY,"$t1","100",NULL,NULL);
        poner(p,IR_LABEL,"$L0",NULL,NULL,NULL);poner(p,IR_JFALSE,"$L0","$t0",NULL,NULL);o.valor[0]=7;
        /* Este fragmento puro no publica un resultado BAL: DCE puede quitar
         * sus temporales y plegar control. Su contrato observable es restaurar
         * SP/RA sin traps; no se exige conservar ranuras internas eliminadas. */
        o.comprobar=0;
    }else if(!strcmp(nombre,"copia") || !strcmp(nombre,"minimo") || !strcmp(nombre,"maximo")){
        const char *v=!strcmp(nombre,"minimo")?"-1024":!strcmp(nombre,"maximo")?"1023":"7";
        poner(p,IR_COPY,"$t0",v,NULL,NULL);o.valor[0]=(uint32_t)strtol(v,NULL,10);o.comprobar=1;
    }else if(!strcmp(nombre,"negacion")){
        poner(p,IR_UNARY,"$t0","7",NULL,"neumann");o.valor[0]=(uint32_t)-7;o.comprobar=1;
    }else if(!strcmp(nombre,"suma") || !strcmp(nombre,"resta") || !strcmp(nombre,"negativos")){
        bool resta=!strcmp(nombre,"resta"),neg=!strcmp(nombre,"negativos");poner(p,IR_BIN,"$t0",neg?"-900":"7",neg?"-100":"5",resta?"neumann":"gauss");
        o.valor[0]=neg?(uint32_t)-1000:resta?2:12;o.comprobar=1;
    }else if(!strcmp(nombre,"raw") || !strcmp(nombre,"war") || !strcmp(nombre,"waw") || !strcmp(nombre,"independientes")){
        poner(p,IR_COPY,"$t0","8",NULL,NULL);o.valor[0]=8;o.comprobar=1;
        if(!strcmp(nombre,"waw")){poner(p,IR_COPY,"$t0","12",NULL,NULL);o.valor[0]=12;}
        else {bool indep=!strcmp(nombre,"independientes");poner(p,indep?IR_COPY:IR_BIN,"$t1",indep?"4":"$t0",indep?NULL:"1",indep?NULL:"gauss");o.valor[1]=indep?4:9;o.comprobar=3;
            if(!strcmp(nombre,"war")){poner(p,IR_COPY,"$t0","2",NULL,NULL);o.valor[0]=2;}}
    }else REQ(false);
    poner(p,IR_END_FUNC,"main",NULL,NULL,NULL);REQ(ir_bloques_basicos(p)==0);return o;
}
/* Captura por texto de IR/CFG públicos: no compara representaciones con padding.
 * El llamador posee la cadena y debe liberarla. Solo opera fuera de inyección. */
static char *capturar_ir(const IrPrograma *p)
{
    FILE *f=tmpfile();REQ(f);REQ(ir_imprimir(p,f)==0);REQ(fflush(f)==0);long n=ftell(f);REQ(n>=0);rewind(f);char *s=calloc((size_t)n+1,1);REQ(s);REQ(fread(s,1,(size_t)n,f)==(size_t)n);REQ(fclose(f)==0);return s;
}
/* Abre exclusivamente artefactos de la ejecución solicitada por el cliente.
 * El orquestador exige que dir sea externo y nuevo; FILE pertenece al llamador. */
static FILE *archivo(const char *dir,const char *nombre)
{char ruta[4096];int n=snprintf(ruta,sizeof ruta,"%s/%s",dir,nombre);REQ(n>0 && (size_t)n<sizeof ruta);FILE *f=fopen(ruta,"wb");REQ(f);return f;}
/* Las pasadas son las reales, opt-in y sobre IR del fixture. No son opciones
 * oficiales -O del compilador. Se registran cantidades y procedencia disponible. */
static void transformar(IrPrograma *p,const char *perfil,size_t *cambios)
{
    *cambios=0;
    if(!strcmp(perfil,"dce")){DceResultado d={0};REQ(dce_eliminar(p,&d)==0);*cambios=d.cantidad_eliminadas;dce_liberar_resultado(&d);}
    else if(!strcmp(perfil,"reorder")){RoResumen r={0};REQ(ro_reordenar(p,NULL,&r)==0);*cambios=r.movidas;ro_resumen_liberar(&r);}
    else if(!strcmp(perfil,"unroll")){LuResumen r={0};REQ(lu_desenrollar(p,NULL,&r,NULL)==0);*cambios=r.totales+r.parciales;lu_resumen_liberar(&r);}
    else REQ(!strcmp(perfil,"base"));
}
/* Crea un arnés ISA separado sin modificar ningún byte del fragmento. Tres
 * bundles iniciales garantizan SP disponible N+3 para el prólogo. Todos los
 * saltos del subconjunto son relativos internos: trasladarlos 48 bytes conserva
 * sus destinos. El epílogo cae en NOP de drenado y un SYE estable, sin HALT. */
static void exportar(const char *nombre,const char *dir,const char *perfil)
{
    IrPrograma p={0};Oraculo o=preparar(nombre,&p);FILE *f=archivo(dir,"original.ir");REQ(ir_imprimir(&p,f)==0);REQ(fclose(f)==0);
    size_t cambios;transformar(&p,perfil,&cambios);char *antes=capturar_ir(&p);
    IrVliwConfiguracion cfg={3,16};IrVliwResultado r={0};IrVliwEstado estado=ir_vliw_desde_ir(&p,&cfg,&r,stderr);
    char *despues=capturar_ir(&p);REQ(!strcmp(antes,despues));free(antes);free(despues);REQ(estado==IR_VLIW_OK);
    const VliwCadena *c=r.cadena;REQ(vliw_verificar(&c->proyeccion->entrada,c->plan,NULL)==VLIW_OK);
    if(o.presion)REQ(r.metricas.desalojos && r.metricas.cargas && r.metricas.almacenes);
    f=archivo(dir,"transformada.ir");REQ(ir_imprimir(&p,f)==0);REQ(fclose(f)==0);
    f=archivo(dir,"spilling.ir");REQ(ir_imprimir(&r.despues_spilling,f)==0);REQ(fclose(f)==0);
    f=archivo(dir,"fragmento.asm");REQ(fputs(c->asm_texto,f)>=0);REQ(fclose(f)==0);
    f=archivo(dir,"fragmento.mem");REQ(cb_escribir_mem(&c->codigo,f)==0);REQ(fclose(f)==0);
    f=archivo(dir,"fragmento.bin");REQ(cb_escribir_bin(&c->codigo,f)==0);REQ(fclose(f)==0);
    f=archivo(dir,"fisica.json");REQ(fputs(c->visualizacion_json,f)>=0);REQ(fclose(f)==0);
    for(size_t i=0;i<c->codigo.cantidad;++i)for(unsigned s=0;s<4;++s){SimInstruccion d;
        SimDecodeEstado e=sim_decode_palabra(c->codigo.bundles[i].slots[s],s,&d);REQ(e==SIM_DECODE_OK || e==SIM_DECODE_NOP);
        if(e==SIM_DECODE_OK && s==2){REQ(d.salto==SIM_SALTO_PC || d.salto==SIM_SALTO_COND_PC);int64_t destino=(int64_t)i*16+d.inmediato;REQ(destino>=0 && destino<(int64_t)c->codigo.cantidad*16);}}
    f=archivo(dir,"programa.asm");REQ(fputs("sumai x2,x0,1000 | nop | nop | nop\nnop\nnop\n",f)>=0);REQ(fputs(c->asm_texto,f)>=0);REQ(fputs("nop\nnop\nnop | nop | sye x0,0 | nop\n",f)>=0);REQ(fclose(f)==0);
    f=archivo(dir,"oraculo.json");fprintf(f,"{\"caso\":\"%s\",\"perfil\":\"%s\",\"transformaciones\":%zu,\"fragmento_inicio\":48,\"fragmento_bytes\":%zu,\"sp\":1000,\"marco\":%zu,\"ranuras\":%zu,\"cargas\":%zu,\"stores\":%zu,\"desalojos\":%zu,\"memoria\":{",nombre,perfil,cambios,c->codigo.cantidad*16,r.backend.marco.bytes_marco,r.metricas.ranuras,r.metricas.cargas,r.metricas.almacenes,r.metricas.desalojos);
    bool primero=true;for(unsigned i=0;i<16;++i)if(o.comprobar&(1u<<i)){size_t addr=1000-r.backend.marco.bytes_marco+r.backend.marco.inicio_spilling+4*i;fprintf(f,"%s\"%zu\":%"PRIu32,primero?"":",",addr,o.valor[i]);primero=false;}fputs("}}\n",f);REQ(fclose(f)==0);
    ir_vliw_liberar(&r);ir_liberar(&p);printf("PASS productor %s perfil=%s\n",nombre,perfil);
}
/* M2 conserva reloj/estado y M1 reales; el presupuesto observa el bucle estable.
 * Se exportan los 32 GPR y 65536 bytes sin alterar ni terminar el pipeline. */
static void simular(const char *ruta,const char *dir,uint64_t presupuesto)
{
    SimEjecucion *s=NULL;REQ(sim_ejecucion_crear(ruta,SIM_EJECUCION_MEM,SIM_BUSQUEDA_FISICA,&s,stderr)==SIM_EJECUCION_OK);
    REQ(sim_ejecucion_ejecutar(s,presupuesto)==SIM_EJECUCION_OK);SimMetricasResumen r;REQ(sim_ejecucion_resultados(s,&r)==SIM_METRICAS_OK);REQ(r.traps==0);
    uint8_t *m=malloc(65536);REQ(m);REQ(sim_ejecucion_memoria(s,m,65536)==SIM_EJECUCION_OK);
    FILE *f=archivo(dir,"sim-memoria.bin");REQ(fwrite(m,1,65536,f)==65536);REQ(fclose(f)==0);free(m);
    f=archivo(dir,"sim-registros.json");fputc('[',f);for(unsigned i=0;i<32;++i)fprintf(f,"%s%"PRIu32,i?",":"",r.registros[i]);fputs("]\n",f);REQ(fclose(f)==0);
    char *json=NULL;REQ(sim_ejecucion_exportar(s,SIM_METRICAS_JSON,&json)==SIM_METRICAS_OK);f=archivo(dir,"sim-metricas.json");REQ(fputs(json,f)>=0);REQ(fclose(f)==0);sim_metricas_liberar_reporte(&json);sim_ejecucion_liberar(&s);REQ(!s);
}
/* Inicializa s0–s15 con centinelas antes de ejecutar código tradicional.
 * Usa S1/S6/M1 públicos; esta precondición también la aplica el banco externo.
 * No atribuye preservación general al productor experimental que rechaza CALL. */
static void preservar(const char *ruta,const char *dir,uint64_t presupuesto)
{
    SimImagen *imagen=NULL;REQ(sim_cargar_mem(ruta,&imagen,stderr)==SIM_CARGA_OK);
    SimPipeline *s=calloc(1,sizeof *s);REQ(s);REQ(sim_pipeline_iniciar(s,imagen)==SIM_PIPE_ACTIVO);
    REQ(sim_pipeline_configurar_busqueda(s,SIM_BUSQUEDA_FISICA)==SIM_PIPE_ACTIVO);
    for(unsigned r=16;r<32;++r)s->registros[r]=UINT32_C(0x4a560000)+r;
    SimMetricas m={0};REQ(sim_metricas_iniciar(&m,s)==SIM_METRICAS_OK);
    for(uint64_t i=0;i<presupuesto;++i){REQ(sim_avanzar_ciclo(s)==SIM_PIPE_ACTIVO);REQ(sim_metricas_observar(&m,s)==SIM_METRICAS_OK);}
    SimMetricasResumen r;REQ(sim_metricas_resultados(&m,&r)==SIM_METRICAS_OK);REQ(r.traps==0);
    for(unsigned k=16;k<32;++k)REQ(r.registros[k]==UINT32_C(0x4a560000)+k);
    FILE *f=archivo(dir,"sim-memoria.bin");REQ(fwrite(s->memoria.bytes,1,65536,f)==65536);REQ(fclose(f)==0);
    f=archivo(dir,"sim-registros.json");fputc('[',f);for(unsigned k=0;k<32;++k)fprintf(f,"%s%"PRIu32,k?",":"",r.registros[k]);fputs("]\n",f);REQ(fclose(f)==0);
    char *json=NULL;REQ(sim_metricas_exportar(&m,SIM_METRICAS_JSON,&json)==SIM_METRICAS_OK);f=archivo(dir,"sim-metricas.json");REQ(fputs(json,f)>=0);REQ(fclose(f)==0);
    sim_metricas_liberar_reporte(&json);sim_metricas_liberar(&m);free(s);sim_liberar_imagen(&imagen);
}

/* Cada rechazo conserva la IR, el resultado inicialmente vacío y sus recursos.
 * Los escenarios mutan entrada/configuración, nunca las expectativas del ISA. */
static void negativas(void)
{
    for(unsigned k=0;k<16;++k){IrPrograma p={0};(void)preparar("copia",&p);IrVliwConfiguracion cfg={3,16};
        switch(k){case 0:p.codigo[1].op=IR_CALL;break;case 1:p.codigo[1].op=IR_RETURN;break;
        case 2:free(p.codigo[1].dst);p.codigo[1].dst=copiar("variable");break;
        case 3:free(p.codigo[1].a);p.codigo[1].a=copiar("1024");break;
        case 4:cfg.registros=2;break;case 5:cfg.registros=9;break;case 6:cfg.bytes_ocupados=SIZE_MAX;break;case 7:cfg.bytes_ocupados=1020;break;
        case 8:p.codigo[1].op=(IrOp)999;break;
        case 9:p.codigo[1].op=IR_JUMP;free(p.codigo[1].dst);p.codigo[1].dst=copiar("$ausente");break;
        case 10:p.codigo[1].op=IR_JUMP;free(p.codigo[1].dst);p.codigo[1].dst=NULL;break;
        case 11:p.codigo[1].op=IR_SPILL_LOAD;break;
        case 12:poner(&p,IR_LABEL,"$L0",NULL,NULL,NULL);poner(&p,IR_LABEL,"$L0",NULL,NULL,NULL);break;
        case 13:cfg.bytes_ocupados=3;break;case 14:cfg.bytes_ocupados=6;break;
        default:for(unsigned i=0;i<1100;++i)poner(&p,IR_COPY,"$t0","7",NULL,NULL);break;}
        int cfg_estado=ir_bloques_basicos(&p);fprintf(stderr,"escenario negativo %u: CFG=%d\n",k,cfg_estado);char *antes=capturar_ir(&p);IrVliwResultado r={0};REQ(ir_vliw_desde_ir(&p,&cfg,&r,NULL)!=IR_VLIW_OK);REQ(!r.cadena && !r.despues_spilling.codigo && !r.despues_spilling.bloques);
        char *despues=capturar_ir(&p);REQ(!strcmp(antes,despues));free(antes);free(despues);ir_vliw_liberar(&r);ir_liberar(&p);
    }
    IrPrograma p={0};(void)preparar("copia",&p);IrVliwConfiguracion cfg={3,16};IrVliwResultado r={0};r.despues_spilling.cantidad=1;
    REQ(ir_vliw_desde_ir(&p,&cfg,&r,NULL)==IR_VLIW_ARGUMENTO && r.despues_spilling.cantidad==1);r.despues_spilling.cantidad=0;ir_vliw_liberar(&r);ir_liberar(&p);
    puts("PASS negativas: 17 escenarios");
}
/* Falla puntos distribuidos entre primera, intermedias y última reserva real.
 * Cada rechazo exige no publicación, IR inmutable y recuperación. No se afirma
 * haber enumerado todas las combinaciones de fallos de consumidores ajenos. */
static void recursos(void)
{
#ifdef I3_FALLOS
    IrPrograma p={0};(void)preparar("raw",&p);IrVliwConfiguracion cfg={3,16};IrVliwResultado r={0};char *antes=capturar_ir(&p);
    reservas=0;fallar=SIZE_MAX;interceptar=true;REQ(ir_vliw_desde_ir(&p,&cfg,&r,NULL)==IR_VLIW_OK);interceptar=false;size_t total=reservas;ir_vliw_liberar(&r);
    for(unsigned i=0;i<16;++i){fallar=1+(total-1)*i/15;reservas=0;interceptar=true;IrVliwEstado e=ir_vliw_desde_ir(&p,&cfg,&r,NULL);interceptar=false;
        REQ(e!=IR_VLIW_OK && !r.cadena && !r.despues_spilling.codigo);char *despues=capturar_ir(&p);REQ(!strcmp(antes,despues));free(despues);
        REQ(ir_vliw_desde_ir(&p,&cfg,&r,NULL)==IR_VLIW_OK);ir_vliw_liberar(&r);}
    free(antes);ir_liberar(&p);printf("PASS recursos: 16 fallos y 16 recuperaciones; reservas=%zu\n",total);
#else
    REQ(false);
#endif
}
/* Despacho explícito: nombres/argumentos inválidos no se aceptan como pruebas.
 * Los directorios y presupuestos son responsabilidades del orquestador externo. */
int main(int argc,char **argv)
{
    if(argc==5 && !strcmp(argv[1],"generar")){exportar(argv[2],argv[3],argv[4]);return 0;}
    if(argc==5 && !strcmp(argv[1],"sim")){simular(argv[2],argv[3],strtoull(argv[4],NULL,10));return 0;}
    if(argc==5 && !strcmp(argv[1],"preservar")){preservar(argv[2],argv[3],strtoull(argv[4],NULL,10));return 0;}
    if(argc==2 && !strcmp(argv[1],"negativas")){negativas();return 0;}
    if(argc==2 && !strcmp(argv[1],"recursos")){recursos();return 0;}
    return 64;
}
