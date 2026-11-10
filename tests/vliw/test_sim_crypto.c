/* Pruebas criptográficas y sonda controlada para contrastar el pipeline.
 * Solo este ejecutable de pruebas observa secretos; la API no imprime bóveda. */
#include "sim_pipeline.h"
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define C(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL S6 %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static SimInstruccion dec(uint32_t w) {
    SimInstruccion i; C(sim_decode_palabra(w,3,&i)==SIM_DECODE_OK); return i;
}
static SimEfectoCrypto preparar(SimBoveda *b,SimMemoria *m,uint32_t w,uint32_t a,uint32_t v,uint32_t estado) {
    SimInstruccion i=dec(w); SimCryptoId id; SimEfectoCrypto e;
    C(sim_crypto_capturar(&i,a,v,estado,&id)); C(sim_crypto_preparar(b,m,&id,&e)); return e;
}
static void unitarias(void) {
    /* Expectativas literales para extremos y rotaciones cero/31: detectan
     * shifts indefinidos y confundir desplazamiento con rotación circular. */
    C(sim_crypto_rol32(0x80000001u,0)==0x80000001u);
    C(sim_crypto_rol32(0x80000001u,1)==3);
    C(sim_crypto_rol32(0x80000001u,5)==48);
    C(sim_crypto_rol32(0x80000001u,13)==12288);
    C(sim_crypto_rol32(0x80000001u,31)==0xc0000000u);
    SimBoveda b={0},otro={0}; SimMemoria m={0};
    SimEfectoCrypto e=preparar(&b,&m,0x04600000u,0,0,2);
    C(e.wb.auth_we && !e.wb.auth); /* pwd_set impide autenticar el cero inicial. */
    e=preparar(&b,&m,0x04a00000u,0,0,2);sim_crypto_aplicar(&b,&e);
    C(b.pwd_set && b.password==0 && e.wb.init_clr && !otro.pwd_set);
    e=preparar(&b,&m,0x04600000u,0,0,0); C(e.wb.auth);
    /* Cada operación protegida debe producir únicamente excepción. La bóveda
     * y DMEM conservan sus valores; vcr siempre está permitida. */
    const uint32_t palabras[]={0x04004200u,0x04204200u,0x04404300u,0x04800007u,0x04a00000u};
    for(unsigned n=0;n<5;++n) {
        SimBoveda antes=b; e=preparar(&b,&m,palabras[n],123,456,0);
        C(e.excepcion && !e.wb.par && !e.memoria.valido);
        sim_crypto_aplicar(&b,&e); C(!memcmp(&antes,&b,sizeof b));
    }
    /* Las cuatro llaves se llenan por pares legales y se sobrescriben sin
     * afectar otra instancia. Los índices LK/RK recorren todas las subllaves. */
    for(unsigned k=0;k<4;++k) for(unsigned off=0;off<4;off+=2) {
        e=preparar(&b,&m,0x04404300u|(k<<19)|(off<<17),100+k+off,200+k+off,1);
        C(!e.excepcion && e.llave_we);sim_crypto_aplicar(&b,&e);
        C(b.keys[k][off]==100+k+off && b.keys[k][off+1]==200+k+off);
    }
    /* Vector HOLA2026 publicado en tb_crypto_unit, calculado con la referencia
     * del enunciado. Detecta errores compartidos por encrypt/decrypt. */
    const uint32_t key[]={0x0f1e2d3cu,0x4b5a6978u,0x8796a5b4u,0xc3d2e1f0u};
    memcpy(b.keys[0],key,sizeof key);
    uint32_t l=0x414c4f48u,v=0x36323032u;
    for(unsigned k=0;k<4;++k) {e=preparar(&b,&m,0x04004200u|(k<<17),l,v,1);l=e.wb.l;v=e.wb.r;}
    C(l==0x03d44466u && v==0xd685afafu);
    for(unsigned k=4;k-->0;) {e=preparar(&b,&m,0x04204200u|(k<<17),l,v,1);l=e.wb.l;v=e.wb.r;}
    C(l==0x414c4f48u && v==0x36323032u);
    for(unsigned k=0;k<4;++k) for(unsigned w=0;w<4;++w) {
        e=preparar(&b,&m,0x04004200u|(k<<19)|(w<<17),UINT32_MAX,0x80000000u,1);
        SimEfectoCrypto inv=preparar(&b,&m,0x04204200u|(k<<19)|(w<<17),e.wb.l,e.wb.r,1);
        C(inv.wb.l==UINT32_MAX && inv.wb.r==0x80000000u);
    }
    SimCryptoId id={0}; SimEfectoCrypto anterior=e;
    C(!sim_crypto_preparar(NULL,&m,&id,&e)); C(!memcmp(&e,&anterior,sizeof e));
    C(!sim_crypto_capturar(NULL,0,0,0,&id));
    SimInstruccion i=dec(0x04004200u); i.rd=31;
    C(sim_crypto_capturar(&i,0,0,1,&id));C(!sim_crypto_preparar(&b,&m,&id,&e));
    sim_crypto_reset(&b); C(!b.pwd_set && !b.password);
    for(unsigned k=0;k<4;++k) for(unsigned w=0;w<4;++w) C(!b.keys[k][w]);
    /* Atomicidad ante un rechazo administrativo: camcon/store están en EX,
     * pero una palabra reservada en ID impide aceptar todo el flanco. Ni la
     * memoria ni la contraseña pueden cambiar anticipadamente. */
    SimImagen imagen={.cantidad_bundles=2};
    imagen.dmem[0]=7; imagen.imem[0][1]=0x92000000u;
    imagen.imem[0][3]=0x04800001u; imagen.imem[1][3]=0x04c00000u;
    SimPipeline s, independiente;
    C(sim_pipeline_iniciar(&s,&imagen)==SIM_PIPE_ACTIVO);
    C(sim_pipeline_iniciar(&independiente,&imagen)==SIM_PIPE_ACTIVO);
    s.estado_crypto=1; s.boveda.pwd_set=true; s.boveda.password=7;
    C(sim_pipeline_ejecutar(&s,2)==SIM_PIPE_LIMITE);
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_DECODIFICACION);
    C(s.ciclos==2 && s.memoria.bytes[0]==7 && s.boveda.password==7);
    C(independiente.memoria.bytes[0]==7 && !independiente.boveda.pwd_set);
    /* Reset sí produce un flanco físico: A intenta escribir cero y B rota
     * el siete preflanco a catorce. Solo la memoria conserva ese efecto. */
    C(sim_pipeline_reiniciar(&s)==SIM_PIPE_ACTIVO);
    C(s.memoria.bytes[0]==14 && !s.boveda.pwd_set && s.estado_crypto==2);
    C(sim_pipeline_restaurar_memoria(&s)==SIM_PIPE_ACTIVO && s.memoria.bytes[0]==7);
    C(independiente.memoria.bytes[0]==7 && imagen.dmem[0]==7);
    /* trap_count es de 32 bits, no saturado. Su desbordamiento no convierte
     * una excepción de hardware en error administrativo del simulador. */
    imagen=(SimImagen){.cantidad_bundles=1}; imagen.imem[0][3]=0x04004200u;
    C(sim_pipeline_iniciar(&s,&imagen)==SIM_PIPE_ACTIVO);
    C(sim_pipeline_ejecutar(&s,2)==SIM_PIPE_LIMITE);s.trap_count=UINT32_MAX;
    C(sim_avanzar_ciclo(&s)==SIM_PIPE_ACTIVO && s.trap_count==0 && s.pc==SIM_TRAP_VECTOR);
    printf("S6: %u comprobaciones unitarias PASS\n",checks);
}

