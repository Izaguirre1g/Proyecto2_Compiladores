/* Oráculos físicos del DAG: ejemplos manuales y cierre Floyd independiente.
 * Usa consumidores reales para la cadena completa; no representa un backend BAL.
 * Comprueba entradas intactas, errores, capacidad y todas las reservas observadas. */
#include "vliw_dag_fisico.h"
#include "vliw_integracion.h"
#include "codificacion_binaria.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define VAC VLIW_VACIO
#define R(x) (UINT64_C(1)<<(x))
#define MEM (VLIW_EF_LEE_MEMORIA|VLIW_EF_ESCRIBE_MEMORIA)
static size_t escenarios,comprobaciones;
#define EXIGIR(x) do {++comprobaciones;if(!(x)){fprintf(stderr,"línea %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#ifdef DAG_FALLOS
static size_t llamadas,fallar;static bool activo;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
/* Intercepta solo reservas activadas alrededor de la API. Preparar fixtures
 * y oráculos queda fuera: un fallo mide limpieza del constructor real. */
void *__wrap_malloc(size_t n){if(activo && ++llamadas==fallar)return NULL;return __real_malloc(n);}
/* CFG y matriz usan calloc; un resultado parcialmente vacío debe liberarse. */
void *__wrap_calloc(size_t n,size_t s){if(activo && ++llamadas==fallar)return NULL;return __real_calloc(n,s);}
/* El crecimiento conserva aristas anteriores; fallar realloc no debe perderlas. */
void *__wrap_realloc(void *p,size_t n){if(activo && ++llamadas==fallar)return NULL;return __real_realloc(p,n);}
#endif
typedef struct {
    VliwCfgEntrada flujo;VliwDagEntrada e;VliwCfg *cfg;
    VliwCfgOperacion op[16];VliwMetadatosParciales meta[16];unsigned extra[16];
    VliwCfgFuncion fn[3];VliwCfgSimbolo s[16];VliwCfgTransferencia t[16];
    size_t fronteras[16],posibles[16];const char *texto[16];
} Fixture;
/* Prepara tablas propias con generación17 e identidades no consecutivas.
 * Por defecto las operaciones ALU son independientes y no tocan x0 como destino. */
static void iniciar(Fixture *f,size_t n)
{
    memset(f,0,sizeof *f);
    f->flujo=(VliwCfgEntrada){.operaciones=f->op,.cantidad_operaciones=n,
        .funciones=f->fn,.cantidad_funciones=n?1:0,.simbolos=f->s,
        .transferencias=f->t,.fronteras=f->fronteras,.generacion=17,
        .generacion_referencias=17,.fronteras_acreditadas=true};
    f->fn[0]=(VliwCfgFuncion){10,0,n};
    f->e=(VliwDagEntrada){&f->flujo,NULL,f->meta,f->extra,n,17,0,true,true};
    for(size_t i=0;i<n;++i) f->op[i]=(VliwCfgOperacion){100+7*i,
        (UINT32_C(0x40)<<25)|((uint32_t)(4+i)<<11)|7,VLIW_SLOT_ALU,SIM_SALTO_NINGUNO,VLIW_CONTROL_NINGUNO};
}
/* CB codifica únicamente la operación explícita en su slot. No calcula DAG
 * ni resultados esperados: máscaras y distancias se contrastan por separado. */
static void texto(Fixture *f,size_t i,unsigned slot,const char *s)
{
    const char *ss[4]={"nop","nop","nop","nop"};ss[slot]=s;char asm_texto[512];
    snprintf(asm_texto,sizeof asm_texto,"%s | %s | %s | %s\n",ss[0],ss[1],ss[2],ss[3]);
    CbPrograma cb={0};EXIGIR(cb_ensamblar(asm_texto,&cb,stderr)==0);EXIGIR(cb.cantidad==1);
    f->op[i].palabra=cb.bundles[0].slots[slot];f->op[i].slot=(VliwSlot)slot;f->texto[i]=s;
    SimInstruccion real;EXIGIR(sim_decode_palabra(f->op[i].palabra,slot,&real)==SIM_DECODE_OK);
    f->op[i].salto=real.salto;cb_liberar(&cb);
}
/* Extrae metadatos reales y construye CFG antes de la prueba DAG. La salida
 * de ambas etapas es evidencia física local, no completitud automática S7. */
static void preparar(Fixture *f)
{
    for(size_t i=0;i<f->flujo.cantidad_operaciones;++i)
        EXIGIR(vliw_metadatos_extraer(f->op[i].palabra,(unsigned)f->op[i].slot,&f->meta[i],NULL)==SIM_DECODE_OK);
    EXIGIR(vliw_cfg_construir(&f->flujo,&f->cfg,NULL)==VLIW_CFG_OK);f->e.cfg=f->cfg;
}
/* Contrasta byte a byte la entrada y las tablas CFG anidadas. Los bloques y
 * sucesores no pertenecen al fixture fijo, por eso se copian expresamente. */
