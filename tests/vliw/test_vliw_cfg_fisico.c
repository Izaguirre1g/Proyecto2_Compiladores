/* Fixtures físicos I1-A2b-1: oráculos manuales de partición, destinos y mapas.
 * Comprueba errores/inmutabilidad, reservas fallidas y dos cadenas controladas
 * con consumidores reales. No genera BAL ni construye dependencias. */
#include "vliw_cfg_fisico.h"
#include "vliw_integracion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VAC VLIW_VACIO
#define R(x) (UINT64_C(1)<<(x))
static unsigned escenarios,comprobaciones;
#define EXIGIR(x) do { ++comprobaciones; if(!(x)) {fprintf(stderr,"línea %d: %s\n",__LINE__,#x);exit(1);} } while(0)
#ifdef CFG_FALLOS
static size_t llamadas,fallar;static int activo;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);
/* Cuenta solo reservas mientras el constructor está activo. La compilación
 * condicional no sustituye funciones del código de producción. */
void *__wrap_malloc(size_t n) {if(activo && ++llamadas==fallar)return NULL;return __real_malloc(n);}
/* Intercepta también calloc: reservar bloques o resultado puede fallar antes
 * de existir arreglos anidados; se exige la misma limpieza parcial. */
void *__wrap_calloc(size_t n,size_t m) {if(activo && ++llamadas==fallar)return NULL;return __real_calloc(n,m);}
#endif
typedef struct {
    VliwCfgEntrada e;
    VliwCfgOperacion op[8];VliwCfgFuncion fn[3];VliwCfgSimbolo s[8];
    VliwCfgTransferencia t[8];size_t fronteras[8],posibles[8];VliwCfgRegion regiones[4];
} Fixture;
/* Proporciona sumas físicas conocidas y una función única. Las palabras son
 * datos ISA explícitos; no se codifica un programa mediante el constructor. */
static void iniciar(Fixture *f,size_t n)
{
    memset(f,0,sizeof *f);
    f->e=((VliwCfgEntrada){f->op,n,f->fn,n?1:0,f->s,0,f->t,0,f->fronteras,0,f->regiones,0,17,17,0,true});
    f->fn[0]=((VliwCfgFuncion){10,0,n});
    for(size_t i=0;i<n;++i)f->op[i]=((VliwCfgOperacion){100+7*i,(UINT32_C(0x40)<<25)|(4u<<11)|7u,VLIW_SLOT_ALU,SIM_SALTO_NINGUNO,VLIW_CONTROL_NINGUNO});
}
/* Declara un salto y su intención externa. Los campos de palabra se fijan
 * con el ISA real; los destinos se suministran por identidad simbólica. */
static void bru(Fixture *f,size_t i,SimSalto forma,VliwControl control,
    VliwDestinoTipo tipo,size_t tomado,size_t continuacion)
{
    uint32_t w=forma==SIM_SALTO_COND_PC ? (UINT32_C(0x41)<<25)|(1u<<21) :
        (UINT32_C(0x4b)<<25)|(forma==SIM_SALTO_REGISTRO ? (1u<<21)|((control==VLIW_CONTROL_RETORNO?1u:13u)<<11):0);
    if(control==VLIW_CONTROL_LLAMADA)w|=1u<<16;
    f->op[i]=((VliwCfgOperacion){100+7*i,w,VLIW_SLOT_BRU,forma,control});
    f->t[f->e.cantidad_transferencias++]=((VliwCfgTransferencia){i,tipo,tomado,continuacion,NULL,0,forma!=SIM_SALTO_REGISTRO});
}
/* Compara toda la entrada, incluidos arreglos y referencias, después de cada
 * ejecución. Un rechazo debe dejar NULL y un diagnóstico localizado. */
