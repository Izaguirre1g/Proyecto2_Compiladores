/* Comprueba metadatos individuales con fixtures ISA y el codificador real CB.
 * Los valores esperados son explícitos e independientes del extractor. S2 se
 * utiliza además como referencia diferencial de aceptación, sin ejecutar BAL,
 * formar entradas S7 ni inventar CFG, DAG o efectos del ABI. */
#include "vliw_metadatos.h"
#include "codificacion_binaria.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R(n) (UINT64_C(1)<<(n))
#define L VLIW_EF_LEE_MEMORIA
#define E VLIW_EF_ESCRIBE_MEMORIA
#define X VLIW_EF_EXCEPCION
#define B VLIW_EF_BOVEDA
#define S VLIW_EF_ESTADO
#define PEND_BASE (VLIW_META_PEND_PRODUCTOR|VLIW_META_PEND_CFG_DAG)
#define PEND_BRU (PEND_BASE|VLIW_META_PEND_CONTROL_ABI|VLIW_META_PEND_DESTINOS)
static unsigned escenarios,comprobaciones;
#define EXIGIR(c) do { ++comprobaciones; if (!(c)) { \
    fprintf(stderr,"línea %d: %s\n",__LINE__,#c);exit(1); } } while (0)

typedef struct {
    const char *texto;
    SimOperacion op;
    unsigned slot;
    uint64_t lee,escribe;
    unsigned efectos;
    SimSalto salto;
    bool auth,init;
} Caso;

/* Las34 filas son operaciones reales, no NOP ni pseudoinstrucciones nuevas.
 * Esta tabla es el oráculo manual: no se obtiene ejecutando el extractor. */
static const Caso catalogo[]={
    {"suma x4,x5,x6",SIM_OP_SUMA,0,R(5)|R(6),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"resta x4,x5,x6",SIM_OP_RESTA,0,R(5)|R(6),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"cader x4,x5,x6",SIM_OP_CADER,0,R(5)|R(6),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"cizq x4,x5,x6",SIM_OP_CIZQ,0,R(5)|R(6),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"and x4,x5,x6",SIM_OP_AND,0,R(5)|R(6),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"or x4,x5,x6",SIM_OP_OR,0,R(5)|R(6),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"xor x4,x5,x6",SIM_OP_XOR,0,R(5)|R(6),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"cder x4,x5,x6",SIM_OP_CDER,0,R(5)|R(6),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"mrq x4,x5,x6",SIM_OP_MRQ,0,R(5)|R(6),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"myq x4,x5,x6",SIM_OP_MYQ,0,R(5)|R(6),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"sumai x4,x5,-1024",SIM_OP_SUMAI,0,R(5),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"restai x4,x5,1023",SIM_OP_RESTAI,0,R(5),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"cizqi x4,x5,2047",SIM_OP_CIZQI,0,R(5),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"cderi x4,x5,31",SIM_OP_CDERI,0,R(5),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"caderi x4,x5,0",SIM_OP_CADERI,0,R(5),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"xori x4,x5,-1",SIM_OP_XORI,0,R(5),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"andi x4,x5,255",SIM_OP_ANDI,0,R(5),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"ori x4,x5,127",SIM_OP_ORI,0,R(5),R(4),0,SIM_SALTO_NINGUNO,false,false},
    {"guardap x2,x4,-4",SIM_OP_GUARDAP,1,R(2)|R(4),0,E,SIM_SALTO_NINGUNO,false,false},
    {"guardab x2,x4,1023",SIM_OP_GUARDAB,1,R(2)|R(4),0,E,SIM_SALTO_NINGUNO,false,false},
    {"cargai x4,x2,8",SIM_OP_CARGAI,1,R(2),R(4),L,SIM_SALTO_NINGUNO,false,false},
    {"cargabai x4,x2,-1024",SIM_OP_CARGABAI,1,R(2),R(4),L,SIM_SALTO_NINGUNO,false,false},
    {"igualsi x4,x5,0",SIM_OP_IGUALSI,2,R(4)|R(5),0,0,SIM_SALTO_COND_PC,false,false},
    {"igualno x4,x5,0",SIM_OP_IGUALNO,2,R(4)|R(5),0,0,SIM_SALTO_COND_PC,false,false},
    {"menora x4,x5,0",SIM_OP_MENORA,2,R(4)|R(5),0,0,SIM_SALTO_COND_PC,false,false},
    {"mayoroigual x4,x5,0",SIM_OP_MAYOROIGUAL,2,R(4)|R(5),0,0,SIM_SALTO_COND_PC,false,false},
    {"sye x1,0",SIM_OP_SYE,2,0,R(1),0,SIM_SALTO_PC,false,false},
    {"syer x1,x13,0",SIM_OP_SYER,2,R(13),R(1),0,SIM_SALTO_REGISTRO,false,false},
    {"fsl x6,x2,0,1",SIM_OP_FSL,3,R(2)|R(3),R(6)|R(7),X,SIM_SALTO_NINGUNO,true,false},
    {"fsli x8,x4,3,2",SIM_OP_FSLI,3,R(4)|R(5),R(8)|R(9),X,SIM_SALTO_NINGUNO,true,false},
    {"ell 2,0,x4,x10",SIM_OP_ELL,3,R(4)|R(10),0,B|X,SIM_SALTO_NINGUNO,true,false},
    {"vcr 8192",SIM_OP_VCR,3,0,0,L|S,SIM_SALTO_NINGUNO,false,false},
    {"camcon 16,7",SIM_OP_CAMCON,3,0,0,L|E|B|X,SIM_SALTO_NINGUNO,true,false},
    {"setpwd x25",SIM_OP_SETPWD,3,R(25),0,B|S|X,SIM_SALTO_NINGUNO,false,true}
};

/* Obtiene la palabra mediante CB existente, en un contexto aislado de prueba.
 * No es otro codificador ni un backend. Libera CbPrograma antes de devolver
 * la palabra por valor; los textos directos de prueba usan offset0 conocido. */
static uint32_t codificar(const char *texto,unsigned slot)
{
    const char *slots[4]={"nop","nop","nop","nop"};slots[slot]=texto;
    char asm_texto[256];
    (void)snprintf(asm_texto,sizeof asm_texto,"%s | %s | %s | %s\n",slots[0],slots[1],slots[2],slots[3]);
    CbPrograma cb={0};EXIGIR(cb_ensamblar(asm_texto,&cb,stderr)==0);EXIGIR(cb.cantidad==1);
    uint32_t palabra=cb.bundles[0].slots[slot];cb_liberar(&cb);return palabra;
}

/* Contrasta todos los campos públicos con expectativas ISA independientes.
 * La comparación por campos evita convertir padding de una estructura en
 * significado arquitectónico. S2 aporta además un contraste diferencial. */
static void verificar_caso(const Caso *c)
{
    uint32_t palabra=codificar(c->texto,c->slot),original=palabra;
    VliwMetadatosParciales m;VliwMetadatosDiagnostico d;
    EXIGIR(vliw_metadatos_extraer(palabra,c->slot,&m,&d)==SIM_DECODE_OK);
    EXIGIR(palabra==original);EXIGIR(d.estado==SIM_DECODE_OK && d.palabra==palabra && d.slot==c->slot);
    EXIGIR(d.mensaje && *d.mensaje);EXIGIR(m.operacion==c->op);
    static const SimFamilia familias[]={SIM_FAMILIA_ALU,SIM_FAMILIA_LSU,SIM_FAMILIA_BRU,SIM_FAMILIA_CRIPTO};
    EXIGIR(m.familia==familias[c->slot]);EXIGIR((unsigned)m.slot==c->slot);
    EXIGIR(m.lee==c->lee);EXIGIR(m.escribe==c->escribe);EXIGIR(m.efectos_minimos==c->efectos);
    EXIGIR(m.salto==c->salto);EXIGIR(m.requiere_auth==c->auth);EXIGIR(m.requiere_init==c->init);
    EXIGIR(m.pendientes==(c->salto==SIM_SALTO_NINGUNO ? PEND_BASE : PEND_BRU));
    SimInstruccion s2;EXIGIR(sim_decode_palabra(palabra,c->slot,&s2)==SIM_DECODE_OK);
    EXIGIR(s2.operacion==c->op && s2.salto==c->salto);
    EXIGIR((m.lee & R(0))==0 && (m.escribe & R(0))==0);++escenarios;
}

/* Distingue las34 operaciones ISA de las alias y NOP. Los efectos esperados
 * reproducen el contrato de I1-A1, sin invocar ni duplicar su validador. */
static void prueba_catalogo(void)
{
    EXIGIR(sizeof catalogo/sizeof catalogo[0]==34);
    for (size_t i=0;i<sizeof catalogo/sizeof catalogo[0];++i) verificar_caso(&catalogo[i]);
}

/* Revisa fuentes/destinos coincidentes, límites GPR y pares Feistel. VCR y x0
 * pueden producir máscara0 por razones distintas; se comprueba la ausencia
 * con SIM_REG_AUSENTE de S2, sin convertirla en un operando x0 ficticio. */
static void prueba_registros(void)
{
    const Caso casos[]={
        {"sumai x0,x0,1",SIM_OP_SUMAI,0,0,0,0,SIM_SALTO_NINGUNO,false,false},
        {"suma x31,x31,x31",SIM_OP_SUMA,0,R(31),R(31),0,SIM_SALTO_NINGUNO,false,false},
        {"suma x1,x0,x31",SIM_OP_SUMA,0,R(31),R(1),0,SIM_SALTO_NINGUNO,false,false},
        {"cargai x0,x31,0",SIM_OP_CARGAI,1,R(31),0,L,SIM_SALTO_NINGUNO,false,false},
        {"guardap x0,x31,0",SIM_OP_GUARDAP,1,R(31),0,E,SIM_SALTO_NINGUNO,false,false},
        {"guardab x31,x31,0",SIM_OP_GUARDAB,1,R(31),0,E,SIM_SALTO_NINGUNO,false,false},
        {"fsl x30,x30,0,0",SIM_OP_FSL,3,R(30)|R(31),R(30)|R(31),X,SIM_SALTO_NINGUNO,true,false},
        {"ell 0,2,x0,x0",SIM_OP_ELL,3,0,0,B|X,SIM_SALTO_NINGUNO,true,false},
        {"vcr 0",SIM_OP_VCR,3,0,0,L|S,SIM_SALTO_NINGUNO,false,false},
        {"setpwd x0",SIM_OP_SETPWD,3,0,0,B|S|X,SIM_SALTO_NINGUNO,false,true}
    };
    for (size_t i=0;i<sizeof casos/sizeof casos[0];++i) verificar_caso(&casos[i]);
    SimInstruccion r;uint32_t palabra=codificar("vcr 0",3);
    EXIGIR(sim_decode_palabra(palabra,3,&r)==SIM_DECODE_OK);
    EXIGIR(r.rs1==SIM_REG_AUSENTE && r.rs2==SIM_REG_AUSENTE && r.rd==SIM_REG_AUSENTE && r.rd2==SIM_REG_AUSENTE);
    palabra=codificar("sumai x0,x0,1",0);EXIGIR(sim_decode_palabra(palabra,0,&r)==SIM_DECODE_OK);
    EXIGIR(r.rs1==0 && r.rd==0 && r.rs2==SIM_REG_AUSENTE && r.rd2==SIM_REG_AUSENTE);
    ++escenarios;
}

/* La forma física no acredita llamada o retorno: escribir x1 desde ALU,
 * enlazar SYE/SYER y la alias RET conservan solo forma S2 y pendientes ABI.
 * SYER permite offset1; no se le aplica la alineación de un salto por PC. */
static void prueba_control(void)
{
    const Caso casos[]={
        {"sumai x1,x1,7",SIM_OP_SUMAI,0,R(1),R(1),0,SIM_SALTO_NINGUNO,false,false},
        {"sye x0,0",SIM_OP_SYE,2,0,0,0,SIM_SALTO_PC,false,false},
        {"sye x1,0",SIM_OP_SYE,2,0,R(1),0,SIM_SALTO_PC,false,false},
        {"syer x0,x1,0",SIM_OP_SYER,2,R(1),0,0,SIM_SALTO_REGISTRO,false,false},
        {"syer x1,x13,0",SIM_OP_SYER,2,R(13),R(1),0,SIM_SALTO_REGISTRO,false,false},
        {"syer x31,x0,1",SIM_OP_SYER,2,0,R(31),0,SIM_SALTO_REGISTRO,false,false},
        {"ret",SIM_OP_SYER,2,R(1),0,0,SIM_SALTO_REGISTRO,false,false},
        {"igualsi x1,x31,0",SIM_OP_IGUALSI,2,R(1)|R(31),0,0,SIM_SALTO_COND_PC,false,false}
    };
    for (size_t i=0;i<sizeof casos/sizeof casos[0];++i) verificar_caso(&casos[i]);
}

/* Comprueba un rechazo esperado y que no publique siquiera parte de la salida.
 * El patrón anterior se compara como bytes: no se interpretan bool con una
 * representación artificial. Aquí memset define también el relleno: cualquier
 * byte modificado por un rechazo viola el contrato. Esta prueba es independiente
 * de conservar los campos de una salida publicada correctamente, cuyo padding
 * no tiene por qué estar definido. El diagnóstico ocupa otro almacenamiento. */
static void verificar_error(uint32_t palabra,unsigned slot,SimDecodeEstado esperado)
{
    VliwMetadatosParciales salida;memset(&salida,0xa5,sizeof salida);
    unsigned char anterior[sizeof salida];memcpy(anterior,&salida,sizeof salida);
    VliwMetadatosDiagnostico d;
    EXIGIR(vliw_metadatos_extraer(palabra,slot,&salida,&d)==esperado);
    EXIGIR(d.estado==esperado && d.palabra==palabra && d.slot==slot && d.mensaje);
    EXIGIR(memcmp(anterior,&salida,sizeof salida)==0);++escenarios;
}

/* Usa codificaciones reservadas y campos inválidos concretos, con estados S2
 * esperados manualmente. No se leen punteros arbitrarios ni se intenta
 * representar un inmediato mayor que el campo de una palabra de32 bits. */
static void prueba_rechazos(void)
{
    uint32_t suma=codificar("suma x4,x5,x6",0);
    verificar_error(suma,4,SIM_DECODE_ARGUMENTO);verificar_error(suma,UINT_MAX,SIM_DECODE_ARGUMENTO);
    VliwMetadatosDiagnostico d;
    EXIGIR(vliw_metadatos_extraer(suma,0,NULL,&d)==SIM_DECODE_ARGUMENTO);++escenarios;
    EXIGIR(vliw_metadatos_extraer(suma,0,NULL,NULL)==SIM_DECODE_ARGUMENTO);++escenarios;
    for (unsigned s=0;s<4;++s) verificar_error(0,s,SIM_DECODE_NOP);
    verificar_error(UINT32_MAX,0,SIM_DECODE_RESERVADA);
    verificar_error(UINT32_C(0x6a)<<25,0,SIM_DECODE_RESERVADA);
    verificar_error((UINT32_C(0x40)<<25)|(UINT32_C(8)<<21),0,SIM_DECODE_RESERVADA);
    verificar_error((UINT32_C(0x02)<<25)|(UINT32_C(6)<<21),3,SIM_DECODE_RESERVADA);
    verificar_error(suma,1,SIM_DECODE_SLOT);
    uint32_t fsl=codificar("fsl x6,x2,0,0",3);
    verificar_error((fsl & ~(UINT32_C(31)<<12)),3,SIM_DECODE_CAMPOS);
    verificar_error((fsl & ~(UINT32_C(31)<<12))|(UINT32_C(31)<<12),3,SIM_DECODE_CAMPOS);
    verificar_error((fsl & ~(UINT32_C(31)<<7))|(UINT32_C(3)<<7),3,SIM_DECODE_CAMPOS);
    uint32_t ell=codificar("ell 0,0,x4,x10",3);
    verificar_error(ell|(UINT32_C(1)<<17),3,SIM_DECODE_CAMPOS);
    verificar_error(ell|(UINT32_C(1)<<12),3,SIM_DECODE_CAMPOS);
    verificar_error(codificar("sye x0,0",2)|1u,2,SIM_DECODE_CAMPOS);
    verificar_error(codificar("igualsi x4,x5,0",2)|1u,2,SIM_DECODE_CAMPOS);
}

/* Contrasta todos los campos publicados, sin interpretar el padding como dato.
 * Una asignación de estructuras conserva valores, pero no garantiza bytes de
 * relleno definidos. Recibe objetos prestados con campos definidos y no usa
 * recursos; detecta una entrada o salida alterada por la llamada evaluada. */
static bool mismos_metadatos(const VliwMetadatosParciales *a,const VliwMetadatosParciales *b)
{
    return a->operacion==b->operacion && a->familia==b->familia && a->slot==b->slot &&
        a->lee==b->lee && a->escribe==b->escribe && a->efectos_minimos==b->efectos_minimos &&
        a->salto==b->salto && a->requiere_auth==b->requiere_auth &&
        a->requiere_init==b->requiere_init && a->pendientes==b->pendientes;
}

/* Reutiliza una salida válida, provoca error/NOP y vuelve a extraer. Los bits
 * inferiores libres de REG se ignoran según S2; no se endurece la ISA al exigir
 * una codificación canónica que el contrato existente no exige. */
static void prueba_publicacion(void)
{
    uint32_t palabra=codificar("suma x4,x5,x6",0),antes=palabra;
    VliwMetadatosParciales m;EXIGIR(vliw_metadatos_extraer(palabra,0,&m,NULL)==SIM_DECODE_OK);
    VliwMetadatosParciales anterior=m;
    EXIGIR(vliw_metadatos_extraer(0,0,&m,NULL)==SIM_DECODE_NOP);
    EXIGIR(mismos_metadatos(&anterior,&m));++escenarios;
    EXIGIR(vliw_metadatos_extraer(UINT32_MAX,0,&m,NULL)==SIM_DECODE_RESERVADA);
    EXIGIR(mismos_metadatos(&anterior,&m));++escenarios;
    EXIGIR(vliw_metadatos_extraer(palabra|63u,0,&m,NULL)==SIM_DECODE_OK);
    EXIGIR(m.lee==(R(5)|R(6)) && m.escribe==R(4));EXIGIR(palabra==antes);++escenarios;
    palabra=codificar("vcr 0",3);EXIGIR(vliw_metadatos_extraer(palabra,3,&m,NULL)==SIM_DECODE_OK);
    EXIGIR(m.lee==0 && m.escribe==0 && m.efectos_minimos==(L|S) && m.operacion==SIM_OP_VCR);
    EXIGIR(m.pendientes==PEND_BASE);++escenarios;
}

/* Barre todos los tipos/IDs en los cuatro slots con campos bajos cero. S2 es
 * referencia de aceptación, no el extractor bajo prueba. El barrido no son
 * todas las2^32 palabras; complementa los operandos dirigidos del catálogo. */
static void prueba_barrido(void)
{
    for (unsigned tipo=0;tipo<128;++tipo) for (unsigned id=0;id<16;++id) for (unsigned slot=0;slot<4;++slot) {
        uint32_t palabra=((uint32_t)tipo<<25)|((uint32_t)id<<21);SimInstruccion real;
        SimDecodeEstado esperado=sim_decode_palabra(palabra,slot,&real);
        if (esperado==SIM_DECODE_OK) {
            VliwMetadatosParciales m;
            EXIGIR(vliw_metadatos_extraer(palabra,slot,&m,NULL)==esperado);
            EXIGIR(m.operacion==real.operacion && (unsigned)m.slot==slot);
            EXIGIR(m.pendientes!=0 && (m.lee & R(0))==0 && (m.escribe & R(0))==0);++escenarios;
        } else verificar_error(palabra,slot,esperado);
    }
}

/* Ejecuta solamente el grupo solicitado y publica sus cantidades reales. Un
 * nombre desconocido retorna2; no se omiten escenarios simulando un éxito. */
int main(int argc,char **argv)
{
    if (argc!=2) return 2;
    if (!strcmp(argv[1],"catalogo")) prueba_catalogo();
    else if (!strcmp(argv[1],"registros")) prueba_registros();
    else if (!strcmp(argv[1],"control")) prueba_control();
    else if (!strcmp(argv[1],"rechazos")) prueba_rechazos();
    else if (!strcmp(argv[1],"publicacion")) prueba_publicacion();
    else if (!strcmp(argv[1],"barrido")) prueba_barrido();
    else return 2;
    printf("PASS %s: escenarios=%u; comprobaciones=%u\n",argv[1],escenarios,comprobaciones);return 0;
}