static VliwDag *ejecutar(Fixture *f,VliwDagEstado esperado)
{
    Fixture antes;memcpy(&antes,f,sizeof antes);
    VliwCfg c=*f->cfg;VliwBloque bloques[16];size_t mapa[16],suc[16][16];VliwDestino destinos[16];
    size_t n=c.cantidad_operaciones,nb=c.cantidad_bloques,nt=c.cantidad_destinos;
    if(nb)memcpy(bloques,c.bloques,nb*sizeof *bloques);
    if(n)memcpy(mapa,c.bloque_por_operacion,n*sizeof *mapa);
    if(nt)memcpy(destinos,c.destinos,nt*sizeof *destinos);
    for(size_t b=0;b<nb;++b)if(c.bloques[b].cantidad_sucesores)
        memcpy(suc[b],c.bloques[b].sucesores,c.bloques[b].cantidad_sucesores*sizeof(size_t));
    VliwDag *g=NULL;VliwDagDiagnostico d;
    VliwDagEstado e=vliw_dag_construir(&f->e,&g,&d);
    if(e!=esperado)fprintf(stderr,"escenario %zu: %d != %d; %s\n",escenarios,e,esperado,d.mensaje);
    EXIGIR(e==esperado);EXIGIR(d.estado==e);EXIGIR(d.mensaje && *d.mensaje);
    EXIGIR(!memcmp(&antes,f,sizeof antes));EXIGIR(!memcmp(&c,f->cfg,sizeof c));
    if(nb)EXIGIR(!memcmp(bloques,c.bloques,nb*sizeof *bloques));
    if(n)EXIGIR(!memcmp(mapa,c.bloque_por_operacion,n*sizeof *mapa));
    if(nt)EXIGIR(!memcmp(destinos,c.destinos,nt*sizeof *destinos));
    for(size_t b=0;b<nb;++b)if(c.bloques[b].cantidad_sucesores)
        EXIGIR(!memcmp(suc[b],c.bloques[b].sucesores,c.bloques[b].cantidad_sucesores*sizeof(size_t)));
    EXIGIR((g!=NULL)==(e==VLIW_DAG_OK));++escenarios;return g;
}
/* Finaliza un escenario sin liberar arreglos prestados; los destructores
 * pueden repetirse porque vacían el doble puntero del llamador. */
static void terminar(Fixture *f,VliwDag **g)
{
    vliw_dag_liberar(g);EXIGIR(!*g);vliw_dag_liberar(g);vliw_cfg_liberar(&f->cfg);
}
/* Busca una arista concreta contra constantes del oráculo manual. No usa
 * la función privada riesgo ni deriva el tipo desde la salida producida. */
static void arista(const VliwDag *g,size_t i,size_t j,VliwTipoDependencia tipo,size_t distancia)
{
    size_t a=0;for(;a<g->cantidad_dependencias;++a)if(g->dependencias[a].antes==i && g->dependencias[a].despues==j)break;
    EXIGIR(a<g->cantidad_dependencias);EXIGIR(g->dependencias[a].tipo==tipo);EXIGIR(g->dependencias[a].distancia==distancia);
}
/* Define el oráculo global independiente con las reglas públicas de I1-A1.
 * No decide qué aristas eliminar: Floyd reconstruye TODOS los caminos después. */
static void cobertura(const Fixture *f,const VliwDag *g)
{
    size_t n=f->flujo.cantidad_operaciones;size_t d[16][16]={{0}};
    bool fuerte[16]={false};unsigned ef[16]={0};size_t critico=0;
    for(size_t i=0;i<n;++i) {
        ef[i]=f->meta[i].efectos_minimos|f->extra[i];
        fuerte[i]=f->op[i].slot==VLIW_SLOT_CRIPTO || (ef[i] & ~MEM) ||
            f->op[i].control==VLIW_CONTROL_LLAMADA || f->op[i].control==VLIW_CONTROL_RETORNO;
    }
    for(size_t a=0;a<g->cantidad_dependencias;++a) {
        const VliwDependencia *ar=&g->dependencias[a];size_t i=ar->antes,j=ar->despues;
        EXIGIR(i<j && j<n);EXIGIR(ar->distancia==1 || ar->distancia==3);
        bool real=false;
        switch(ar->tipo) {
        case VLIW_DEP_RAW:real=(f->meta[i].escribe & f->meta[j].lee)!=0;EXIGIR(ar->distancia==3);break;
        case VLIW_DEP_WAR:real=(f->meta[i].lee & f->meta[j].escribe)!=0;break;
        case VLIW_DEP_WAW:real=(f->meta[i].escribe & f->meta[j].escribe)!=0;break;
        case VLIW_DEP_MEMORIA:real=(ef[i]&MEM) && (ef[j]&MEM);break;
        case VLIW_DEP_CONTROL:real=f->op[i].control || f->op[j].control;break;
        case VLIW_DEP_BARRERA:real=fuerte[i] || fuerte[j];EXIGIR(ar->distancia==3);break;
        }
        EXIGIR(real);EXIGIR(!d[i][j]);d[i][j]=ar->distancia;
    }
    // Floyd usa un orden distinto al constructor incremental. Solo concatena
    // caminos existentes; una suma con tramo ausente no acredita cobertura.
    for(size_t k=0;k<n;++k)for(size_t i=0;i<n;++i)for(size_t j=0;j<n;++j)
        if(d[i][k] && d[k][j] && d[i][j]<d[i][k]+d[k][j])d[i][j]=d[i][k]+d[k][j];
    for(size_t i=0;i<n;++i)for(size_t j=i+1;j<n;++j) {
        size_t minimo=0;
        if((f->meta[i].lee&f->meta[j].escribe) || (f->meta[i].escribe&f->meta[j].escribe) ||
           ((ef[i]&MEM)&&(ef[j]&MEM)) || f->op[i].control || f->op[j].control)minimo=1;
        if((f->meta[i].escribe&f->meta[j].lee) || fuerte[i] || fuerte[j])minimo=3;
        EXIGIR(d[i][j]>=minimo);if(d[i][j]>critico)critico=d[i][j];
    }
    EXIGIR(critico==g->camino_critico);EXIGIR(g->cantidad_operaciones==n);EXIGIR(g->generacion==17);
}
/* Vacío y componentes independientes no adquieren restricciones ficticias.
 * x0 sigue siendo operación, pero nunca una fuente de RAW/WAR/WAW. */
