/* Pruebas de memoria, LSU y su temporización; también emite vectores/trazas
 * que Python contrasta con los módulos RTL originales de Arquitectura. */
#include "sim_pipeline.h"
#include <stdlib.h>
#include <string.h>

static size_t comprobaciones;
#define C(c) do { ++comprobaciones; if (!(c)) { \
    fprintf(stderr,"FAIL LSU línea %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static const SimOperacion ops[] = {SIM_OP_GUARDAP,SIM_OP_GUARDAB,SIM_OP_CARGAI,SIM_OP_CARGABAI};

static SimAccesoLsu acceso(SimMemoria *m, SimOperacion op, uint32_t base, uint32_t dato, int32_t off)
{
    SimAccesoLsu t;
    C(sim_lsu_preparar(m,op,base,dato,off,&t));
    return t;
}

static uint32_t palabra(const SimMemoria *m, uint32_t dir)
{
    uint32_t v;
    C(sim_memoria_leer_palabra(m,dir,&v));
    return v;
}

static void memoria_y_bytes(void)
{
    /* La palabra no alineada en 3 selecciona [0..3], nunca [3..6]. Los bytes
     * 78/56/34/12 comprueban el orden little-endian independientemente del host. */
    SimMemoria m = {0};
    SimAccesoLsu t = acceso(&m,SIM_OP_GUARDAP,3,0x12345678,0);
    C(t.direccion==3 && t.palabra_fisica==0 && t.byte_enable==15 && t.escritura);
    C(palabra(&m,0)==0);
    C(sim_lsu_aplicar(&m,&t));
    C(m.bytes[0]==0x78 && m.bytes[1]==0x56 && m.bytes[2]==0x34 && m.bytes[3]==0x12);
    C(palabra(&m,4)==0 && palabra(&m,65536)==0x12345678);
    C(acceso(&m,SIM_OP_CARGAI,1,0,0).resultado==0x12345678);
    const uint32_t bytes[] = {0,0x7f,0x80,0xff};
    const uint32_t signos[] = {0,0x7f,0xffffff80u,UINT32_MAX};
    for (unsigned pos=0;pos<4;++pos) {
        for (unsigned n=0;n<4;++n) {
            /* Reinicia la palabra conocida y modifica un único carril con un
             * fuente de bits altos no cero: solo su octeto bajo debe sobrevivir. */
            t=acceso(&m,SIM_OP_GUARDAP,0,0x12345678,0); C(sim_lsu_aplicar(&m,&t));
            t=acceso(&m,SIM_OP_GUARDAB,pos,0xdead0000u|bytes[n],0);
            C(t.byte_enable==(1u<<pos) && t.dato_escritura==((0xdead0000u|bytes[n])<<(8*pos)));
            C(sim_lsu_aplicar(&m,&t));
            uint32_t esperado=(0x12345678u & ~(255u<<(8*pos))) | (bytes[n]<<(8*pos));
            C(palabra(&m,0)==esperado && palabra(&m,4)==0);
            C(acceso(&m,SIM_OP_CARGABAI,pos,0,0).resultado==signos[n]);
        }
    }
    /* La suma y el direccionamiento físico son contratos distintos: FFFFFFFF
     * selecciona el último byte; base FFFFFFFF + 1 vuelve a dirección efectiva 0. */
    t=acceso(&m,SIM_OP_GUARDAP,UINT32_MAX,UINT32_MAX,0); C(sim_lsu_aplicar(&m,&t));
    C(t.palabra_fisica==65532 && palabra(&m,65535)==UINT32_MAX);
    t=acceso(&m,SIM_OP_GUARDAB,UINT32_MAX,0x80,1); C(sim_lsu_aplicar(&m,&t));
    C(t.direccion==0 && m.bytes[0]==0x80);
    C(acceso(&m,SIM_OP_CARGAI,0,0,-1).direccion==UINT32_MAX);
    C(acceso(&m,SIM_OP_CARGAI,1024,0,-1024).direccion==0);
    C(acceso(&m,SIM_OP_CARGAI,0,0,1023).direccion==1023);
    C(acceso(&m,SIM_OP_CARGAI,10,0,-4).direccion==6);
    /* Errores no producen modificaciones parciales ni alteran salida. */
    SimAccesoLsu copia=t;
    C(!sim_lsu_preparar(&m,SIM_OP_SUMA,0,0,0,&t)); C(t.direccion==copia.direccion);
    C(!sim_lsu_preparar(&m,SIM_OP_CARGAI,0,0,1024,&t));
    C(!sim_lsu_preparar(&m,SIM_OP_CARGAI,0,0,-1025,&t));
    C(!sim_lsu_preparar(NULL,SIM_OP_CARGAI,0,0,0,&t));
    C(!sim_lsu_preparar(&m,SIM_OP_CARGAI,0,0,0,NULL));
    C(!sim_memoria_leer_palabra(NULL,0,&copia.lectura));
    C(!sim_memoria_leer_palabra(&m,0,NULL));
    C(!sim_lsu_aplicar(NULL,&t) && !sim_lsu_aplicar(&m,NULL));
    copia.palabra_fisica=65535; C(!sim_lsu_aplicar(&m,&copia));
    copia.palabra_fisica=65536; C(!sim_lsu_aplicar(&m,&copia));
    copia.palabra_fisica=0; copia.byte_enable=16; C(!sim_lsu_aplicar(&m,&copia));
    C(m.bytes[0]==0x80);
}

/* Estímulos binarios de la ISA, no un ensamblador adicional. Python utiliza
 * el ensamblador existente para los programas que se comparan con top. */
static uint32_t lsu(unsigned id,unsigned base,unsigned reg,int off)
{
    return (0x49u<<25)|(id<<21)|(base<<16)|(reg<<11)|((uint32_t)off&2047u);
}
static uint32_t sumai(unsigned rd,unsigned rs,int off)
{
    return (0x40u<<25)|(rs<<16)|(rd<<11)|((uint32_t)off&2047u);
}

static void pipeline_y_recursos(void)
{
    /* B0 almacena x0 en EX del ciclo 3; B1 carga ese cero en EX del 4 y lo
     * escribe en WB del 5. El dato original de la imagen permanece 0x80. */
    SimImagen imagen={.cantidad_bundles=2}; imagen.dmem[0]=0x80;
    imagen.imem[0][1]=lsu(0,0,0,0); imagen.imem[1][1]=lsu(8,0,2,0);
    SimPipeline s,otro;
    C(sim_pipeline_iniciar(&s,&imagen)==SIM_PIPE_ACTIVO);
    C(sim_pipeline_iniciar(&otro,&imagen)==SIM_PIPE_ACTIVO);
    C(s.memoria.bytes[0]==0x80 && otro.memoria.bytes[0]==0x80);
    C(sim_pipeline_ejecutar(&s,2)==SIM_PIPE_LIMITE && s.memoria.bytes[0]==0x80);
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_ACTIVO);
    C(s.ultimo_ciclo.ex_lsu.escritura && s.memoria.bytes[0]==0);
    C(otro.memoria.bytes[0]==0x80 && imagen.dmem[0]==0x80);
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_ACTIVO && s.ex_wb.lsu.valida && s.registros[2]==0);
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_TERMINADO && s.ultimo_ciclo.lsu_wb_escribe);
    C(s.bundles_completados==2 && !s.ultimo_ciclo.wb_escribe);
    C(sim_pipeline_reiniciar(&s)==SIM_PIPE_ACTIVO && s.memoria.bytes[0]==0);
    C(sim_pipeline_restaurar_memoria(&s)==SIM_PIPE_ACTIVO && s.memoria.bytes[0]==0x80);
    C(s.pc==0 && s.ciclos==0 && !s.id_ex.valido);
    /* Reset antes de EX no escribe. En cambio después de dos pasos el store
     * está activo en EX y la memoria lo publica en el propio flanco de reset. */
    sim_avanzar_ciclo(&s); sim_pipeline_reiniciar(&s); C(s.memoria.bytes[0]==0x80);
    sim_pipeline_ejecutar(&s,2); sim_pipeline_reiniciar(&s);
    C(s.memoria.bytes[0]==0 && s.ciclos==0 && !s.ex_wb.valido);
    sim_pipeline_restaurar_memoria(&s);
    /* Load pendiente se descarta con reset; uno ya retirado limpia el banco
     * conservando la memoria. Nueva inicialización sí recarga datos de S1. */
    imagen.imem[0][1]=lsu(9,0,31,0);
    sim_pipeline_iniciar(&s,&imagen); sim_pipeline_ejecutar(&s,3);
    C(s.ex_wb.lsu.valida && s.ex_wb.lsu.valor==0xffffff80u && s.registros[31]==0);
    sim_pipeline_reiniciar(&s); C(s.registros[31]==0 && s.memoria.bytes[0]==0x80);
    sim_pipeline_ejecutar(&s,4); C(s.registros[31]==0xffffff80u);
    sim_pipeline_reiniciar(&s); C(s.registros[31]==0 && s.memoria.bytes[0]==0x80);
    /* Destinos iguales: ambos puertos solicitan escritura en ciclo 4, pero
     * puerto 1 LSU gana a ALU sin stall ni excepción. x0 nunca crea conflicto. */
    imagen.imem[0][0]=sumai(31,0,7);
    sim_pipeline_iniciar(&s,&imagen); sim_pipeline_ejecutar(&s,4);
    C(s.ultimo_ciclo.conflicto_wb && s.ultimo_ciclo.wb_escribe && s.ultimo_ciclo.lsu_wb_escribe);
    C(s.registros[31]==0xffffff80u && s.ultima_escritura.destino==31);
    imagen.imem[0][0]=sumai(0,0,7); imagen.imem[0][1]=lsu(8,0,0,0);
    sim_pipeline_iniciar(&s,&imagen); sim_pipeline_ejecutar(&s,4);
    C(!s.ultimo_ciclo.conflicto_wb && s.ultimo_ciclo.lsu_wb_descarta_x0 && s.registros[0]==0);
    /* Store preparado en EX coincide con BRU inválida para S4 en ID. La
     * detención administrativa debe conservar todos los bytes, etapas y WB. */
    imagen.imem[0][0]=0; imagen.imem[0][1]=lsu(0,0,0,0); imagen.imem[1][2]=0x96000000u;
    sim_pipeline_iniciar(&s,&imagen); sim_pipeline_ejecutar(&s,2);
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_NO_SOPORTADA);
    C(s.ciclos==2 && s.memoria.bytes[0]==0x80 && s.id_ex.lsu.valido);
    C(s.error.pc==16 && s.error.slot==2);
    /* Error posterior a preparar EX: un destino WB inyectado fuera de rango
     * tampoco puede publicar el store. Detecta compromisos prematuros. */
    imagen.imem[1][2]=0;
    sim_pipeline_iniciar(&s,&imagen); sim_pipeline_ejecutar(&s,2);
    s.ex_wb=(SimExWb){.valido=true,.operacion=SIM_OP_SUMA,.destino=32};
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_ARGUMENTO && s.memoria.bytes[0]==0x80 && s.ciclos==2);
    SimPipeline cero={0};
    C(sim_avanzar_ciclo(&cero)==SIM_PIPE_ARGUMENTO);
    C(sim_pipeline_reiniciar(&cero)==SIM_PIPE_ARGUMENTO);
    C(sim_pipeline_ejecutar(&cero,0)==SIM_PIPE_ARGUMENTO);
    C(sim_pipeline_ejecutar(&cero,1)==SIM_PIPE_ARGUMENTO);
    C(sim_pipeline_restaurar_memoria(&cero)==SIM_PIPE_ARGUMENTO);
    C(sim_pipeline_restaurar_memoria(NULL)==SIM_PIPE_ARGUMENTO);
    C(sim_pipeline_iniciar(&s,NULL)==SIM_PIPE_ARGUMENTO);
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_ARGUMENTO);
}