static VliwCfg *ejecutar(Fixture *f,VliwCfgEstado esperado)
{
    unsigned char copia[sizeof *f];memcpy(copia,f,sizeof *f);
    VliwCfg *c=NULL;VliwCfgDiagnostico d;
    VliwCfgEstado e=vliw_cfg_construir(&f->e,&c,&d);
    if(e!=esperado)fprintf(stderr,"escenario %u: %d != %d; %s\n",escenarios,e,esperado,d.mensaje);
    EXIGIR(e==esperado);EXIGIR(d.estado==e);EXIGIR(d.mensaje && *d.mensaje);
    EXIGIR(!memcmp(copia,f,sizeof *f));EXIGIR((c!=NULL)==(e==VLIW_CFG_OK));
    if(c) {
        EXIGIR(c->cantidad_operaciones==f->e.cantidad_operaciones);
        EXIGIR(c->cantidad_destinos==f->e.cantidad_transferencias);
        /* El tipo y la posición deben sobrevivir sin reinterpretar intención;
         * los destinos/mapas se contrastan además con oráculos por escenario. */
        for(size_t t=0;t<c->cantidad_destinos;++t) {
            EXIGIR(c->destinos[t].instruccion==f->t[t].operacion);
            EXIGIR(c->destinos[t].tipo==f->t[t].tipo);
        }
    }
    ++escenarios;
    return c;
}
/* Contrasta extremos y sucesores como conjunto contra constantes del fixture;
 * no usa otra llamada al constructor para calcular los valores esperados. */
static void bloque(VliwCfg *c,size_t b,size_t inicio,size_t fin,const size_t *s,size_t ns)
{
    EXIGIR(b<c->cantidad_bloques);EXIGIR(c->bloques[b].inicio==inicio);EXIGIR(c->bloques[b].fin==fin);
    EXIGIR(c->bloques[b].cantidad_sucesores==ns);
    for(size_t j=0;j<ns;++j){size_t k=0;while(k<ns && c->bloques[b].sucesores[k]!=s[j])++k;EXIGIR(k<ns);}
    for(size_t i=inicio;i<fin;++i)EXIGIR(c->bloque_por_operacion[i]==b);
}
/* Vacío y secuencias 1..8 no tienen líderes artificiales. Una etiqueta en 2
 * y un alias de esa etiqueta deben producir exactamente dos bloques. */
static void secuencias(void)
{
    Fixture f;VliwCfg *c;
    for(size_t n=0;n<=8;++n){iniciar(&f,n);c=ejecutar(&f,VLIW_CFG_OK);
        EXIGIR(c->cantidad_bloques==(n?1:0));EXIGIR(c->generacion==17);
        if(n)bloque(c,0,0,n,NULL,0);
        vliw_cfg_liberar(&c);EXIGIR(!c);vliw_cfg_liberar(&c);}
    iniciar(&f,4);f.s[0]=((VliwCfgSimbolo){500,10,2});f.s[1]=((VliwCfgSimbolo){501,10,2});f.e.cantidad_simbolos=2;
    c=ejecutar(&f,VLIW_CFG_OK);EXIGIR(c->cantidad_bloques==2);
    size_t uno[]={1};bloque(c,0,0,2,uno,1);bloque(c,1,2,4,NULL,0);vliw_cfg_liberar(&c);
    /* Una frontera acreditada adicional subdivide sin cambiar IDs externos;
     * una región protegida sin entradas internas permite varias operaciones. */
    iniciar(&f,4);f.fronteras[0]=1;f.e.cantidad_fronteras=1;f.regiones[0]=((VliwCfgRegion){1,4});f.e.cantidad_regiones=1;
    f.op[0].id=SIZE_MAX;c=ejecutar(&f,VLIW_CFG_OK);bloque(c,0,0,1,uno,1);bloque(c,1,1,4,NULL,0);vliw_cfg_liberar(&c);
}
/* Directos, condicionales y ciclos se resuelven por símbolos. Se prueban
 * bucles inicial/propio, destino previo y tomado igual/distinto de caída. */