static void basicos(void)
{
    Fixture f;VliwDag *g;
    for(size_t n=0;n<=8;++n){iniciar(&f,n);preparar(&f);g=ejecutar(&f,VLIW_DAG_OK);
        EXIGIR(g->cantidad_dependencias==0);cobertura(&f,g);terminar(&f,&g);}
    iniciar(&f,2);texto(&f,0,0,"sumai x0,x0,7");texto(&f,1,0,"suma x4,x0,x0");preparar(&f);
    g=ejecutar(&f,VLIW_DAG_OK);EXIGIR(!g->cantidad_dependencias);EXIGIR(!f.meta[0].lee && !f.meta[0].escribe);
    cobertura(&f,g);terminar(&f,&g);vliw_dag_liberar(NULL);
    // Leer/escribir x1 en ALU no acredita llamada o retorno: debe seguir sin
    // barreras ABI, aunque el ABI tradicional utilice ese registro como ra.
    iniciar(&f,2);texto(&f,0,0,"sumai x1,x0,7");texto(&f,1,0,"sumai x4,x1,1");preparar(&f);
    f.op[0].id=SIZE_MAX;f.e.abi_acreditado=false;g=ejecutar(&f,VLIW_DAG_OK);arista(g,0,1,VLIW_DEP_RAW,3);cobertura(&f,g);
    // El resultado posee sus aristas: debe sobrevivir a las tablas prestadas.
    vliw_cfg_liberar(&f.cfg);memset(&f,0,sizeof f);arista(g,0,1,VLIW_DEP_RAW,3);vliw_dag_liberar(&g);
}
/* Pares manuales distinguen RAW3, WAR1 y WAW1, incluidas causas simultáneas.
 * La primera escritura no puede desaparecer por existir una redefinición. */
static void registros(void)
{
    const char *a[]={"sumai x4,x0,7","sumai x5,x4,1","sumai x4,x0,7","sumai x4,x5,1","sumai x4,x4,1"};
    const char *b[]={"sumai x5,x4,1","sumai x4,x0,7","sumai x4,x0,9","sumai x5,x4,1","sumai x4,x4,1"};
    const VliwTipoDependencia tipos[]={VLIW_DEP_RAW,VLIW_DEP_WAR,VLIW_DEP_WAW,VLIW_DEP_RAW,VLIW_DEP_RAW};
    for(size_t k=0;k<5;++k){Fixture f;iniciar(&f,2);texto(&f,0,0,a[k]);texto(&f,1,0,b[k]);preparar(&f);
        VliwDag *g=ejecutar(&f,VLIW_DAG_OK);EXIGIR(g->cantidad_dependencias==1);
        arista(g,0,1,tipos[k],tipos[k]==VLIW_DEP_RAW?3:1);cobertura(&f,g);terminar(&f,&g);}
    Fixture f;iniciar(&f,3);texto(&f,0,0,"sumai x4,x0,7");texto(&f,1,0,"sumai x4,x0,8");texto(&f,2,0,"suma x6,x4,x4");
    preparar(&f);VliwDag *g=ejecutar(&f,VLIW_DAG_OK);
    EXIGIR(g->cantidad_dependencias==2);arista(g,0,1,VLIW_DEP_WAW,1);arista(g,1,2,VLIW_DEP_RAW,3);
    EXIGIR(g->camino_critico==4);cobertura(&f,g);terminar(&f,&g);
    // Dos consumidores y una frontera explícita ejercitan riesgos entre
    // bloques; el mapa CFG no debe limitar el análisis a cada bloque aislado.
    iniciar(&f,4);texto(&f,0,0,"sumai x4,x0,7");texto(&f,1,0,"sumai x5,x4,1");
    texto(&f,2,0,"sumai x6,x4,2");texto(&f,3,0,"suma x7,x5,x6");f.fronteras[0]=2;f.flujo.cantidad_fronteras=1;
    preparar(&f);g=ejecutar(&f,VLIW_DAG_OK);arista(g,0,2,VLIW_DEP_RAW,3);EXIGIR(f.cfg->cantidad_bloques==2);
    cobertura(&f,g);terminar(&f,&g);
}
/* Todas las parejas LSU conservan orden aun si ambas leen o usan offsets
 * disjuntos. Tres loads deben reducirse a una cadena, no a un triángulo. */
static void memoria(void)
{
    const char *op[]={"cargai x4,x2,0","cargai x5,x2,12","guardap x2,x6,16","guardab x2,x7,24"};
    for(size_t i=0;i<4;++i)for(size_t j=0;j<4;++j){Fixture f;iniciar(&f,2);texto(&f,0,1,op[i]);texto(&f,1,1,op[j]);preparar(&f);
        VliwDag *g=ejecutar(&f,VLIW_DAG_OK);EXIGIR(g->cantidad_dependencias==1);
        if(i<2 && j<2 && i==j)arista(g,0,1,VLIW_DEP_WAW,1);else arista(g,0,1,VLIW_DEP_MEMORIA,1);
        cobertura(&f,g);terminar(&f,&g);}
    Fixture f;iniciar(&f,3);texto(&f,0,1,"cargai x4,x2,0");texto(&f,1,1,"cargai x5,x2,4");texto(&f,2,1,"cargai x6,x2,8");
    preparar(&f);VliwDag *g=ejecutar(&f,VLIW_DAG_OK);EXIGIR(g->cantidad_dependencias==2);
    arista(g,0,1,VLIW_DEP_MEMORIA,1);arista(g,1,2,VLIW_DEP_MEMORIA,1);EXIGIR(g->camino_critico==2);
    cobertura(&f,g);terminar(&f,&g);
    // El load escribe x4 y el store lo consume; RAW3 domina memoria1.
    iniciar(&f,2);texto(&f,0,1,"cargai x4,x2,0");texto(&f,1,1,"guardap x2,x4,4");preparar(&f);
    g=ejecutar(&f,VLIW_DAG_OK);arista(g,0,1,VLIW_DEP_RAW,3);cobertura(&f,g);terminar(&f,&g);
}
/* Declara un salto directo/condicional a un símbolo lógico dentro del fixture.
 * No infiere destino del inmediato cero codificado por CB antes del layout. */
