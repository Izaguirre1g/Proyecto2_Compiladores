/* Pruebas BRU y control de flujo; sonda de vectores y trazas para contrastar
 * flancos completos con el procesador RTL sin modificar Arquitectura. */
#include "sim_pipeline.h"
#include <stdlib.h>
#include <string.h>

static size_t comprobaciones;
#define C(c) do { ++comprobaciones; if (!(c)) { \
    fprintf(stderr,"FAIL BRU línea %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static const SimOperacion ops[]={SIM_OP_IGUALSI,SIM_OP_IGUALNO,SIM_OP_MENORA,
    SIM_OP_MAYOROIGUAL,SIM_OP_SYE,SIM_OP_SYER};

static void condiciones_y_destinos(void)
{
    /* Las expectativas signed son literales: ordenan INT_MIN, -1, cero y
     * máximos positivos. Cada par prueba las cuatro condiciones completas. */
    const struct { uint32_t a,b; bool eq,lt; } pares[]={
        {0,0,true,false},{1,2,false,true},{2,1,false,false},
        {UINT32_MAX,0,false,true},{0,UINT32_MAX,false,false},
        {0x80000000u,0x7fffffffu,false,true},{0x7fffffffu,0x80000000u,false,false},
        {0x80000000u,UINT32_MAX,false,true},{UINT32_MAX,0x80000000u,false,false},
        {UINT32_MAX,UINT32_MAX,true,false},{0x80000000u,0x80000000u,true,false}
    };
    for(size_t n=0;n<sizeof pares/sizeof *pares;++n) {
        const bool decisiones[]={pares[n].eq,!pares[n].eq,pares[n].lt,!pares[n].lt};
        for(unsigned i=0;i<4;++i) {
            SimResultadoBru r;
            C(sim_bru_ejecutar(ops[i],64,pares[n].a,pares[n].b,-16,&r));
            C(r.tomado==decisiones[i] && !r.enlace);
            C(r.destino==(decisiones[i]?48u:80u) && r.valor_enlace==80);
        }
    }
    SimResultadoBru r;
    C(sim_bru_ejecutar(SIM_OP_SYE,0xfffffff0u,0,0,32,&r));
    C(r.tomado && r.enlace && r.destino==16 && r.valor_enlace==0);
    C(sim_bru_ejecutar(SIM_OP_SYE,16,0,0,-32,&r) && r.destino==0xfffffff0u);
    C(sim_bru_ejecutar(SIM_OP_SYE,0,0,0,-32768,&r) && r.destino==0xffff8000u);
    C(sim_bru_ejecutar(SIM_OP_SYE,0,0,0,32767,&r) && r.destino==32767);
    C(sim_bru_ejecutar(SIM_OP_SYER,128,0x43,0,0,&r) && r.destino==0x40 && r.valor_enlace==144);
    C(sim_bru_ejecutar(SIM_OP_SYER,0,0x43,0,-4,&r) && r.destino==0x30);
    C(sim_bru_ejecutar(SIM_OP_SYER,0,UINT32_MAX,0,1,&r) && r.destino==0);
    C(sim_bru_ejecutar(SIM_OP_IGUALSI,0,0,0,-1024,&r) && r.destino==0xfffffc00u);
    C(sim_bru_ejecutar(SIM_OP_IGUALSI,0,0,0,1023,&r) && r.destino==1023);
    /* BRU no impone alineación estática: S2 la valida en instrucciones reales.
     * syer sí elimina dinámicamente los cuatro bits bajos de su suma. */
    C(sim_bru_ejecutar(SIM_OP_IGUALSI,3,0,0,1,&r) && r.destino==4);
    SimResultadoBru anterior=r;
    C(!sim_bru_ejecutar(SIM_OP_NOP,0,0,0,0,&r));
    C(!sim_bru_ejecutar(SIM_OP_SUMA,0,0,0,0,&r));
    C(!sim_bru_ejecutar(SIM_OP_SYE,0,0,0,32768,&r));
    C(!sim_bru_ejecutar(SIM_OP_SYE,0,0,0,-32769,&r));
    C(!sim_bru_ejecutar(SIM_OP_SYER,0,0,0,1024,&r));
    C(!sim_bru_ejecutar(SIM_OP_IGUALSI,0,0,0,-1025,&r));
    C(!sim_bru_ejecutar(SIM_OP_SYE,0,0,0,0,NULL));
    C(r.destino==anterior.destino && r.tomado==anterior.tomado && r.enlace==anterior.enlace);
}