static void saltos(void)
{
    Fixture f;VliwCfg *c;size_t cero[]={0},uno[]={1},dos[]={2},ambos[]={2,1};
    iniciar(&f,1);f.s[0]=((VliwCfgSimbolo){500,10,0});f.e.cantidad_simbolos=1;
    bru(&f,0,SIM_SALTO_PC,VLIW_CONTROL_SALTO,VLIW_DEST_DIRECTO,500,VAC);
    c=ejecutar(&f,VLIW_CFG_OK);bloque(c,0,0,1,cero,1);EXIGIR(c->destinos[0].tomado==0);vliw_cfg_liberar(&c);
    iniciar(&f,3);f.s[0]=((VliwCfgSimbolo){500,10,0});f.e.cantidad_simbolos=1;
    bru(&f,1,SIM_SALTO_PC,VLIW_CONTROL_SALTO,VLIW_DEST_DIRECTO,500,VAC);
    c=ejecutar(&f,VLIW_CFG_OK);bloque(c,0,0,2,cero,1);bloque(c,1,2,3,NULL,0);vliw_cfg_liberar(&c);
    for(size_t p=1;p<=2;++p){iniciar(&f,3);f.s[0]=((VliwCfgSimbolo){500,10,p});f.e.cantidad_simbolos=1;
        bru(&f,0,SIM_SALTO_COND_PC,VLIW_CONTROL_SALTO,VLIW_DEST_CONDICIONAL,500,1);
        c=ejecutar(&f,VLIW_CFG_OK);bloque(c,0,0,1,p==1?uno:ambos,p==1?1:2);
        EXIGIR(c->destinos[0].caida==1);EXIGIR(c->destinos[0].tomado==p);
        if(p==1)bloque(c,1,1,3,NULL,0);else {bloque(c,1,1,2,dos,1);bloque(c,2,2,3,NULL,0);}vliw_cfg_liberar(&c);}
    iniciar(&f,3);f.s[0]=((VliwCfgSimbolo){500,10,2});f.e.cantidad_simbolos=1;
    bru(&f,0,SIM_SALTO_PC,VLIW_CONTROL_SALTO,VLIW_DEST_DIRECTO,500,VAC);
    c=ejecutar(&f,VLIW_CFG_OK);bloque(c,0,0,1,dos,1);bloque(c,1,1,2,dos,1);bloque(c,2,2,3,NULL,0);vliw_cfg_liberar(&c);
    /* El condicional al final puede declarar salida del flujo como caída
     * ausente; el único sucesor conocido sigue siendo su backedge. */
    iniciar(&f,1);f.s[0]=((VliwCfgSimbolo){500,10,0});f.e.cantidad_simbolos=1;
    bru(&f,0,SIM_SALTO_COND_PC,VLIW_CONTROL_SALTO,VLIW_DEST_CONDICIONAL,500,VAC);
    c=ejecutar(&f,VLIW_CFG_OK);bloque(c,0,0,1,cero,1);vliw_cfg_liberar(&c);
}
/* Prepara dos funciones: call en 0, continuación/retorno en 1, callee en 2
 * y retorno en 3. Los retornos sin conjunto completo quedan explícitamente
 * sin cobertura dinámica, aunque su CFG estructural sea válido. */
static void llamadas_base(Fixture *f,int indirecta)
{
    iniciar(f,4);f->fn[0].fin=2;f->fn[1]=((VliwCfgFuncion){20,2,4});f->e.cantidad_funciones=2;
    f->s[0]=((VliwCfgSimbolo){500,20,2});f->e.cantidad_simbolos=1;
    bru(f,0,indirecta?SIM_SALTO_REGISTRO:SIM_SALTO_PC,VLIW_CONTROL_LLAMADA,indirecta?VLIW_DEST_INDIRECTO:VLIW_DEST_DIRECTO,indirecta?VAC:500,1);
    if(indirecta){f->posibles[0]=500;f->t[0].posibles=f->posibles;f->t[0].cantidad_posibles=1;f->t[0].completos=true;}
    bru(f,1,SIM_SALTO_REGISTRO,VLIW_CONTROL_RETORNO,VLIW_DEST_RETORNO,VAC,VAC);
    bru(f,3,SIM_SALTO_REGISTRO,VLIW_CONTROL_RETORNO,VLIW_DEST_RETORNO,VAC,VAC);
}
/* Conserva el tipo indirecto y la continuación sin convertir SYER en SYE.
 * También prueba alias de destinos dinámicos que convergen al mismo bloque. */