static void salto(Fixture *f,size_t i,size_t destino,bool cond)
{
    texto(f,i,2,cond?"igualsi x0,x0,0":"sye x0,0");f->op[i].control=VLIW_CONTROL_SALTO;
    size_t id=500+i;f->s[f->flujo.cantidad_simbolos++]=(VliwCfgSimbolo){id,10,destino};
    f->t[f->flujo.cantidad_transferencias++]=(VliwCfgTransferencia){i,cond?VLIW_DEST_CONDICIONAL:VLIW_DEST_DIRECTO,
        id,cond?(i+1<f->flujo.cantidad_operaciones?i+1:VAC):VAC,NULL,0,true};
}
/* Un camino CONTROL1+CONTROL1 no cubre RAW3. El constructor debe retener
 * una arista RAW directa; un backedge CFG jamás genera una arista DAG inversa. */
static void control(void)
{
    Fixture f;iniciar(&f,3);texto(&f,0,0,"sumai x4,x0,7");salto(&f,1,2,false);texto(&f,2,0,"sumai x5,x4,1");
    preparar(&f);VliwDag *g=ejecutar(&f,VLIW_DAG_OK);EXIGIR(g->cantidad_dependencias==3);
    arista(g,0,1,VLIW_DEP_CONTROL,1);arista(g,1,2,VLIW_DEP_CONTROL,1);arista(g,0,2,VLIW_DEP_RAW,3);
    cobertura(&f,g);terminar(&f,&g);
    for(size_t destino=0;destino<3;++destino){iniciar(&f,3);salto(&f,2,destino,false);preparar(&f);
        g=ejecutar(&f,VLIW_DAG_OK);EXIGIR(g->cantidad_dependencias==2);cobertura(&f,g);terminar(&f,&g);}
    // Dos loads en rutas distintas conservan memoria1; no se introduce un
    // análisis de exclusión de caminos que contradiga la política de I1-A1.
    iniciar(&f,4);salto(&f,0,2,true);texto(&f,1,1,"cargai x4,x2,0");texto(&f,2,1,"cargai x5,x2,4");
    preparar(&f);g=ejecutar(&f,VLIW_DAG_OK);arista(g,1,2,VLIW_DEP_MEMORIA,1);cobertura(&f,g);terminar(&f,&g);
}
/* CRIPTO siempre es barrera3. Los dos destinos Feistel se contrastan con
 * oráculos GPR manuales y generan cobertura aunque solo se lea el segundo. */
static void barreras(void)
{
    const char *ops[]={"fsl x6,x2,0,1","fsli x8,x4,3,2","ell 2,0,x4,x10","vcr 8192","camcon 16,7","setpwd x25"};
    for(size_t k=0;k<6;++k){Fixture f;iniciar(&f,3);texto(&f,1,3,ops[k]);preparar(&f);
        VliwDag *g=ejecutar(&f,VLIW_DAG_OK);arista(g,0,1,VLIW_DEP_BARRERA,3);arista(g,1,2,VLIW_DEP_BARRERA,3);
        EXIGIR(g->cantidad_dependencias==2);cobertura(&f,g);terminar(&f,&g);}
    Fixture f;iniciar(&f,2);texto(&f,0,3,"fsl x30,x2,0,1");texto(&f,1,0,"sumai x4,x31,1");preparar(&f);
    EXIGIR(f.meta[0].escribe==(R(30)|R(31)));EXIGIR(f.meta[0].lee==(R(2)|R(3)));
    VliwDag *g=ejecutar(&f,VLIW_DAG_OK);arista(g,0,1,VLIW_DEP_BARRERA,3);cobertura(&f,g);terminar(&f,&g);
    // WAW sobre el segundo destino Feistel queda cubierto por su barrera real,
    // sin inventar RAW ni reducir la separación al WAW ordinario de un bundle.
    iniciar(&f,2);texto(&f,0,3,"fsl x6,x2,0,1");texto(&f,1,0,"sumai x7,x0,1");preparar(&f);
    g=ejecutar(&f,VLIW_DAG_OK);arista(g,0,1,VLIW_DEP_BARRERA,3);cobertura(&f,g);terminar(&f,&g);
    unsigned extras[]={VLIW_EF_EXCEPCION,VLIW_EF_DESCONOCIDOS,VLIW_EF_BARRERA};
    for(size_t k=0;k<3;++k){iniciar(&f,3);f.extra[1]=extras[k];preparar(&f);g=ejecutar(&f,VLIW_DAG_OK);
        EXIGIR(g->cantidad_dependencias==2);cobertura(&f,g);terminar(&f,&g);}
}
/* Fixture interprocedimental explícito: call0→callee2, continuación1 y
 * retorno2→1. La acreditación ABI es del fixture, no una inferencia de x1.
 * No se declara equivalencia BAL ni se ejecuta un programa compilado real. */
static void llamada(Fixture *f)
{
    iniciar(f,3);texto(f,0,2,"sye x1,0");f->op[0].control=VLIW_CONTROL_LLAMADA;
    salto(f,1,1,false);texto(f,2,2,"syer x0,x1,0");f->op[2].control=VLIW_CONTROL_RETORNO;
    f->fn[0]=(VliwCfgFuncion){10,0,2};f->fn[1]=(VliwCfgFuncion){20,2,3};f->flujo.cantidad_funciones=2;
    f->s[f->flujo.cantidad_simbolos++]=(VliwCfgSimbolo){600,20,2};f->posibles[0]=501;
    f->t[f->flujo.cantidad_transferencias++]=(VliwCfgTransferencia){0,VLIW_DEST_DIRECTO,600,1,NULL,0,true};
    f->t[f->flujo.cantidad_transferencias++]=(VliwCfgTransferencia){2,VLIW_DEST_RETORNO,VAC,VAC,f->posibles,1,true};
    f->extra[0]=VLIW_EF_DESCONOCIDOS;
}
/* Call/ret deben quedar como barreras3 con CFG cíclico válido. Retirar crédito
 * ABI o efecto DESCONOCIDOS provoca rechazo antes de publicar dependencias. */