/* Estímulos binarios mínimos ya definidos por la ISA. Los programas RTL de
 * Python se codifican mediante el ensamblador existente, no estas funciones. */
static uint32_t salto(unsigned rd,int off)
{
    return 0x96000000u|(rd<<16)|((uint32_t)off&65535u);
}
static uint32_t sumai(unsigned rd,int v)
{
    return 0x80000000u|(rd<<11)|((uint32_t)v&2047u);
}

static void flush_bucles_y_politicas(void)
{
    SimImagen imagen={.cantidad_bundles=4};
    imagen.imem[0][0]=sumai(5,7); imagen.imem[0][2]=salto(31,48);
    imagen.imem[1][3]=0x04c00000u; /* ID criptográfico reservado en ID, pero anulado. */
    imagen.imem[2][0]=1; /* Reservada en IF; tampoco debe aceptarse. */
    imagen.imem[3][0]=sumai(6,9);
    SimPipeline s;
    C(sim_pipeline_iniciar(&s,&imagen)==SIM_PIPE_ACTIVO);
    C(sim_pipeline_ejecutar(&s,3)==SIM_PIPE_LIMITE);
    C(s.pc==48 && s.ultimo_ciclo.flush && s.ultimo_ciclo.if_descartado && s.ultimo_ciclo.id_descartado);
    C(!s.if_id.valido && !s.id_ex.valido && s.ex_wb.valido && s.ex_wb.bru.valida);
    C(s.registros[5]==0 && s.registros[31]==0 && s.bundles_admitidos==2 && s.bundles_anulados==1);
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_ACTIVO);
    C(s.registros[5]==7 && s.registros[31]==16 && s.ultima_escritura.destino==31);
    C(sim_pipeline_ejecutar(&s,20)==SIM_PIPE_TERMINADO);
    C(s.registros[6]==9 && s.ciclos==7 && s.bundles_completados==2);
    /* El flujo no tomado no oculta errores del camino que realmente avanza. */
    imagen.imem[0][2]=0x82400030u; /* igualno x0,x0,+48: falso. */
    sim_pipeline_iniciar(&s,&imagen);
    C(sim_pipeline_ejecutar(&s,20)==SIM_PIPE_DECODIFICACION && s.ciclos==2);
    /* Un salto al propio PC no termina por fin_busqueda. El mismo estado debe
     * avanzar de forma idéntica con presupuesto único o varias reanudaciones. */
    imagen=(SimImagen){.cantidad_bundles=1}; imagen.imem[0][2]=salto(31,0);
    SimPipeline otro;
    sim_pipeline_iniciar(&s,&imagen); sim_pipeline_iniciar(&otro,&imagen);
    C(sim_pipeline_ejecutar(&s,3)==SIM_PIPE_LIMITE && s.pc==0 && !s.fin_busqueda);
    C(sim_pipeline_ejecutar(&s,17)==SIM_PIPE_LIMITE && s.ciclos==20);
    C(sim_pipeline_ejecutar(&otro,20)==SIM_PIPE_LIMITE);
    C(s.pc==otro.pc && s.bundles_completados==otro.bundles_completados &&
      memcmp(s.registros,otro.registros,sizeof s.registros)==0);
    C(s.bundles_completados>1 && s.registros[31]==16);
    /* Tras un paso ya no se puede cambiar de política inadvertidamente. */
    C(sim_pipeline_configurar_busqueda(&s,SIM_BUSQUEDA_FISICA)==SIM_PIPE_ARGUMENTO);
    C(sim_pipeline_reiniciar(&s)==SIM_PIPE_ACTIVO && s.ciclos==0 && !s.ex_wb.bru.valida);
    C(s.registros[31]==0);
    C(sim_pipeline_configurar_busqueda(&s,SIM_BUSQUEDA_FISICA)==SIM_PIPE_ACTIVO);
    sim_pipeline_reiniciar(&s); C(s.modo_busqueda==SIM_BUSQUEDA_FISICA);
    C(sim_pipeline_configurar_busqueda(&s,(SimModoBusqueda)7)==SIM_PIPE_ARGUMENTO);
    C(sim_pipeline_configurar_busqueda(NULL,SIM_BUSQUEDA_FINITA)==SIM_PIPE_ARGUMENTO);
    /* Un IF inválido que coincide con el redirect no debe impedirlo. */
    sim_pipeline_iniciar(&s,&imagen); sim_pipeline_ejecutar(&s,2); s.pc=3;
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_ACTIVO && s.pc==0);
    /* Destino exactamente al final: vacía y retira el enlace. Un destino mayor
     * se publica y se detiene administrativamente al intentar admitir su IF. */
    imagen.imem[0][2]=salto(31,16); sim_pipeline_iniciar(&s,&imagen);
    C(sim_pipeline_ejecutar(&s,20)==SIM_PIPE_TERMINADO && s.registros[31]==16);
    imagen.imem[0][2]=salto(31,32); sim_pipeline_iniciar(&s,&imagen);
    C(sim_pipeline_ejecutar(&s,3)==SIM_PIPE_LIMITE && s.pc==32);
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_PC && s.ciclos==3 && s.ex_wb.bru.valida);
    /* En modo físico hay NOP en la región no cargada. El PC alto usa los bits
     * 13:4 como índice: 0x4000 vuelve a B0. No se inventan excepciones de CPU. */
    sim_pipeline_iniciar(&s,&imagen); sim_pipeline_configurar_busqueda(&s,SIM_BUSQUEDA_FISICA);
    C(sim_pipeline_ejecutar(&s,12)==SIM_PIPE_LIMITE && s.registros[31]==16);
    sim_pipeline_reiniciar(&s); s.pc=0x4000;
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_ACTIVO && s.if_id.palabras[2]==imagen.imem[0][2]);
    s.pc=0xfffffff0u; sim_avanzar_ciclo(&s); C(s.pc==0);
    /* Una instrucción directa desalineada se rechaza por S2, no por BRU pura. */
    imagen.imem[0][2]=salto(0,1); sim_pipeline_iniciar(&s,&imagen);
    C(sim_pipeline_ejecutar(&s,5)==SIM_PIPE_DECODIFICACION && s.ciclos==1);
}