static void dinamicos(void)
{
    Fixture f;VliwCfg *c;size_t destino[]={2,1};
    for(int k=0;k<2;++k){llamadas_base(&f,k);c=ejecutar(&f,VLIW_CFG_OK);
        EXIGIR(c->cantidad_bloques==3);bloque(c,0,0,1,destino,2);bloque(c,1,1,2,NULL,0);bloque(c,2,2,4,NULL,0);
        EXIGIR(c->destinos[0].tomado==(k?VAC:2));EXIGIR(c->destinos[0].caida==1);
        EXIGIR(!c->destinos_dinamicos_acreditados);vliw_cfg_liberar(&c);}
    for(int ret=0;ret<2;++ret){iniciar(&f,1);bru(&f,0,SIM_SALTO_REGISTRO,ret?VLIW_CONTROL_RETORNO:VLIW_CONTROL_SALTO,ret?VLIW_DEST_RETORNO:VLIW_DEST_INDIRECTO,VAC,VAC);
        c=ejecutar(&f,VLIW_CFG_OK);bloque(c,0,0,1,NULL,0);EXIGIR(!c->destinos_dinamicos_acreditados);vliw_cfg_liberar(&c);
        f.s[0]=((VliwCfgSimbolo){500,10,0});f.s[1]=((VliwCfgSimbolo){501,10,0});f.e.cantidad_simbolos=2;
        f.posibles[0]=500;f.posibles[1]=501;f.t[0].posibles=f.posibles;f.t[0].cantidad_posibles=2;f.t[0].completos=true;
        c=ejecutar(&f,VLIW_CFG_OK);size_t cero[]={0};bloque(c,0,0,1,cero,1);EXIGIR(c->destinos_dinamicos_acreditados);vliw_cfg_liberar(&c);}
}
/* Cada modificación rompe un contrato aislado sobre una suma válida. Los
 * diagnósticos exactos y la conservación byte a byte impiden aceptar entradas
 * incompletas o repararlas silenciosamente. */