static void abi(void)
{
    Fixture f;
    // SYER como llamada conserva sus operandos locales (x13 y x1); el
    // destino posible se acredita por símbolo, sin convertirlo a directo.
    llamada(&f);texto(&f,0,2,"syer x1,x13,0");f.op[0].control=VLIW_CONTROL_LLAMADA;
    f.posibles[1]=600;f.t[1].tipo=VLIW_DEST_INDIRECTO;f.t[1].tomado=VAC;
    f.t[1].posibles=&f.posibles[1];f.t[1].cantidad_posibles=1;
    preparar(&f);VliwDag *ind=ejecutar(&f,VLIW_DAG_OK);cobertura(&f,ind);terminar(&f,&ind);
    llamada(&f);preparar(&f);VliwDag *g=ejecutar(&f,VLIW_DAG_OK);
    EXIGIR(g->cantidad_dependencias==2);cobertura(&f,g);terminar(&f,&g);
    llamada(&f);preparar(&f);f.e.abi_acreditado=false;g=ejecutar(&f,VLIW_DAG_INCOMPLETO);terminar(&f,&g);
    llamada(&f);preparar(&f);f.extra[0]=0;g=ejecutar(&f,VLIW_DAG_METADATOS);terminar(&f,&g);
    llamada(&f);preparar(&f);f.meta[0].escribe|=R(8);g=ejecutar(&f,VLIW_DAG_METADATOS);terminar(&f,&g);
}
/* Cambia cada campo local por separado. Rechazar todos evita que una palabra
 * correcta oculte máscaras virtuales, efectos omitidos o metadatos obsoletos. */
static void rechazos(void)
{
    for(unsigned k=0;k<19;++k){Fixture f;iniciar(&f,2);preparar(&f);VliwDagEstado esperado=VLIW_DAG_METADATOS;
        switch(k){
        case 0:f.meta[0].lee=R(32);break;case 1:f.meta[0].escribe=R(63);break;
        case 2:f.meta[0].slot=VLIW_SLOT_LSU;break;case 3:f.meta[0].operacion=SIM_OP_RESTA;break;
        case 4:f.meta[0].familia=SIM_FAMILIA_LSU;break;case 5:f.meta[0].efectos_minimos=VLIW_EF_EXCEPCION;break;
        case 6:f.meta[0].salto=SIM_SALTO_PC;break;case 7:f.meta[0].requiere_auth=true;break;
        case 8:f.meta[0].requiere_init=true;break;case 9:f.meta[0].pendientes=0;break;
        case 10:f.extra[0]=128;break;case 11:f.extra[0]=VLIW_EF_LEE_MEMORIA;break;
        case 12:f.extra[0]=VLIW_EF_BOVEDA;break;case 13:f.extra[0]=VLIW_EF_ESTADO;break;
        case 14:f.e.generacion++;esperado=VLIW_DAG_INCOMPLETO;break;
        case 15:f.e.efectos_acreditados=false;esperado=VLIW_DAG_INCOMPLETO;break;
        case 16:f.e.pendientes=1;esperado=VLIW_DAG_INCOMPLETO;break;
        case 17:f.e.cantidad_metadatos=1;esperado=VLIW_DAG_ARGUMENTO;break;
        default:f.op[0].palabra^=1u<<11;break;
        }
        VliwDag *g=ejecutar(&f,esperado);terminar(&f,&g);
    }
    // Contrastar efectos de un load/store exige conservar sus mínimos S2.
    for(unsigned s=0;s<2;++s){Fixture f;iniciar(&f,1);texto(&f,0,1,s?"guardap x2,x4,0":"cargai x4,x2,0");
        preparar(&f);f.meta[0].efectos_minimos=0;VliwDag *g=ejecutar(&f,VLIW_DAG_METADATOS);terminar(&f,&g);}
    // El CFG puede estar bien formado por sí mismo y aun pertenecer a otro
    // flujo. Se contrastan sus campos, no solo el número de generación.
    for(unsigned k=0;k<16;++k){Fixture f;iniciar(&f,3);salto(&f,1,0,true);preparar(&f);VliwDagEstado esperado=VLIW_DAG_CFG;
        VliwCfg c=*f.cfg;f.e.cfg=&c;size_t mapa[3];memcpy(mapa,c.bloque_por_operacion,sizeof mapa);
        VliwBloque b[2];memcpy(b,c.bloques,sizeof b);VliwDestino t=*c.destinos;
        size_t su[2]={0,1};
        switch(k){case 0:c.cantidad_operaciones=2;break;case 1:c.cantidad_bloques=3;break;
        case 2:mapa[0]=3;c.bloque_por_operacion=mapa;break;case 3:b[0].fin=1;c.bloques=b;break;
        case 4:su[0]=9;b[0].sucesores=su;c.bloques=b;break;
        case 5:t.tomado=9;c.destinos=&t;break;case 6:c.generacion++;esperado=VLIW_DAG_INCOMPLETO;break;
        case 7:c.destinos_dinamicos_acreditados=false;esperado=VLIW_DAG_INCOMPLETO;break;
        case 8:c.cantidad_destinos=0;break;
        case 9:c.bloques=NULL;break;case 10:c.bloque_por_operacion=NULL;break;case 11:c.destinos=NULL;break;
        case 12:b[0].cantidad_sucesores=1;c.bloques=b;break;
        case 13:su[0]=su[1]=0;b[0].sucesores=su;c.bloques=b;break;
        case 14:t.instruccion=0;c.destinos=&t;break;default:t.tipo=VLIW_DEST_RETORNO;c.destinos=&t;break;}
        VliwDag *g=ejecutar(&f,esperado);terminar(&f,&g);
    }
    // Una referencia de entrada modificada obliga a reconstruir y rechazar;
    // índices, control, ISA reservada e IDs duplicados no se aceptan por crédito.
    for(unsigned k=0;k<6;++k){Fixture f;iniciar(&f,2);preparar(&f);
        switch(k){case 0:f.op[0].slot=VLIW_SLOT_LSU;break;case 1:f.op[0].palabra=0;break;
        case 2:f.op[0].control=VLIW_CONTROL_SALTO;break;case 3:f.op[1].id=f.op[0].id;break;
        case 4:f.fronteras[0]=9;f.flujo.cantidad_fronteras=1;break;default:f.flujo.generacion_referencias++;break;}
        VliwDag *g=ejecutar(&f,VLIW_DAG_CFG);terminar(&f,&g);
    }
}
/* Argumentos inválidos se rechazan sin recorrer dimensiones absurdas ni
 * sobrescribir una salida ocupada. No se fabrican punteros inaccesibles. */
