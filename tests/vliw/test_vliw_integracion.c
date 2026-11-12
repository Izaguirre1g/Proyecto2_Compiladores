/* Prueba I1-A1 con operaciones físicas explícitas, nunca con BAL compilado.
 * Contrasta rechazos, entradas intactas y compatibilidad controlada S7/S8-A.
 * Los oráculos de máscaras, distancias y destinos se declaran en los fixtures;
 * las variantes de recursos interceptan reservas sin cambiar las bibliotecas. */
#include "vliw_integracion.h"
#include "codificacion_binaria.h"
#include "sim_decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R(n) (UINT64_C(1) << (n))
#define VAC VLIW_VACIO
#define LEER VLIW_EF_LEE_MEMORIA
#define ESCRIBIR VLIW_EF_ESCRIBE_MEMORIA
#define EXC VLIW_EF_EXCEPCION
#define BOVEDA VLIW_EF_BOVEDA
#define ESTADO VLIW_EF_ESTADO
static unsigned comprobaciones, escenarios;
#define EXIGIR(c) do { ++comprobaciones; if (!(c)) { \
    fprintf(stderr,"fallo en línea %d: %s\n",__LINE__,#c); exit(1); } } while (0)

typedef struct {
    VliwInstruccion ops[16];
    VliwDependencia aristas[64];
    VliwBloque bloques[16];
    VliwDestino destinos[16];
    size_t sucesores[16][16];
    char textos[16][160];
    VliwEntrada entrada;
    VliwDestinos tabla;
} Fixture;

/* Prepara almacenamiento propio y mutable para que memcmp pueda detectar
 * cualquier escritura del validador, incluso sobre texto, sucesores o punteros. */
static void iniciar(Fixture *f)
{
    memset(f,0,sizeof *f);
    f->entrada=(VliwEntrada){f->ops,0,f->aristas,0,f->bloques,0,true};
    f->tabla=(VliwDestinos){f->destinos,0};
    for (size_t b=0;b<16;++b) f->bloques[b].sucesores=f->sucesores[b];
}

/* Añade una operación con máscaras/efectos especificados por la prueba. No
 * obtiene metadatos del validador ni actúa como productor del compilador. */
static void operacion(Fixture *f, unsigned slot, const char *texto,
    uint64_t lee, uint64_t escribe, unsigned efectos)
{
    size_t i=f->entrada.cantidad_instrucciones++;
    EXIGIR(i<16);
    strcpy(f->textos[i],texto);
    f->ops[i]=(VliwInstruccion){1000+i,(VliwSlot)slot,f->textos[i],lee,escribe,efectos,VLIW_CONTROL_NINGUNO,0};
    f->entrada.cantidad_bloques=1; f->bloques[0].fin=i+1;
}

/* Compara el resultado con un oráculo externo explícito y verifica la copia
 * completa del fixture. En rechazos también exige diagnóstico identificable. */
static void esperar(Fixture *f, VliwIntegracionEstado esperado)
{
    unsigned char anterior[sizeof *f]; memcpy(anterior,f,sizeof *f);
    VliwIntegracionDiagnostico d;
    memset(&d,0xa5,sizeof d);
    VliwIntegracionEstado estado=vliw_integracion_validar(&f->entrada,&f->tabla,&d);
    if (estado!=esperado)
        fprintf(stderr,"escenario %u: código %d, esperado %d; %s\n",escenarios,(int)estado,(int)esperado,d.mensaje);
    EXIGIR(estado==esperado); EXIGIR(d.estado==estado); EXIGIR(d.mensaje && *d.mensaje);
    EXIGIR(memcmp(anterior,f,sizeof *f)==0);
    ++escenarios;
}

/* La base de los casos negativos es una suma inmediata física coherente.
 * Cada escenario altera después una sola condición relevante del contrato. */
static void alu(Fixture *f)
{
    iniciar(f); operacion(f,0,"sumai x4,x0,7",0,R(4),0);
}

/* Prepara un salto al propio bloque: backedge CFG permitido, sin ciclo DAG.
 * El destino textual es un símbolo lógico, no una dirección ya relocalizada. */
static void salto(Fixture *f)
{
    iniciar(f); operacion(f,2,"sye x0,objetivo",0,0,0);
    f->ops[0].control=VLIW_CONTROL_SALTO;
    f->sucesores[0][0]=0; f->bloques[0].cantidad_sucesores=1;
    f->destinos[0]=(VliwDestino){0,VLIW_DEST_DIRECTO,0,VAC};f->tabla.cantidad=1;
}

/* Cubre las 34 instrucciones con operandos y máscaras escritos manualmente.
 * Detecta confundir store con escritura GPR, perder un destino Feistel o
 * aceptar nombres virtuales solo porque las demás estructuras parecen válidas. */
static void prueba_isa(void)
{
    static const struct { const char *texto; unsigned slot; uint64_t lee,escribe; unsigned efectos; } casos[]={
        {"suma x4,x5,x6",0,R(5)|R(6),R(4),0}, {"resta x4,x5,x6",0,R(5)|R(6),R(4),0},
        {"cader x4,x5,x6",0,R(5)|R(6),R(4),0}, {"cizq x4,x5,x6",0,R(5)|R(6),R(4),0},
        {"and x4,x5,x6",0,R(5)|R(6),R(4),0}, {"or x4,x5,x6",0,R(5)|R(6),R(4),0},
        {"xor x4,x5,x6",0,R(5)|R(6),R(4),0}, {"cder x4,x5,x6",0,R(5)|R(6),R(4),0},
        {"mrq x4,x5,x6",0,R(5)|R(6),R(4),0}, {"myq x4,x5,x6",0,R(5)|R(6),R(4),0},
        {"sumai x4,x5,-1024",0,R(5),R(4),0}, {"restai x4,x5,1023",0,R(5),R(4),0},
        {"cizqi x4,x5,2047",0,R(5),R(4),0}, {"cderi x4,x5,31",0,R(5),R(4),0},
        {"caderi x4,x5,3",0,R(5),R(4),0}, {"xori x4,x5,-1",0,R(5),R(4),0},
        {"andi x4,x5,255",0,R(5),R(4),0}, {"ori x4,x5,127",0,R(5),R(4),0},
        {"guardap x2,x4,-4",1,R(2)|R(4),0,ESCRIBIR}, {"guardab x2,x4,1023",1,R(2)|R(4),0,ESCRIBIR},
        {"cargai x4,x2,8",1,R(2),R(4),LEER}, {"cargabai x4,x2,-1024",1,R(2),R(4),LEER},
        {"igualsi x4,x5,objetivo",2,R(4)|R(5),0,0}, {"igualno x4,x5,objetivo",2,R(4)|R(5),0,0},
        {"menora x4,x5,objetivo",2,R(4)|R(5),0,0}, {"mayoroigual x4,x5,objetivo",2,R(4)|R(5),0,0},
        {"sye x0,objetivo",2,0,0,0}, {"syer x0,x1,0",2,R(1),0,0},
        {"fsl x6,x2,0,1",3,R(2)|R(3),R(6)|R(7),EXC},
        {"fsli x8,x4,3,2",3,R(4)|R(5),R(8)|R(9),EXC},
        {"ell 2,0,x4,x10",3,R(4)|R(10),0,BOVEDA|EXC},
        {"vcr 8192",3,0,0,LEER|ESTADO}, {"camcon 16,7",3,0,0,LEER|ESCRIBIR|BOVEDA|EXC},
        {"setpwd x25",3,R(25),0,BOVEDA|ESTADO|EXC}
    };
    Fixture f;
    for (size_t i=0;i<sizeof casos/sizeof casos[0];++i) {
        iniciar(&f);operacion(&f,casos[i].slot,casos[i].texto,casos[i].lee,casos[i].escribe,casos[i].efectos);
        if (casos[i].slot==2) {
            f.ops[0].control=VLIW_CONTROL_SALTO;f.tabla.cantidad=1;
            f.destinos[0]=(VliwDestino){0,i<26 ? VLIW_DEST_CONDICIONAL : i==26 ? VLIW_DEST_DIRECTO : VLIW_DEST_INDIRECTO,i<27?0:VAC,VAC};
            if (i<27) {f.sucesores[0][0]=0;f.bloques[0].cantidad_sucesores=1;}
        }
        esperar(&f,VLIW_INT_OK);
    }
    // x0 no crea riesgos, pero una suma con destino x0 sigue siendo operación.
    iniciar(&f);operacion(&f,0,"sumai x0,x0,1",R(0),R(0),0);esperar(&f,VLIW_INT_OK);
    iniciar(&f);operacion(&f,0,"suma x31,x31,x0",R(31),R(31),0);esperar(&f,VLIW_INT_OK);
    const char *invalidos[]={"nop","inexistente x4,x0,1","sumai $t1,x0,7","sumai $r1,x0,7","sumai resultado,x0,7","sumai x32,x0,7","sumai x4,x0,99999","sumai x4,x0,7 | nop","etiqueta: sumai x4,x0,7","sumai x4,x0,7\nnop"};
    for (size_t i=0;i<sizeof invalidos/sizeof invalidos[0];++i) {
        alu(&f);strcpy(f.textos[0],invalidos[i]);esperar(&f,VLIW_INT_ISA);
    }
    alu(&f);f.ops[0].slot=VLIW_SLOT_LSU;esperar(&f,VLIW_INT_ISA);
    alu(&f);f.ops[0].lee=R(5);esperar(&f,VLIW_INT_METADATOS);
    alu(&f);f.ops[0].escribe=R(6);esperar(&f,VLIW_INT_METADATOS);
    iniciar(&f);operacion(&f,3,"fsl x6,x2,0,1",R(2)|R(3),R(6),EXC);esperar(&f,VLIW_INT_METADATOS);
    iniciar(&f);operacion(&f,1,"cargai x4,x2,0",R(2),R(4),ESCRIBIR);esperar(&f,VLIW_INT_EFECTOS);
    iniciar(&f);operacion(&f,3,"vcr 0",0,0,LEER);esperar(&f,VLIW_INT_EFECTOS);
    iniciar(&f);operacion(&f,3,"fsl x7,x2,0,1",R(2)|R(3),R(7)|R(8),EXC);esperar(&f,VLIW_INT_ISA);
    alu(&f);f.ops[0].efectos=BOVEDA;esperar(&f,VLIW_INT_EFECTOS);
}

/* Comprueba rechazos antes de recorrer almacenamiento inexistente y particiones
 * incoherentes. Ningún caso usa punteros inventados o regiones inaccesibles. */
static void prueba_estructura(void)
{
    Fixture f; VliwIntegracionDiagnostico d;
    EXIGIR(vliw_integracion_validar(NULL,NULL,&d)==VLIW_INT_ARGUMENTO);++escenarios;
    iniciar(&f);esperar(&f,VLIW_INT_OK);
    alu(&f);EXIGIR(vliw_integracion_validar(&f.entrada,NULL,NULL)==VLIW_INT_OK);++escenarios;
    alu(&f);f.entrada.instrucciones=NULL;esperar(&f,VLIW_INT_ESTRUCTURA);
    alu(&f);f.entrada.bloques=NULL;esperar(&f,VLIW_INT_ESTRUCTURA);
    alu(&f);f.entrada.cantidad_bloques=0;esperar(&f,VLIW_INT_ESTRUCTURA);
    alu(&f);f.entrada.cantidad_dependencias=1;f.entrada.dependencias=NULL;esperar(&f,VLIW_INT_ESTRUCTURA);
    salto(&f);f.tabla.items=NULL;esperar(&f,VLIW_INT_ESTRUCTURA);
    alu(&f);f.entrada.metadatos_completos=false;esperar(&f,VLIW_INT_NO_ACREDITADOS);
    alu(&f);f.ops[0].slot=(VliwSlot)4;esperar(&f,VLIW_INT_SLOT);
    alu(&f);f.ops[0].slot=(VliwSlot)-1;esperar(&f,VLIW_INT_SLOT);
    alu(&f);f.ops[0].lee=R(32);esperar(&f,VLIW_INT_REGISTRO);
    alu(&f);f.ops[0].escribe=UINT64_MAX;esperar(&f,VLIW_INT_REGISTRO);
    alu(&f);f.ops[0].efectos=1u<<20;esperar(&f,VLIW_INT_EFECTOS);
    alu(&f);f.ops[0].control=(VliwControl)20;esperar(&f,VLIW_INT_CFG);
    alu(&f);f.bloques[0].fin=0;esperar(&f,VLIW_INT_CFG);
    alu(&f);f.bloques[0].inicio=1;esperar(&f,VLIW_INT_CFG);
    alu(&f);f.bloques[0].fin=2;esperar(&f,VLIW_INT_CFG);
    alu(&f);f.ops[0].bloque=1;esperar(&f,VLIW_INT_CFG);
    alu(&f);f.bloques[0].cantidad_sucesores=1;f.sucesores[0][0]=1;esperar(&f,VLIW_INT_CFG);
    alu(&f);f.bloques[0].cantidad_sucesores=1;f.bloques[0].sucesores=NULL;esperar(&f,VLIW_INT_CFG);
    // Dos bloques se solapan; otra variante deja una operación sin cubrir.
    alu(&f);operacion(&f,0,"sumai x5,x0,8",0,R(5),0);f.entrada.cantidad_bloques=2;
    f.bloques[1].inicio=1;f.bloques[1].fin=2;esperar(&f,VLIW_INT_CFG);
    alu(&f);operacion(&f,0,"sumai x5,x0,8",0,R(5),0);f.bloques[0].fin=1;esperar(&f,VLIW_INT_CFG);
    alu(&f);operacion(&f,0,"sumai x5,x0,8",0,R(5),0);f.ops[1].id=f.ops[0].id;esperar(&f,VLIW_INT_ESTRUCTURA);
    alu(&f);f.ops[0].texto=NULL;esperar(&f,VLIW_INT_ISA);
    alu(&f);memset(f.textos[0],'a',159);f.textos[0][159]='\0';esperar(&f,VLIW_INT_ISA);
    alu(&f);f.ops[0].id=SIZE_MAX;esperar(&f,VLIW_INT_OK);
}

/* Prepara un productor/consumidor RAW explícito. Distancia3 evita que ID lea
 * un valor anterior a WB, sin usar el resultado del plan como oráculo. */
static void raw(Fixture *f)
{
    alu(f);operacion(f,0,"suma x6,x4,x0",R(4),R(6),0);
    f->aristas[0]=(VliwDependencia){0,1,VLIW_DEP_RAW,3};f->entrada.cantidad_dependencias=1;
}

/* Contrasta el DAG declarado, incluyendo cobertura transitiva, WAR/WAW,
 * memoria y barreras. Un backedge CFG se prueba aparte como caso válido. */
static void prueba_dependencias(void)
{
    Fixture f;raw(&f);esperar(&f,VLIW_INT_OK);
    raw(&f);f.entrada.cantidad_dependencias=0;esperar(&f,VLIW_INT_DEPENDENCIA);
    raw(&f);f.aristas[0].distancia=2;esperar(&f,VLIW_INT_DEPENDENCIA);
    raw(&f);f.aristas[0].distancia=0;esperar(&f,VLIW_INT_DEPENDENCIA);
    raw(&f);f.aristas[0].distancia=SIZE_MAX;esperar(&f,VLIW_INT_CAPACIDAD);
    raw(&f);f.aristas[0].antes=SIZE_MAX;esperar(&f,VLIW_INT_DEPENDENCIA);
    raw(&f);f.aristas[0].despues=2;esperar(&f,VLIW_INT_DEPENDENCIA);
    raw(&f);f.aristas[0]=(VliwDependencia){1,0,VLIW_DEP_RAW,3};esperar(&f,VLIW_INT_DEPENDENCIA);
    raw(&f);f.aristas[0].tipo=(VliwTipoDependencia)99;esperar(&f,VLIW_INT_DEPENDENCIA);
    raw(&f);f.aristas[0].tipo=VLIW_DEP_WAR;esperar(&f,VLIW_INT_DEPENDENCIA);
    raw(&f);f.aristas[1]=f.aristas[0];f.entrada.cantidad_dependencias=2;esperar(&f,VLIW_INT_OK);
    // Dos aristas consecutivas cubren también el riesgo entre primera y tercera.
    raw(&f);operacion(&f,0,"suma x7,x4,x6",R(4)|R(6),R(7),0);
    f.aristas[1]=(VliwDependencia){1,2,VLIW_DEP_RAW,3};f.entrada.cantidad_dependencias=2;esperar(&f,VLIW_INT_OK);
    f.aristas[0].distancia=600;f.aristas[1].distancia=600;esperar(&f,VLIW_INT_CAPACIDAD);
    iniciar(&f);operacion(&f,0,"suma x4,x5,x0",R(5),R(4),0);operacion(&f,0,"sumai x5,x0,1",0,R(5),0);
    f.aristas[0]=(VliwDependencia){0,1,VLIW_DEP_WAR,1};f.entrada.cantidad_dependencias=1;esperar(&f,VLIW_INT_OK);
    f.entrada.cantidad_dependencias=0;esperar(&f,VLIW_INT_DEPENDENCIA);
    alu(&f);operacion(&f,0,"sumai x4,x0,8",0,R(4),0);f.aristas[0]=(VliwDependencia){0,1,VLIW_DEP_WAW,1};f.entrada.cantidad_dependencias=1;esperar(&f,VLIW_INT_OK);
    f.entrada.cantidad_dependencias=0;esperar(&f,VLIW_INT_DEPENDENCIA);
    iniciar(&f);operacion(&f,1,"guardap x0,x4,0",R(4),0,ESCRIBIR);operacion(&f,1,"cargai x5,x0,0",0,R(5),LEER);
    f.aristas[0]=(VliwDependencia){0,1,VLIW_DEP_MEMORIA,1};f.entrada.cantidad_dependencias=1;esperar(&f,VLIW_INT_OK);
    f.entrada.cantidad_dependencias=0;esperar(&f,VLIW_INT_DEPENDENCIA);
    alu(&f);operacion(&f,3,"setpwd x25",R(25),0,BOVEDA|ESTADO|EXC);
    f.aristas[0]=(VliwDependencia){0,1,VLIW_DEP_BARRERA,3};f.entrada.cantidad_dependencias=1;esperar(&f,VLIW_INT_OK);
    f.aristas[0].distancia=1;esperar(&f,VLIW_INT_DEPENDENCIA);
}

/* Ejercita tomado/caída, llamadas y retornos con CFG explícitos. Las
 * direcciones indirectas permanecen externas; aceptarlas no las relocaliza. */
static void prueba_destinos(void)
{
    Fixture f;salto(&f);esperar(&f,VLIW_INT_OK);
    salto(&f);f.tabla.cantidad=0;esperar(&f,VLIW_INT_DESTINO);
    salto(&f);f.destinos[0].tipo=VLIW_DEST_NO_RESUELTO;esperar(&f,VLIW_INT_NO_RESUELTO);
    salto(&f);f.destinos[0].tipo=(VliwDestinoTipo)99;esperar(&f,VLIW_INT_DESTINO);
    salto(&f);f.destinos[0].tomado=1;esperar(&f,VLIW_INT_DESTINO);
    salto(&f);f.destinos[0].caida=0;esperar(&f,VLIW_INT_DESTINO);
    salto(&f);f.destinos[0].instruccion=SIZE_MAX;esperar(&f,VLIW_INT_DESTINO);
    salto(&f);f.destinos[0].tipo=VLIW_DEST_CONDICIONAL;esperar(&f,VLIW_INT_DESTINO);
    salto(&f);f.bloques[0].cantidad_sucesores=0;esperar(&f,VLIW_INT_DESTINO);
    salto(&f);strcpy(f.textos[0],"sye x0,$L1");esperar(&f,VLIW_INT_ISA);
    salto(&f);strcpy(f.textos[0],"sye x0,0xzz");esperar(&f,VLIW_INT_ISA);
    // Una llamada indirecta declara efectos desconocidos y continuación real.
    iniciar(&f);operacion(&f,2,"syer x1,x13,0",R(13),R(1),VLIW_EF_DESCONOCIDOS);
    operacion(&f,0,"sumai x4,x0,7",0,R(4),0);
    f.ops[0].control=VLIW_CONTROL_LLAMADA;f.ops[1].bloque=1;
    f.entrada.cantidad_bloques=2;f.bloques[0].fin=1;f.bloques[1].inicio=1;f.bloques[1].fin=2;
    f.bloques[0].cantidad_sucesores=1;f.sucesores[0][0]=1;
    f.destinos[0]=(VliwDestino){0,VLIW_DEST_INDIRECTO,VAC,1};f.tabla.cantidad=1;
    f.aristas[0]=(VliwDependencia){0,1,VLIW_DEP_BARRERA,3};f.entrada.cantidad_dependencias=1;
    esperar(&f,VLIW_INT_OK);
    f.ops[0].efectos=0;esperar(&f,VLIW_INT_EFECTOS);f.ops[0].efectos=VLIW_EF_DESCONOCIDOS;
    f.destinos[0].caida=VAC;esperar(&f,VLIW_INT_DESTINO);f.destinos[0].caida=1;
    f.destinos[1]=f.destinos[0];f.tabla.cantidad=2;esperar(&f,VLIW_INT_DESTINO);f.tabla.cantidad=1;
    f.bloques[0].cantidad_sucesores=2;f.sucesores[0][1]=1;esperar(&f,VLIW_INT_CFG);
    // RET es SYER real; no se inventa un destino estático para el valor de ra.
    iniciar(&f);operacion(&f,2,"ret",R(1),0,0);f.ops[0].control=VLIW_CONTROL_RETORNO;
    f.tabla.cantidad=1;f.destinos[0]=(VliwDestino){0,VLIW_DEST_RETORNO,VAC,VAC};esperar(&f,VLIW_INT_OK);
    f.destinos[0].tomado=0;esperar(&f,VLIW_INT_DESTINO);
    salto(&f);operacion(&f,0,"sumai x4,x0,7",0,R(4),0);esperar(&f,VLIW_INT_CFG);
}

/* Revisa cotas previas al recorrido y la diferencia entre capacidad local
 * necesaria y tamaño de plan. La prueba grande usa arreglos realmente reservados. */
static void prueba_limites(void)
{
    Fixture f;alu(&f);f.entrada.cantidad_instrucciones=SIZE_MAX;esperar(&f,VLIW_INT_CAPACIDAD);
    alu(&f);f.entrada.cantidad_dependencias=SIZE_MAX;esperar(&f,VLIW_INT_ESTRUCTURA);
    alu(&f);f.entrada.cantidad_bloques=SIZE_MAX;esperar(&f,VLIW_INT_ESTRUCTURA);
    alu(&f);f.tabla.cantidad=SIZE_MAX;esperar(&f,VLIW_INT_ESTRUCTURA);
    raw(&f);f.aristas[0].distancia=1022;esperar(&f,VLIW_INT_OK);
    f.aristas[0].distancia=1023;esperar(&f,VLIW_INT_CAPACIDAD);
    size_t n=VLIW_MAX_BUNDLES*(size_t)VLIW_SLOTS;
    VliwInstruccion *ops=calloc(n,sizeof *ops),*antes=calloc(n,sizeof *antes);
    EXIGIR(ops && antes);
    char texto[]="sumai x0,x0,1";
    for (size_t i=0;i<n;++i) ops[i]=(VliwInstruccion){i,VLIW_SLOT_ALU,texto,0,0,0,VLIW_CONTROL_NINGUNO,0};
    memcpy(antes,ops,n*sizeof *ops);VliwBloque bloque={0,n,NULL,0};
    VliwEntrada e={ops,n,NULL,0,&bloque,1,true};
    EXIGIR(vliw_integracion_validar(&e,NULL,NULL)==VLIW_INT_OK);++escenarios;
    EXIGIR(memcmp(antes,ops,n*sizeof *ops)==0);EXIGIR(!strcmp(texto,"sumai x0,x0,1"));
    // 4092 operaciones ALU no caben en1023 bundles: el validador no promete un
    // plan, y S7 conserva la decisión final de capacidad sin cambiar su API.
    VliwPlan *plan=NULL;EXIGIR(vliw_planificar(&e,&plan,NULL)==VLIW_CAPACIDAD);EXIGIR(plan==NULL);
    free(antes);free(ops);
}

/* Recorre la cadena real sin BAL. Inspecciona el ASM con CB y S2 y cuenta
 * operaciones/posiciones vacías sin reutilizar el selector ni el validador. */
static void cadena(Fixture *f, size_t operaciones_esperadas)
{
    unsigned char anterior[sizeof *f];memcpy(anterior,f,sizeof *f);
    esperar(f,VLIW_INT_OK);VliwPlan *p=NULL;VliwDiagnostico sd;
    EXIGIR(vliw_planificar(&f->entrada,&p,&sd)==VLIW_OK);
    EXIGIR(vliw_verificar(&f->entrada,p,&sd)==VLIW_OK);
    char *asm_texto=NULL;VliwEmisorDiagnostico ed;
    EXIGIR(vliw_emitir(&f->entrada,p,&f->tabla,&asm_texto,&ed)==VLIW_EM_OK);
    // El vacío de S8-A es una cadena liberable sin código. CB lo rechaza por
    // contrato: se comprueba aquí esa diferencia sin exigir un bundle ficticio.
    if (!operaciones_esperadas) {
        EXIGIR(!*asm_texto && p->cantidad_bundles==0);
        EXIGIR(memcmp(anterior,f,sizeof *f)==0);
        vliw_emisor_liberar(&asm_texto);vliw_liberar(&p);
        EXIGIR(!asm_texto && !p);return;
    }
    CbPrograma cb={0};EXIGIR(cb_ensamblar(asm_texto,&cb,stderr)==0);
    EXIGIR(cb.cantidad==p->cantidad_bundles);size_t operaciones=0;
    for (size_t q=0;q<cb.cantidad;++q) for (unsigned s=0;s<4;++s) {
        size_t i=p->bundles[q].instrucciones[s];SimInstruccion r;
        if (i==VAC) EXIGIR(cb.bundles[q].slots[s]==0);
        else {EXIGIR(sim_decode_palabra(cb.bundles[q].slots[s],s,&r)==SIM_DECODE_OK);++operaciones;}
    }
    EXIGIR(operaciones==operaciones_esperadas);EXIGIR(memcmp(anterior,f,sizeof *f)==0);
    cb_liberar(&cb);vliw_emisor_liberar(&asm_texto);vliw_liberar(&p);
    EXIGIR(!asm_texto && !p);
}

/* Fija expectativas externas del empaquetado ALU+LSU con consumidor RAW,
 * además de backedge, CRIPTO y vacío, sin afirmar equivalencia arquitectónica. */
static void prueba_integracion(void)
{
    Fixture f;alu(&f);operacion(&f,1,"cargai x8,x0,0",0,R(8),LEER);
    operacion(&f,0,"suma x6,x4,x8",R(4)|R(8),R(6),0);
    f.aristas[0]=(VliwDependencia){0,2,VLIW_DEP_RAW,3};f.aristas[1]=(VliwDependencia){1,2,VLIW_DEP_RAW,3};f.entrada.cantidad_dependencias=2;
    cadena(&f,3);VliwPlan *p=NULL;EXIGIR(vliw_planificar(&f.entrada,&p,NULL)==VLIW_OK);
    EXIGIR(p->cantidad_bundles==4);EXIGIR(p->cantidad_vacios==2);
    EXIGIR(p->bundle_por_instruccion[0]==0 && p->bundle_por_instruccion[1]==0 && p->bundle_por_instruccion[2]==3);
    // La validación de entrada no reemplaza la validación de un plan alterado.
    p->bundles[0].instrucciones[0]=2;EXIGIR(vliw_verificar(&f.entrada,p,NULL)==VLIW_PLAN_INVALIDO);vliw_liberar(&p);
    salto(&f);cadena(&f,1);
    // Una condición puede tener tomado igual a caída: son un único sucesor,
    // aunque S8-A deba conservar ambas interpretaciones del destino lógico.
    iniciar(&f);operacion(&f,2,"igualsi x0,x0,siguiente",0,0,0);
    operacion(&f,0,"sumai x4,x0,7",0,R(4),0);
    f.ops[0].control=VLIW_CONTROL_SALTO;f.ops[1].bloque=1;
    f.entrada.cantidad_bloques=2;f.bloques[0].fin=1;
    f.bloques[1].inicio=1;f.bloques[1].fin=2;
    f.bloques[0].cantidad_sucesores=1;f.sucesores[0][0]=1;
    f.destinos[0]=(VliwDestino){0,VLIW_DEST_CONDICIONAL,1,1};f.tabla.cantidad=1;
    f.aristas[0]=(VliwDependencia){0,1,VLIW_DEP_CONTROL,1};f.entrada.cantidad_dependencias=1;
    cadena(&f,2);
    iniciar(&f);operacion(&f,3,"setpwd x25",R(25),0,BOVEDA|ESTADO|EXC);cadena(&f,1);
    iniciar(&f);cadena(&f,0);
}

#ifdef INTEGRACION_FALLOS
static bool vigilar;
static long posicion,fallo=-1;
static unsigned fallo_io;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
FILE *__real_tmpfile(void);
int __real_fflush(FILE *);
int __real_fseek(FILE *,long,int);
int __real_fclose(FILE *);

/* Decide qué reserva real se interrumpe, sin reservar para el propio monitor.
 * Solo se activa alrededor de la llamada probada para no afectar sus oráculos. */
static bool interrumpir(void) { return vigilar && posicion++==fallo; }
/* Intercepta malloc conservando su contrato normal salvo en la posición elegida. */
void *__wrap_malloc(size_t n) { return interrumpir()?NULL:__real_malloc(n); }
/* Intercepta calloc para cubrir las reservas internas de CB y su limpieza parcial. */
void *__wrap_calloc(size_t n,size_t t) { return interrumpir()?NULL:__real_calloc(n,t); }
/* Conserva la reserva anterior al fallar realloc, como exige su contrato C. */
void *__wrap_realloc(void *p,size_t n) { return interrumpir()?NULL:__real_realloc(p,n); }
/* Simula que no puede crearse el flujo privado de diagnóstico. */
FILE *__wrap_tmpfile(void) { return fallo_io==1?NULL:__real_tmpfile(); }
/* Fuerza el error de sincronización; el llamador todavía debe cerrar el flujo. */
int __wrap_fflush(FILE *f) { return fallo_io==2?EOF:__real_fflush(f); }
/* Fuerza fallo de posicionamiento antes de leer el diagnóstico. */
int __wrap_fseek(FILE *f,long o,int w) { return fallo_io==3?-1:__real_fseek(f,o,w); }
/* Cierra realmente el recurso incluso cuando comunica un error de cierre. */
int __wrap_fclose(FILE *f) { int r=__real_fclose(f);return fallo_io==4?EOF:r; }

/* Repite cada posición de reserva observada y los cuatro fallos de flujo.
 * Cada rechazo conserva el fixture; después debe poder validarse de nuevo. */
static void prueba_recursos(void)
{
    Fixture f;alu(&f);vigilar=true;posicion=0;fallo=-1;esperar(&f,VLIW_INT_OK);
    long cantidad=posicion;vigilar=false;EXIGIR(cantidad>0);
    for (long i=0;i<cantidad;++i) {
        posicion=0;fallo=i;vigilar=true;esperar(&f,VLIW_INT_MEMORIA);
        vigilar=false;esperar(&f,VLIW_INT_OK);
    }
    for (unsigned i=1;i<=4;++i) {
        fallo_io=i;esperar(&f,VLIW_INT_IO);fallo_io=0;esperar(&f,VLIW_INT_OK);
    }
    printf("reservas=%ld; fallos_io=4\n",cantidad);
}
#endif

/* Selecciona un grupo independiente y emite cantidades verificables. Un
 * argumento desconocido falla; no omite silenciosamente escenarios solicitados. */
int main(int argc,char **argv)
{
    if (argc!=2) return 2;
    if (!strcmp(argv[1],"isa")) prueba_isa();
    else if (!strcmp(argv[1],"estructura")) prueba_estructura();
    else if (!strcmp(argv[1],"dependencias")) prueba_dependencias();
    else if (!strcmp(argv[1],"destinos")) prueba_destinos();
    else if (!strcmp(argv[1],"limites")) prueba_limites();
    else if (!strcmp(argv[1],"integracion")) prueba_integracion();
#ifdef INTEGRACION_FALLOS
    else if (!strcmp(argv[1],"recursos")) prueba_recursos();
#endif
    else return 2;
    printf("PASS %s: escenarios=%u; comprobaciones=%u\n",argv[1],escenarios,comprobaciones);
    return 0;
}