static void registros(const SimPipeline *s)
{
    printf("\"regs\":[");
    for(unsigned n=0;n<32;++n) printf("%s%u",n?",":"",s->registros[n]);
    printf("]}");
    putchar('\n');
}

static void traza(const SimPipeline *s)
{
    const SimCiclo *c=&s->ultimo_ciclo;
    const SimAccesoLsu *t=&c->ex_lsu;
    uint32_t post=0; sim_memoria_leer_palabra(&s->memoria,t->direccion,&post);
    printf("{\"c\":%llu,\"pc\":%u,\"stages\":[%u,%u,%u,%u],\"pcs\":[%u,%u,%u,%u],",
        (unsigned long long)c->ciclo,s->pc,c->if_.valido,c->id.valido,c->ex.valido,c->wb.valido,
        c->if_.pc,c->id.pc,c->ex.pc,c->wb.pc);
    printf("\"idalu\":[%u,%u],\"idlsu\":[%u,%u,%u,%d],\"exalu\":[%u,%u,%u],",
        c->id_a,c->id_b,c->id_lsu.valido,c->id_lsu.base,c->id_lsu.dato,c->id_lsu.offset,
        c->ex_a,c->ex_b,c->ex_resultado);
    printf("\"x\":[%u,%u,%u,%u,%u,%u,%u,%u,%u],\"postmem\":%u,",
        t->valido,t->direccion,t->palabra_fisica,t->byte_enable,t->dato_escritura,
        t->lectura,t->resultado,t->escritura,t->carga,post);
    printf("\"w0\":[%u,%u,%u],\"w1\":[%u,%u,%u],\"conflicto\":%u,\"pending\":[%u,%u,%u],",
        c->wb_escribe,c->wb_destino,c->wb_valor,c->lsu_wb_escribe,c->wb_lsu.destino,c->wb_lsu.valor,
        c->conflicto_wb,s->ex_wb.lsu.valida,s->ex_wb.lsu.destino,s->ex_wb.lsu.valor);
    printf("\"w1_valida\":%u,",c->wb_lsu.valida);
    registros(s);
}