static void argumentos(void)
{
    Fixture f;iniciar(&f,1);preparar(&f);VliwDag *g=NULL;VliwDagDiagnostico d;
    EXIGIR(vliw_dag_construir(NULL,&g,&d)==VLIW_DAG_ARGUMENTO);++escenarios;
    EXIGIR(vliw_dag_construir(&f.e,NULL,&d)==VLIW_DAG_ARGUMENTO);++escenarios;
    VliwDag ocupado={0};g=&ocupado;EXIGIR(vliw_dag_construir(&f.e,&g,&d)==VLIW_DAG_ARGUMENTO);
    EXIGIR(g==&ocupado);++escenarios;g=NULL;
    VliwDagEntrada e=f.e;
    for(unsigned k=0;k<4;++k){f.e=e;if(k==0)f.e.flujo=NULL;if(k==1)f.e.cfg=NULL;if(k==2)f.e.metadatos=NULL;if(k==3)f.e.efectos_adicionales=NULL;
        EXIGIR(vliw_dag_construir(&f.e,&g,NULL)==VLIW_DAG_ARGUMENTO);EXIGIR(!g);++escenarios;}
    f.e=e;size_t anterior=f.flujo.cantidad_operaciones;f.flujo.cantidad_operaciones=SIZE_MAX;
    EXIGIR(vliw_dag_construir(&f.e,&g,&d)==VLIW_DAG_CAPACIDAD);EXIGIR(!g);++escenarios;
    f.flujo.cantidad_operaciones=anterior;
    // Un cambio que no altera estructura ni máscaras (solo el inmediato) no
    // puede autenticarse con CFG: documentamos esa obligación externa real.
    f.op[0].palabra^=1;g=ejecutar(&f,VLIW_DAG_OK);terminar(&f,&g);
}
/* Cien flujos deterministas mezclan lecturas, escrituras y barreras. El cierre
 * independiente comprueba todas las parejas y la justificación de cada arista,
 * sin depender de una lista específica ni de decisiones de reducción internas. */
static void combinaciones(void)
{
    uint32_t azar=17;
    for(unsigned caso=0;caso<100;++caso){Fixture f;iniciar(&f,12);
        for(size_t i=0;i<12;++i){azar=azar*1664525u+1013904223u;unsigned rd=(azar>>8)%8,rs=(azar>>16)%8;
            f.op[i].palabra=(UINT32_C(0x40)<<25)|(rs<<16)|(rd<<11)|1;
            if(caso%5==0 && i==5)f.extra[i]=VLIW_EF_BARRERA;
        }
        preparar(&f);VliwDag *g=ejecutar(&f,VLIW_DAG_OK);cobertura(&f,g);terminar(&f,&g);
    }
}
/* Proyecta solo fixtures completos y explícitos. Sus textos, efectos y
 * destinos fueron aportados por la prueba; DAG no activa completitud S7.
 * I1-A1 y S7 reciben exactamente el puntero/contador de aristas generado. */
static void cadena(Fixture *f)
{
    preparar(f);VliwDag *g=ejecutar(f,VLIW_DAG_OK);cobertura(f,g);
    VliwInstruccion in[16];size_t n=f->flujo.cantidad_operaciones;
    for(size_t i=0;i<n;++i)in[i]=(VliwInstruccion){f->op[i].id,f->op[i].slot,f->texto[i],f->meta[i].lee,
        f->meta[i].escribe,f->meta[i].efectos_minimos|f->extra[i],f->op[i].control,f->cfg->bloque_por_operacion[i]};
    VliwEntrada e={in,n,g->dependencias,g->cantidad_dependencias,f->cfg->bloques,f->cfg->cantidad_bloques,true};
    VliwDestinos ds={f->cfg->destinos,f->cfg->cantidad_destinos};VliwIntegracionDiagnostico id;
    VliwDependencia copia[64];EXIGIR(g->cantidad_dependencias<64);
    if(g->cantidad_dependencias)memcpy(copia,g->dependencias,g->cantidad_dependencias*sizeof *copia);
    EXIGIR(e.dependencias==g->dependencias);EXIGIR(vliw_integracion_validar(&e,&ds,&id)==VLIW_INT_OK);
    VliwPlan *p=NULL;VliwDiagnostico sd;EXIGIR(vliw_planificar(&e,&p,&sd)==VLIW_OK);
    EXIGIR(vliw_verificar(&e,p,&sd)==VLIW_OK);char *asm_texto=NULL;VliwEmisorDiagnostico ed;
    VliwEmisorEstado em=vliw_emitir(&e,p,&ds,&asm_texto,&ed);
    if(em)fprintf(stderr,"emisor: %d %s\n",em,ed.mensaje);
    EXIGIR(em==VLIW_EM_OK);CbPrograma cb={0};EXIGIR(cb_ensamblar(asm_texto,&cb,stderr)==0);
    EXIGIR(cb.cantidad==p->cantidad_bundles);
    for(size_t i=0;i<n;++i){SimInstruccion real;
        EXIGIR(sim_decode_palabra(cb.bundles[p->bundle_por_instruccion[i]].slots[in[i].slot],(unsigned)in[i].slot,&real)==SIM_DECODE_OK);
        EXIGIR(real.operacion==f->meta[i].operacion);}
    // VliwDependencia tiene padding entre enum y size_t. Su contrato conserva
    // campos, no bytes de relleno sin inicializar que memcmp podría consultar.
    for(size_t a=0;a<g->cantidad_dependencias;++a){const VliwDependencia *ar=&g->dependencias[a];
        EXIGIR(copia[a].antes==ar->antes);EXIGIR(copia[a].despues==ar->despues);
        EXIGIR(copia[a].tipo==ar->tipo);EXIGIR(copia[a].distancia==ar->distancia);}

    cb_liberar(&cb);vliw_emisor_liberar(&asm_texto);vliw_liberar(&p);terminar(f,&g);
}
/* Cinco cadenas reales prueban RAW/LSU, memoria transitiva, backedge y CRIPTO.
 * El caso ABI es explícito y estático: no certifica preservación de un callee BAL. */