static void rechazos(void)
{
    Fixture f;VliwCfg *c;
#define MAL(cambio,codigo) do {iniciar(&f,2);cambio;c=ejecutar(&f,codigo);EXIGIR(!c);}while(0)
    MAL(f.e.operaciones=NULL,VLIW_CFG_ARGUMENTO);
    MAL(f.e.cantidad_operaciones=SIZE_MAX,VLIW_CFG_CAPACIDAD);
    MAL(f.e.cantidad_simbolos=SIZE_MAX,VLIW_CFG_CAPACIDAD);
    MAL(f.e.cantidad_funciones=3,VLIW_CFG_ESTRUCTURA);
    MAL(f.e.cantidad_funciones=0,VLIW_CFG_ESTRUCTURA);
    MAL(f.e.generacion_referencias=18,VLIW_CFG_INCOMPLETO);
    MAL(f.e.pendientes=1,VLIW_CFG_INCOMPLETO);
    MAL(f.e.fronteras_acreditadas=false,VLIW_CFG_INCOMPLETO);
    MAL(f.fn[0].inicio=1,VLIW_CFG_FRONTERA);
    MAL(f.fn[0].fin=0,VLIW_CFG_FRONTERA);
    MAL(f.fn[0].fin=3,VLIW_CFG_FRONTERA);
    MAL(f.fn[0].fin=1,VLIW_CFG_FRONTERA);
    MAL(f.fn[0].id=VAC,VLIW_CFG_FRONTERA);
    MAL(f.op[1].id=f.op[0].id,VLIW_CFG_IDENTIDAD);
    MAL(f.op[0].palabra=0,VLIW_CFG_ISA);
    MAL(f.op[0].palabra=UINT32_MAX,VLIW_CFG_ISA);
    MAL(f.op[0].slot=(VliwSlot)9,VLIW_CFG_ISA);
    MAL(f.op[0].salto=SIM_SALTO_PC,VLIW_CFG_CONTROL);
    MAL(f.op[0].control=VLIW_CONTROL_LLAMADA,VLIW_CFG_CONTROL);
    MAL(f.op[0].control=(VliwControl)9,VLIW_CFG_CONTROL);
    MAL(f.e.cantidad_fronteras=1;f.fronteras[0]=2,VLIW_CFG_FRONTERA);
    MAL(f.e.cantidad_regiones=1;f.regiones[0]=((VliwCfgRegion){1,1}),VLIW_CFG_FRONTERA);
    MAL(f.e.cantidad_regiones=1;f.regiones[0]=((VliwCfgRegion){0,3}),VLIW_CFG_FRONTERA);
    MAL(f.e.cantidad_simbolos=1;f.s[0]=((VliwCfgSimbolo){500,10,2}),VLIW_CFG_DESTINO);
    MAL(f.e.cantidad_simbolos=1;f.s[0]=((VliwCfgSimbolo){500,99,0}),VLIW_CFG_DESTINO);
    MAL(f.e.cantidad_simbolos=2;f.s[0]=((VliwCfgSimbolo){500,10,0});f.s[1]=((VliwCfgSimbolo){500,10,1}),VLIW_CFG_IDENTIDAD);
    MAL(f.e.cantidad_simbolos=1;f.s[0]=((VliwCfgSimbolo){500,10,1});f.e.cantidad_regiones=1;f.regiones[0]=((VliwCfgRegion){0,2}),VLIW_CFG_FRONTERA);
    MAL(f.fn[0].fin=1;f.fn[1]=((VliwCfgFuncion){20,1,2});f.e.cantidad_funciones=2,VLIW_CFG_FRONTERA);
    MAL(f.e.cantidad_transferencias=1;f.t[0].operacion=2,VLIW_CFG_DESTINO);
    MAL(f.e.cantidad_transferencias=1,VLIW_CFG_CONTROL);
#undef MAL
    /* Partimos ahora de call/ret reales: estos negativos aíslan el crédito
     * de destinos y continuaciones, no una palabra ISA inválida. */
#define CALL_MAL(cambio,codigo) do {llamadas_base(&f,0);cambio;c=ejecutar(&f,codigo);EXIGIR(!c);}while(0)
    CALL_MAL(f.t[0].tomado=999,VLIW_CFG_DESTINO);
    CALL_MAL(f.t[0].completos=false,VLIW_CFG_INCOMPLETO);
    CALL_MAL(f.t[0].continuacion=2,VLIW_CFG_FRONTERA);
    CALL_MAL(f.t[0].tipo=VLIW_DEST_RETORNO,VLIW_CFG_CONTROL);
    CALL_MAL(f.t[0].tipo=VLIW_DEST_NO_RESUELTO,VLIW_CFG_CONTROL);
    CALL_MAL(f.e.cantidad_transferencias=2,VLIW_CFG_INCOMPLETO);
    CALL_MAL(f.t[1].operacion=0,VLIW_CFG_CONTROL);
    CALL_MAL(f.t[0].operacion=4,VLIW_CFG_DESTINO);
    CALL_MAL(f.s[0].posicion=3,VLIW_CFG_DESTINO);
    CALL_MAL(f.fn[1].inicio=1,VLIW_CFG_FRONTERA);
    CALL_MAL(f.fn[1].id=10,VLIW_CFG_IDENTIDAD);
    CALL_MAL(f.t[1].completos=true,VLIW_CFG_INCOMPLETO);
    CALL_MAL(f.t[1].tomado=500,VLIW_CFG_INCOMPLETO);
    CALL_MAL(f.t[1].cantidad_posibles=2,VLIW_CFG_ARGUMENTO);
    CALL_MAL(f.e.simbolos=NULL,VLIW_CFG_ARGUMENTO);
    CALL_MAL(f.e.transferencias=NULL,VLIW_CFG_ARGUMENTO);
    CALL_MAL(f.t[1].cantidad_posibles=1,VLIW_CFG_ARGUMENTO);
    CALL_MAL(f.t[0].continuacion=VAC,VLIW_CFG_FRONTERA);
    CALL_MAL(f.fn[1].fin=2,VLIW_CFG_FRONTERA);
    CALL_MAL(f.op[0].control=VLIW_CONTROL_SALTO;f.t[0].continuacion=VAC,VLIW_CFG_DESTINO);
    /* Una región no puede unir operaciones de funciones distintas, y una
     * condición al final del caller tampoco puede caer hacia el callee. */
    CALL_MAL(f.e.cantidad_regiones=1;f.regiones[0]=((VliwCfgRegion){0,3}),VLIW_CFG_FRONTERA);
    CALL_MAL(f.op[1].palabra=(UINT32_C(0x41)<<25)|(1u<<21);f.op[1].salto=SIM_SALTO_COND_PC;f.op[1].control=VLIW_CONTROL_SALTO;f.t[1].tipo=VLIW_DEST_CONDICIONAL;f.t[1].continuacion=2,VLIW_CFG_FRONTERA);
#undef CALL_MAL
    /* Salida ocupada y NULL se rechazan antes de adquirir recursos; nunca
     * se desreferencia ni se libera la salida anterior del llamador. */
    iniciar(&f,1);c=(VliwCfg *)&f;VliwCfg *anterior=c;
    EXIGIR(vliw_cfg_construir(&f.e,&c,NULL)==VLIW_CFG_ARGUMENTO);EXIGIR(c==anterior);++escenarios;
    c=NULL;EXIGIR(vliw_cfg_construir(NULL,&c,NULL)==VLIW_CFG_ARGUMENTO);EXIGIR(!c);++escenarios;
    EXIGIR(vliw_cfg_construir(&f.e,NULL,NULL)==VLIW_CFG_ARGUMENTO);++escenarios;
}
/* El máximo estructural reserva almacenamiento real. Verifica extremos y
 * mapa completo; no afirma que 4092 ALU quepan en un plan de 1023 bundles. */