static void registros(const SimPipeline *s)
{
    printf("\"regs\":[");
    for(unsigned r=0;r<32;++r) printf("%s%u",r?",":"",s->registros[r]);
    printf("]}\n");
}

static void traza(const SimPipeline *s)
{
    const SimCiclo *c=&s->ultimo_ciclo;
    const SimAccesoLsu *t=&c->ex_lsu;
    uint32_t post=0; sim_memoria_leer_palabra(&s->memoria,t->direccion,&post);
    printf("{\"c\":%llu,\"pc\":%u,\"stages\":[%u,%u,%u,%u],\"pcs\":[%u,%u,%u,%u],",
        (unsigned long long)c->ciclo,s->pc,c->if_.valido,c->id.valido,c->ex.valido,c->wb.valido,
        c->if_.pc,c->id.pc,c->ex.pc,c->wb.pc);
    printf("\"idalu\":[%u,%u],\"idlsu\":[%u,%u,%u,%d],\"idbru\":[%u,%u,%u,%d],",
        c->id_a,c->id_b,c->id_lsu.valido,c->id_lsu.base,c->id_lsu.dato,c->id_lsu.offset,
        c->id_bru.valido,c->id_bru.a,c->id_bru.b,c->id_bru.offset);
    printf("\"exalu\":[%u,%u,%u],\"x\":[%u,%u,%u,%u,%u,%u,%u,%u,%u],\"postmem\":%u,",
        c->ex_a,c->ex_b,c->ex_resultado,t->valido,t->direccion,t->palabra_fisica,
        t->byte_enable,t->dato_escritura,t->lectura,t->resultado,t->escritura,t->carga,post);
    printf("\"w0\":[%u,%u,%u],\"w1\":[%u,%u,%u],\"w2\":[%u,%u,%u],\"w1_valida\":%u,\"w2_valida\":%u,",
        c->wb_escribe,c->wb_destino,c->wb_valor,c->lsu_wb_escribe,c->wb_lsu.destino,c->wb_lsu.valor,
        c->bru_wb_escribe,c->wb_bru.destino,c->wb_bru.valor,c->wb_lsu.valida,c->wb_bru.valida);
    printf("\"conflicto\":%u,\"flush\":%u,\"target\":%u,\"kill\":[%u,%u],\"pending\":[%u,%u,%u],\"link\":[%u,%u,%u],",
        c->conflicto_wb,c->flush,c->ex_bru.destino,c->if_descartado,c->id_descartado,
        s->ex_wb.lsu.valida,s->ex_wb.lsu.destino,s->ex_wb.lsu.valor,
        s->ex_wb.bru.valida,s->ex_wb.bru.destino,s->ex_wb.bru.valor);
    printf("\"next\":[%u,%u,%u],\"next_pc\":[%u,%u,%u],\"cuentas\":[%llu,%llu,%llu],",
        s->if_id.valido,s->id_ex.valido,s->ex_wb.valido,s->if_id.pc,s->id_ex.pc,s->ex_wb.pc,
        (unsigned long long)s->bundles_admitidos,(unsigned long long)s->bundles_completados,
        (unsigned long long)s->bundles_anulados);
    registros(s);
}