static void integracion(void)
{
    Fixture f;iniciar(&f,2);texto(&f,0,0,"sumai x4,x0,7");texto(&f,1,1,"guardap x2,x4,0");cadena(&f);
    iniciar(&f,3);texto(&f,0,1,"cargai x4,x2,0");texto(&f,1,1,"cargai x5,x2,4");texto(&f,2,1,"guardap x2,x5,8");cadena(&f);
    iniciar(&f,2);texto(&f,0,0,"sumai x4,x0,7");salto(&f,1,0,false);cadena(&f);
    iniciar(&f,2);texto(&f,0,3,"fsl x6,x2,0,1");texto(&f,1,0,"sumai x4,x7,1");cadena(&f);
    llamada(&f);cadena(&f);
}
/* Construye fixtures grandes con almacenamiento real y sin CB para aislar el
 * costo del constructor. Modo0: independientes; modo1: WAW; modo2:31 cadenas.
 * Modo3 añade barreras explícitas. Longitud y aristas se calculan analíticamente. */
static void grande(size_t n,unsigned modo,VliwDagEstado esperado)
{
    VliwCfgOperacion *op=calloc(n,sizeof *op);VliwMetadatosParciales *m=calloc(n,sizeof *m);unsigned *ef=calloc(n,sizeof *ef);
    EXIGIR(op && m && ef);
    for(size_t i=0;i<n;++i){unsigned rd=modo==0?0:modo==1?4:(unsigned)(i%31)+1;
        op[i]=(VliwCfgOperacion){100+7*i,(UINT32_C(0x40)<<25)|(rd<<11)|1,VLIW_SLOT_ALU,SIM_SALTO_NINGUNO,VLIW_CONTROL_NINGUNO};
        if(modo==3)ef[i]=VLIW_EF_BARRERA;
        EXIGIR(vliw_metadatos_extraer(op[i].palabra,0,&m[i],NULL)==SIM_DECODE_OK);}
    VliwCfgFuncion fn={10,0,n};VliwCfgEntrada flujo={.operaciones=op,.cantidad_operaciones=n,.funciones=&fn,
        .cantidad_funciones=1,.generacion=17,.generacion_referencias=17,.fronteras_acreditadas=true};
    VliwCfg *cfg=NULL;EXIGIR(vliw_cfg_construir(&flujo,&cfg,NULL)==VLIW_CFG_OK);
    VliwDagEntrada e={&flujo,cfg,m,ef,n,17,0,true,true};VliwDag *g=NULL;
    clock_t inicio=clock();VliwDagEstado estado=vliw_dag_construir(&e,&g,NULL);
    double segundos=(double)(clock()-inicio)/CLOCKS_PER_SEC;
    EXIGIR(estado==esperado);EXIGIR((g!=NULL)==(estado==VLIW_DAG_OK));++escenarios;
    if(g){size_t ar=modo==0?0:modo==1?n-1:n-31,crit=modo==0?0:modo==1?n-1:(n-1)/31;
        EXIGIR(g->cantidad_dependencias==ar);EXIGIR(g->camino_critico==crit);
        printf("ESTRES n=%zu modo=%u aristas=%zu critico=%zu segundos=%.6f matriz_bytes=%zu\n",n,modo,ar,crit,segundos,n*n*sizeof(uint16_t));}
    else printf("ESTRES n=%zu modo=%u rechazo=%d segundos=%.6f\n",n,modo,estado,segundos);
    vliw_dag_liberar(&g);vliw_cfg_liberar(&cfg);free(op);free(m);free(ef);
}
/* 4092 no garantiza un plan de1023: una cadena WAW de1024 ya necesita1024
 * bundles. Se mide el constructor solo, sin el costoso validador global I1-A1. */
static void limites(void)
{
    grande(4092,0,VLIW_DAG_OK);grande(4092,2,VLIW_DAG_OK);
    grande(1023,1,VLIW_DAG_OK);grande(1024,1,VLIW_DAG_CAPACIDAD);
    // Con barrera3 en cada operación,342 separaciones requieren1026 bundles
    // de distancia: el constructor rechaza sin depender del futuro scheduler.
    grande(343,3,VLIW_DAG_CAPACIDAD);
}
/* Falla cada reserva observada, incluidas reconstrucción CFG, matriz y dos
 * crecimientos realloc. Compara entradas y CFG anidado antes de repetir sano. */