static void limites(void)
{
    size_t n=4092;VliwCfgOperacion *ops=calloc(n,sizeof *ops);EXIGIR(ops);
    for(size_t i=0;i<n;++i)ops[i]=((VliwCfgOperacion){i,(UINT32_C(0x40)<<25)|(4u<<11)|7u,VLIW_SLOT_ALU,SIM_SALTO_NINGUNO,VLIW_CONTROL_NINGUNO});
    VliwCfgFuncion fn={9,0,n};VliwCfgEntrada e={0};e.operaciones=ops;e.cantidad_operaciones=n;e.funciones=&fn;e.cantidad_funciones=1;e.fronteras_acreditadas=true;
    VliwCfg *c=NULL;EXIGIR(vliw_cfg_construir(&e,&c,NULL)==VLIW_CFG_OK);bloque(c,0,0,n,NULL,0);++escenarios;
    for(size_t i=0;i<n;++i)EXIGIR(ops[i].id==i);
    vliw_cfg_liberar(&c);free(ops);vliw_cfg_liberar(NULL);
}
/* Los fixtures aportan manualmente máscaras, efectos y RAW3; el constructor
 * únicamente aporta CFG/mapa/destinos. La cadena real no se presenta como
 * DAG construido ni como ejecución de BAL. */
static void integracion(void)
{
    Fixture f;VliwCfg *c;VliwInstruccion ops[2];VliwDependencia raw={0,1,VLIW_DEP_RAW,3};
    for(int k=0;k<2;++k){
        iniciar(&f,k?1:2);
        if(k){f.s[0]=((VliwCfgSimbolo){500,10,0});f.e.cantidad_simbolos=1;bru(&f,0,SIM_SALTO_PC,VLIW_CONTROL_SALTO,VLIW_DEST_DIRECTO,500,VAC);}
        else {f.op[1]=((VliwCfgOperacion){107,(UINT32_C(0x49)<<25)|(2u<<16)|(4u<<11)|4u,VLIW_SLOT_LSU,SIM_SALTO_NINGUNO,VLIW_CONTROL_NINGUNO});}
        c=ejecutar(&f,VLIW_CFG_OK);
        ops[0]=(VliwInstruccion){100,k?VLIW_SLOT_BRU:VLIW_SLOT_ALU,k?"sye x0,inicio":"sumai x4,x0,7",0,k?0:R(4),0,k?VLIW_CONTROL_SALTO:VLIW_CONTROL_NINGUNO,c->bloque_por_operacion[0]};
        ops[1]=(VliwInstruccion){107,VLIW_SLOT_LSU,"guardap x2,x4,4",R(2)|R(4),0,VLIW_EF_ESCRIBE_MEMORIA,VLIW_CONTROL_NINGUNO,k?0:c->bloque_por_operacion[1]};
        VliwEntrada e={ops,k?1:2,k?NULL:&raw,k?0:1,c->bloques,c->cantidad_bloques,true};
        VliwDestinos ds={c->destinos,c->cantidad_destinos};VliwPlan *p=NULL;char *asmtexto=NULL;
        EXIGIR(vliw_integracion_validar(&e,&ds,NULL)==VLIW_INT_OK);
        EXIGIR(vliw_planificar(&e,&p,NULL)==VLIW_OK);EXIGIR(vliw_verificar(&e,p,NULL)==VLIW_OK);
        EXIGIR(vliw_emitir(&e,p,&ds,&asmtexto,NULL)==VLIW_EM_OK);EXIGIR(asmtexto && *asmtexto);
        if(!k)EXIGIR(p->bundle_por_instruccion[1]-p->bundle_por_instruccion[0]>=3);
        vliw_emisor_liberar(&asmtexto);vliw_liberar(&p);vliw_cfg_liberar(&c);
    }
}
/* Inyecta cada fallo de reserva observado. Tras el rechazo se repite sano
 * para detectar recursos retenidos o estado global contaminado. */