static int vectores(const char *ruta)
{
    FILE *f=fopen(ruta,"r"); C(f!=NULL);
    SimMemoria m={0}; unsigned indice,codigo,base,imm,dato,rd,inicial,valid,reset;
    int n;
    while((n=fscanf(f,"%u %u %x %x %x %u %x %u %u",&indice,&codigo,&base,&imm,&dato,&rd,&inicial,&valid,&reset))==9) {
        C(indice<4 && rd<32 && imm<2048);
        int32_t off=imm&1024u?(int32_t)imm-2048:(int32_t)imm;
        uint32_t dir=base+(uint32_t)off;
        SimAccesoLsu init=acceso(&m,SIM_OP_GUARDAP,dir,inicial,0); C(sim_lsu_aplicar(&m,&init));
        SimAccesoLsu t=acceso(&m,ops[indice],base,dato,off);
        if(valid) C(sim_lsu_aplicar(&m,&t));
        /* reset limpia registros internos LSU pero no mem_we ni DMEM. */
        printf("V %08x %u %x %08x %08x %u %u %08x %08x\n",t.direccion,valid&&t.escritura,t.byte_enable,t.dato_escritura,
            t.lectura,!reset&&valid&&t.carga,reset?0:rd,reset?0:t.resultado,palabra(&m,dir));
    }
    C(n==EOF && !ferror(f)); C(fclose(f)==0); return 0;
}