static void recursos(void)
{
#ifdef DAG_FALLOS
    enum{N=80};VliwCfgOperacion op[N];VliwMetadatosParciales m[N];unsigned ef[N]={0};
    for(size_t i=0;i<N;++i){op[i]=(VliwCfgOperacion){i,(UINT32_C(0x40)<<25)|(4u<<11)|1,VLIW_SLOT_ALU,SIM_SALTO_NINGUNO,VLIW_CONTROL_NINGUNO};
        EXIGIR(vliw_metadatos_extraer(op[i].palabra,0,&m[i],NULL)==SIM_DECODE_OK);}
    VliwCfgFuncion fn={10,0,N};VliwCfgEntrada flujo={.operaciones=op,.cantidad_operaciones=N,.funciones=&fn,.cantidad_funciones=1,
        .generacion=17,.generacion_referencias=17,.fronteras_acreditadas=true};
    VliwCfg *cfg=NULL;EXIGIR(vliw_cfg_construir(&flujo,&cfg,NULL)==VLIW_CFG_OK);
    // Estas entradas se comparan byte a byte: inicializamos también padding
    // antes de asignar campos, para que la captura de integridad sea definida.
    VliwDagEntrada e;memset(&e,0,sizeof e);e.flujo=&flujo;e.cfg=cfg;e.metadatos=m;
    e.efectos_adicionales=ef;e.cantidad_metadatos=N;e.generacion=17;
    e.efectos_acreditados=true;e.abi_acreditado=true;VliwDag *g=NULL;
    VliwCfgOperacion copia[N];VliwMetadatosParciales cm[N];memcpy(copia,op,sizeof op);memcpy(cm,m,sizeof m);
    VliwCfg cc=*cfg;VliwBloque bb=*cfg->bloques;size_t mapa[N];memcpy(mapa,cfg->bloque_por_operacion,sizeof mapa);
    llamadas=0;fallar=SIZE_MAX;activo=true;EXIGIR(vliw_dag_construir(&e,&g,NULL)==VLIW_DAG_OK);activo=false;
    size_t total=llamadas;EXIGIR(total>=10);vliw_dag_liberar(&g);++escenarios;
    VliwDagEntrada entrada_antes;VliwCfgEntrada flujo_antes;VliwCfgFuncion fn_antes;
    memcpy(&entrada_antes,&e,sizeof e);memcpy(&flujo_antes,&flujo,sizeof flujo);memcpy(&fn_antes,&fn,sizeof fn);
    for(size_t k=1;k<=total;++k){llamadas=0;fallar=k;activo=true;
        VliwDagEstado estado=vliw_dag_construir(&e,&g,NULL);activo=false;
        EXIGIR(estado==VLIW_DAG_MEMORIA);EXIGIR(!g);
        EXIGIR(!memcmp(copia,op,sizeof op));EXIGIR(!memcmp(cm,m,sizeof m));
        EXIGIR(!memcmp(&cc,cfg,sizeof cc));EXIGIR(!memcmp(&bb,cfg->bloques,sizeof bb));
        EXIGIR(!memcmp(mapa,cfg->bloque_por_operacion,sizeof mapa));
        EXIGIR(!memcmp(&entrada_antes,&e,sizeof e));EXIGIR(!memcmp(&flujo_antes,&flujo,sizeof flujo));
        EXIGIR(!memcmp(&fn_antes,&fn,sizeof fn));
        for(size_t i=0;i<N;++i)EXIGIR(ef[i]==0);
        ++escenarios;EXIGIR(vliw_dag_construir(&e,&g,NULL)==VLIW_DAG_OK);vliw_dag_liberar(&g);
    }
    printf("RESERVAS secuencia=%zu; fallos inyectados=%zu\n",total,total);vliw_cfg_liberar(&cfg);
    // La secuencia anterior no reserva destinos ni sucesores. Este fixture
    // call/ret añade esas rutas de adquisición y su limpieza parcial anidada.
    Fixture f;llamada(&f);preparar(&f);llamadas=0;fallar=SIZE_MAX;activo=true;
    g=ejecutar(&f,VLIW_DAG_OK);activo=false;total=llamadas;vliw_dag_liberar(&g);
    for(size_t k=1;k<=total;++k){llamadas=0;fallar=k;activo=true;
        g=ejecutar(&f,VLIW_DAG_MEMORIA);activo=false;EXIGIR(!g);
        g=ejecutar(&f,VLIW_DAG_OK);vliw_dag_liberar(&g);
    }
    printf("RESERVAS call/ret=%zu; fallos inyectados=%zu\n",total,total);vliw_cfg_liberar(&f.cfg);
#else
    EXIGIR(false);
#endif
}
/* Selecciona un grupo reproducible; los contadores aumentan dentro de los
 * escenarios ejecutados, no se publican cifras prefijadas como resultados. */
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    if(!strcmp(argv[1],"basicos"))basicos();else if(!strcmp(argv[1],"registros"))registros();
    else if(!strcmp(argv[1],"memoria"))memoria();else if(!strcmp(argv[1],"control"))control();
    else if(!strcmp(argv[1],"barreras"))barreras();else if(!strcmp(argv[1],"abi"))abi();
    else if(!strcmp(argv[1],"rechazos"))rechazos();else if(!strcmp(argv[1],"argumentos"))argumentos();
    else if(!strcmp(argv[1],"combinaciones"))combinaciones();else if(!strcmp(argv[1],"integracion"))integracion();
    else if(!strcmp(argv[1],"limites"))limites();else if(!strcmp(argv[1],"recursos"))recursos();else return 2;
    printf("PASS %s: escenarios=%zu; comprobaciones=%zu\n",argv[1],escenarios,comprobaciones);return 0;
}