static int vectores(const char *ruta)
{
    FILE *f=fopen(ruta,"r"); C(f!=NULL);
    unsigned i,op,pc,a,b,rd,reset; int off,n;
    while((n=fscanf(f,"%u %u %x %x %x %d %u %u",&i,&op,&pc,&a,&b,&off,&rd,&reset))==8) {
        C(i<6 && rd<32);
        SimResultadoBru r; C(sim_bru_ejecutar(ops[i],pc,a,b,off,&r));
        printf("B %u %08x %u %u %08x %08x\n",r.tomado,r.destino,!reset&&r.enlace,
            reset?0:rd,reset?0:r.valor_enlace,reset?0:r.tomado?r.destino:16u);
    }
    C(n==EOF && !ferror(f)); C(fclose(f)==0); return 0;
}

int main(int argc,char **argv)
{
    if(argc==1) {
        condiciones_y_destinos(); flush_bucles_y_politicas();
        printf("OK: %zu comprobaciones BRU/pipeline\n",comprobaciones); return 0;
    }
    if(argc==3 && strcmp(argv[1],"vectores")==0) return vectores(argv[2]);
    if(argc!=6 && argc!=7) return 2;
    SimImagen *imagen=NULL;
    SimCargaEstado e=strcmp(argv[1],"mem")==0?sim_cargar_mem(argv[2],&imagen,stderr):sim_cargar_balb(argv[2],&imagen,stderr);
    if(e!=SIM_CARGA_OK) return 1;
    if(argc==7) {
        FILE *f=fopen(argv[6],"rb"); C(f!=NULL);
        C(fread(imagen->dmem,1,sizeof imagen->dmem,f)==sizeof imagen->dmem); C(fclose(f)==0);
    }
    uint8_t original[65536]; memcpy(original,imagen->dmem,sizeof original);
    SimPipeline s; sim_pipeline_iniciar(&s,imagen);
    if(strcmp(argv[5],"fisica")==0) sim_pipeline_configurar_busqueda(&s,SIM_BUSQUEDA_FISICA);
    unsigned limite=(unsigned)strtoul(argv[3],NULL,10),reinicio=(unsigned)strtoul(argv[4],NULL,10);
    for(unsigned n=0;n<limite && s.estado==SIM_PIPE_ACTIVO;++n) {
        if(reinicio && n==reinicio) {
            uint32_t dir=s.id_ex.lsu.valido?s.id_ex.lsu.base+(uint32_t)s.id_ex.lsu.offset:0;
            uint32_t antes=0,despues=0;
            sim_memoria_leer_palabra(&s.memoria,dir,&antes);
            sim_pipeline_reiniciar(&s);
            sim_memoria_leer_palabra(&s.memoria,dir,&despues);
            printf("{\"reset\":1,\"dir\":%u,\"antes\":%u,\"despues\":%u,",dir,antes,despues);
            registros(&s);
        }
        SimPipelineEstado estado=sim_avanzar_ciclo(&s);
        if(estado!=SIM_PIPE_ACTIVO && estado!=SIM_PIPE_TERMINADO) break;
        traza(&s);
    }
    sim_pipeline_ejecutar(&s,0);
    for(unsigned p=0;p<65536;p+=4) {
        uint32_t v=0; sim_memoria_leer_palabra(&s.memoria,p,&v);
        if(v) printf("{\"mem\":%u,\"valor\":%u}\n",p,v);
    }
    C(memcmp(original,imagen->dmem,sizeof original)==0);
    printf("{\"estado\":%u,\"ciclos\":%llu,\"pc\":%u}\n",(unsigned)s.estado,(unsigned long long)s.ciclos,s.pc);
    if(s.estado!=SIM_PIPE_TERMINADO && s.estado!=SIM_PIPE_LIMITE)
        fprintf(stderr,"S5 estado=%u PC=%u bundle=%zu slot=%u palabra=%08x S2=%u causa=%s\n",
            (unsigned)s.estado,s.error.pc,s.error.indice_bundle,s.error.slot,s.error.palabra,
            (unsigned)s.error.decodificacion,s.error.causa?s.error.causa:"sin causa");
    sim_liberar_imagen(&imagen);
    return s.estado==SIM_PIPE_TERMINADO||s.estado==SIM_PIPE_LIMITE?0:1;
}