static void vector(const char *ruta) {
    FILE *f=fopen(ruta,"r"); C(f!=NULL);
    unsigned l,v,k,n; int leidos;
    while((leidos=fscanf(f,"%x %x %x %u",&l,&v,&k,&n))==4)
        printf("F %08x %08x %08x %08x %08x %08x\n",sim_crypto_rol32(l,n),sim_crypto_f(v,k),v,l^sim_crypto_f(v,k),v^sim_crypto_f(l,k),l);
    C(leidos==EOF);C(fclose(f)==0);
}

static void valor(const char *nombre,uint64_t v,bool *primero) {
    printf("%s\"%s\":%llu",*primero?"":",",nombre,(unsigned long long)v);*primero=false;
}
static void traza(const SimPipeline *s, const SimIdEx *ex, uint32_t pre) {
    const SimCiclo *c=&s->ultimo_ciclo; bool primero=true; uint32_t ma=0,mb=0;
    sim_memoria_leer_palabra(&s->memoria,c->ex_lsu.direccion,&ma);
    sim_memoria_leer_palabra(&s->memoria,c->ex_crypto.memoria.direccion,&mb);
    (void)ex; (void)pre; (void)ma; (void)mb;
    printf("{");
    valor("c",c->ciclo,&primero);
    valor("pc_pre",pre,&primero);
    valor("pc",s->pc,&primero);
    valor("estado_pre",c->estado_pre,&primero);
    valor("estado",s->estado_crypto,&primero);
    valor("trap_pc",s->trap_pc,&primero);
    valor("traps",s->trap_count,&primero);
    valor("fault",c->excepcion,&primero);
    valor("flush",c->flush,&primero);
    valor("redirect",c->flush?c->redirect_pc:0,&primero);
    valor("conflicto",c->conflicto_wb,&primero);
    valor("v0",c->if_.valido,&primero);
    valor("pc0",c->if_.valido?c->if_.pc:0,&primero);
    valor("v1",c->id.valido,&primero);
    valor("pc1",c->id.valido?c->id.pc:0,&primero);
    valor("v2",c->ex.valido,&primero);
    valor("pc2",c->ex.valido?c->ex.pc:0,&primero);
    valor("v3",c->wb.valido,&primero);
    valor("pc3",c->wb.valido?c->wb.pc:0,&primero);
    valor("kill_if",c->if_descartado,&primero);
    valor("kill_id",c->id_descartado,&primero);
    valor("id_alu_a",(c->id.valido&&!c->flush&&(1))?c->id_a:0,&primero);
    valor("id_alu_b",(c->id.valido&&!c->flush&&(1))?c->id_b:0,&primero);
    valor("id_lsu_a",(c->id.valido&&!c->flush&&(c->id_lsu.valido))?c->id_lsu.base:0,&primero);
    valor("id_lsu_b",(c->id.valido&&!c->flush&&(c->id_lsu.valido))?c->id_lsu.dato:0,&primero);
    valor("id_bru_a",(c->id.valido&&!c->flush&&(c->id_bru.valido))?c->id_bru.a:0,&primero);
    valor("id_bru_b",(c->id.valido&&!c->flush&&(c->id_bru.valido))?c->id_bru.b:0,&primero);
    valor("id_crypto_a",(c->id.valido&&!c->flush&&(c->id_crypto.valido))?c->id_crypto.a:0,&primero);
    valor("id_crypto_b",(c->id.valido&&!c->flush&&(c->id_crypto.valido))?c->id_crypto.b:0,&primero);
    valor("id_crypto_fault",c->id_crypto.valido&&c->id_crypto.falta_permiso,&primero);
    valor("ex_crypto_a",ex->valido&&ex->crypto.valido?ex->crypto.a:0,&primero);
    valor("ex_crypto_b",ex->valido&&ex->crypto.valido?ex->crypto.b:0,&primero);
    valor("a_valid",c->ex_lsu.valido,&primero);
    valor("a_addr",c->ex_lsu.valido?c->ex_lsu.direccion:0,&primero);
    valor("a_rd",c->ex_lsu.valido?c->ex_lsu.lectura:0,&primero);
    valor("a_we",c->ex_lsu.valido?c->ex_lsu.escritura:0,&primero);
    valor("a_wd",c->ex_lsu.escritura?c->ex_lsu.dato_escritura:0,&primero);
    valor("a_post",c->ex_lsu.valido?ma:0,&primero);
    valor("b_valid",c->ex_crypto.memoria.valido,&primero);
    valor("b_addr",c->ex_crypto.memoria.valido?c->ex_crypto.memoria.direccion:0,&primero);
    valor("b_rd",c->ex_crypto.memoria.valido?c->ex_crypto.memoria.lectura:0,&primero);
    valor("b_we",c->ex_crypto.memoria.valido?c->ex_crypto.memoria.escritura:0,&primero);
    valor("b_wd",c->ex_crypto.memoria.escritura?c->ex_crypto.memoria.dato_escritura:0,&primero);
    valor("b_post",c->ex_crypto.memoria.valido?mb:0,&primero);
    valor("w0",c->wb_escribe,&primero);
    valor("w0_rd",c->wb_escribe?c->wb_destino:0,&primero);
    valor("w0_data",c->wb_escribe?c->wb_valor:0,&primero);
    valor("w1",c->lsu_wb_escribe,&primero);
    valor("w1_rd",c->lsu_wb_escribe?c->wb_lsu.destino:0,&primero);
    valor("w1_data",c->lsu_wb_escribe?c->wb_lsu.valor:0,&primero);
    valor("w2",c->bru_wb_escribe,&primero);
    valor("w2_rd",c->bru_wb_escribe?c->wb_bru.destino:0,&primero);
    valor("w2_data",c->bru_wb_escribe?c->wb_bru.valor:0,&primero);
    valor("w3",c->crypto_wb_escribe[0],&primero);
    valor("w3_rd",c->crypto_wb_escribe[0]?c->wb_crypto.rd:0,&primero);
    valor("w3_data",c->crypto_wb_escribe[0]?c->wb_crypto.l:0,&primero);
    valor("w4",c->crypto_wb_escribe[1],&primero);
    valor("w4_rd",c->crypto_wb_escribe[1]?c->wb_crypto.rd2:0,&primero);
    valor("w4_data",c->crypto_wb_escribe[1]?c->wb_crypto.r:0,&primero);
    valor("par_l",s->ex_wb.crypto.par?s->ex_wb.crypto.l:0,&primero);
    valor("par_r",s->ex_wb.crypto.par?s->ex_wb.crypto.r:0,&primero);
    valor("par_rd",s->ex_wb.crypto.par?s->ex_wb.crypto.rd:0,&primero);
    valor("auth_d",s->ex_wb.crypto.auth_we?s->ex_wb.crypto.auth:0,&primero);
    valor("par",s->ex_wb.crypto.par,&primero);
    valor("auth_we",s->ex_wb.crypto.auth_we,&primero);
    valor("init_clr",s->ex_wb.crypto.init_clr,&primero);
    valor("admitidos",s->bundles_admitidos,&primero);
    valor("completados",s->bundles_completados,&primero);
    valor("anulados",s->bundles_anulados,&primero);
    valor("ex_alu_a",(ex->valido)?ex->a:0,&primero);
    valor("ex_alu_b",(ex->valido)?ex->b:0,&primero);
    valor("ex_lsu_a",(ex->valido&&ex->lsu.valido)?ex->lsu.base:0,&primero);
    valor("ex_lsu_b",(ex->valido&&ex->lsu.valido)?ex->lsu.dato:0,&primero);
    valor("ex_bru_a",(ex->valido&&ex->bru.valido)?ex->bru.a:0,&primero);
    valor("ex_bru_b",(ex->valido&&ex->bru.valido)?ex->bru.b:0,&primero);
    valor("ex_alu_resultado",c->ex.valido&&!c->excepcion?c->ex_resultado:0,&primero);
    printf(",\"regs\":[");for(unsigned i=0;i<32;++i) printf("%s%u",i?",":"",s->registros[i]);
    /* Inspección deliberadamente limitada al ejecutable de pruebas. Nunca se
     * incorpora a las trazas públicas de SimCiclo ni a una instrucción ISA. */
    printf("],\"sonda_boveda\":[%u,%u",s->boveda.password,s->boveda.pwd_set);
    for(unsigned k=0;k<4;++k)for(unsigned w=0;w<4;++w)printf(",%u",s->boveda.keys[k][w]);
    puts("]}");
}
int main(int argc,char **argv) {
    if(argc==1) {unitarias();return 0;}
    if(argc==3 && !strcmp(argv[1],"vectores")) {vector(argv[2]);return 0;}
    C(argc==7);
    SimImagen *imagen=NULL;
    C((!strcmp(argv[1],"balb")?sim_cargar_balb:sim_cargar_mem)(argv[2],&imagen,stderr)==SIM_CARGA_OK);
    if(strcmp(argv[3],"-")) {FILE *f=fopen(argv[3],"rb");C(f!=NULL);C(fread(imagen->dmem,1,65536,f)==65536);C(!fclose(f));}
    SimImagen *original=malloc(sizeof *original);C(original!=NULL);memcpy(original,imagen,sizeof *original);
    SimPipeline *s=malloc(sizeof *s);C(s!=NULL);C(sim_pipeline_iniciar(s,imagen)==SIM_PIPE_ACTIVO);
    C(sim_pipeline_configurar_busqueda(s,SIM_BUSQUEDA_FISICA)==SIM_PIPE_ACTIVO);
    unsigned ciclos=(unsigned)strtoul(argv[4],NULL,10),reinicio=(unsigned)strtoul(argv[5],NULL,10);
    C(!strcmp(argv[6],"sonda"));
    for(unsigned paso=0;paso<ciclos;++paso) {
        if(reinicio && paso==reinicio) {C(sim_pipeline_reiniciar(s)==SIM_PIPE_ACTIVO);}
        SimIdEx ex=s->id_ex; uint32_t pre=s->pc;
        C(sim_avanzar_ciclo(s)==SIM_PIPE_ACTIVO);traza(s,&ex,pre);
        C(s->bundles_admitidos==s->bundles_completados+s->bundles_anulados+s->if_id.valido+s->id_ex.valido+s->ex_wb.valido);
    }
    for(unsigned p=0;p<65536;p+=4) {uint32_t v;sim_memoria_leer_palabra(&s->memoria,p,&v);if(v)printf("{\"mem\":%u,\"valor\":%u}\n",p,v);}
    C(!memcmp(original,imagen,sizeof *original));free(original);free(s);sim_liberar_imagen(&imagen);return 0;
}