static void recursos(void)
{
#ifdef CFG_FALLOS
    Fixture f;iniciar(&f,4);f.e.cantidad_fronteras=2;f.fronteras[0]=1;f.fronteras[1]=2;
    llamadas=0;fallar=SIZE_MAX;activo=1;VliwCfg *c=ejecutar(&f,VLIW_CFG_OK);activo=0;
    size_t total=llamadas;EXIGIR(total>=8);vliw_cfg_liberar(&c);
    for(size_t k=1;k<=total;++k){llamadas=0;fallar=k;activo=1;c=ejecutar(&f,VLIW_CFG_MEMORIA);activo=0;EXIGIR(!c);
        c=ejecutar(&f,VLIW_CFG_OK);vliw_cfg_liberar(&c);}
#else
    EXIGIR(0);
#endif
}
/* Cada grupo informa conteos reales al wrapper Python; argumento desconocido
 * retorna 2 y evita acreditar una ejecución que no recorrió escenarios. */
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    if(!strcmp(argv[1],"secuencias"))secuencias();else if(!strcmp(argv[1],"saltos"))saltos();
    else if(!strcmp(argv[1],"dinamicos"))dinamicos();else if(!strcmp(argv[1],"rechazos"))rechazos();
    else if(!strcmp(argv[1],"limites"))limites();else if(!strcmp(argv[1],"integracion"))integracion();
    else if(!strcmp(argv[1],"recursos"))recursos();else return 2;
    printf("PASS %s: escenarios=%u; comprobaciones=%u\n",argv[1],escenarios,comprobaciones);return 0;
}