int main(int argc,char **argv)
{
    if(argc==1) {
        memoria_y_bytes(); pipeline_y_recursos();
        printf("OK: %zu comprobaciones LSU/memoria/pipeline\n",comprobaciones); return 0;
    }
    if(argc==3 && strcmp(argv[1],"vectores")==0) return vectores(argv[2]);
    if(argc!=6 && argc!=7) return 2;
    SimImagen *imagen=NULL;
    SimCargaEstado e=strcmp(argv[1],"mem")==0?sim_cargar_mem(argv[2],&imagen,stderr):sim_cargar_balb(argv[2],&imagen,stderr);
    if(e!=SIM_CARGA_OK) return 1;
    /* Precarga solo del fixture MEM antes de crear instancias; BALB usa sus
     * propios datos de S1. Durante ejecución la imagen debe permanecer intacta. */
    if(argc==7) {
        FILE *f=fopen(argv[6],"rb"); C(f!=NULL);
        C(fread(imagen->dmem,1,sizeof imagen->dmem,f)==sizeof imagen->dmem); C(fclose(f)==0);
    }
    uint8_t imagen_original[SIM_CARGADOR_DMEM_BYTES];
    memcpy(imagen_original,imagen->dmem,sizeof imagen_original);
    SimPipeline s; sim_pipeline_iniciar(&s,imagen);
    unsigned limite=(unsigned)strtoul(argv[3],NULL,10), reinicio=(unsigned)strtoul(argv[4],NULL,10);
    /* argv[5] reserva la restauración explícita para pruebas; no es una opción
     * nueva de BALC. La sonda conserva dos memorias iniciales idénticas al RTL. */
    bool restaurar=strcmp(argv[5],"restaurar")==0;
    for(unsigned n=0;n<limite && s.estado==SIM_PIPE_ACTIVO;++n) {
        if(reinicio && n==reinicio) {
            uint32_t dir=s.id_ex.lsu.valido?s.id_ex.lsu.base+(uint32_t)s.id_ex.lsu.offset:0;
            uint32_t antes=palabra(&s.memoria,dir);
            sim_pipeline_reiniciar(&s);
            if(restaurar) sim_pipeline_restaurar_memoria(&s);
            printf("{\"reset\":1,\"dir\":%u,\"antes\":%u,\"despues\":%u,",dir,antes,palabra(&s.memoria,dir));
            registros(&s);
        }
        SimPipelineEstado estado=sim_avanzar_ciclo(&s);
        if(estado!=SIM_PIPE_ACTIVO && estado!=SIM_PIPE_TERMINADO) break;
        traza(&s);
    }
    for(unsigned p=0;p<SIM_CARGADOR_DMEM_BYTES;p+=4) {
        uint32_t v=palabra(&s.memoria,p);
        if(v) printf("{\"mem\":%u,\"valor\":%u}\n",p,v);
    }
    C(memcmp(imagen->dmem,imagen_original,sizeof imagen_original)==0);
    if(s.estado!=SIM_PIPE_TERMINADO)
        fprintf(stderr,"S4 estado=%u PC=%u slot=%u S2=%u causa=%s\n",(unsigned)s.estado,s.error.pc,s.error.slot,
            (unsigned)s.error.decodificacion,s.error.causa?s.error.causa:"límite");
    sim_liberar_imagen(&imagen);
    return s.estado==SIM_PIPE_TERMINADO?0:1;
}
