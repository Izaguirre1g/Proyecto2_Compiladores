/* Pruebas del scheduler y de su verificador, sin parser, IR ni simulador.
 * Los fixtures entregan dependencias explícitas como lo hará la fase externa. */
#include "vliw_scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static size_t pruebas, comprobaciones;
#define C(x) do { ++comprobaciones; if (!(x)) { fprintf(stderr,"FAIL S7 %d: %s\n",__LINE__,#x); exit(1); } } while (0)
#define R(x) (UINT64_C(1) << (x))
typedef struct {
    VliwInstruccion i[64]; VliwDependencia a[512];
    VliwBloque b[8]; size_t sucesores[8][8]; VliwEntrada e;
} Fixture;
static void iniciar(Fixture *f, size_t n)
{
    memset(f,0,sizeof(*f)); C(n<=64);
    f->e=(VliwEntrada){f->i,n,f->a,0,f->b,n?1:0,true};
    f->b[0]=(VliwBloque){0,n,NULL,0};
    for(size_t i=0;i<n;++i)f->i[i]=(VliwInstruccion){.id=100+i,.slot=VLIW_SLOT_ALU};
}
static void arista(Fixture *f,size_t a,size_t b,VliwTipoDependencia tipo,size_t distancia)
{
    C(f->e.cantidad_dependencias<512);
    f->a[f->e.cantidad_dependencias++]=(VliwDependencia){a,b,tipo,distancia};
}
static VliwPlan *plan(Fixture *f)
{
    VliwPlan *p=NULL; VliwDiagnostico d;
    VliwEstado s=vliw_planificar(&f->e,&p,&d);
    if(s!=VLIW_OK)fprintf(stderr,"diagnóstico: %s\n",d.mensaje);
    C(s==VLIW_OK && p!=NULL); C(vliw_verificar(&f->e,p,&d)==VLIW_OK);
    return p;
}
static void rechazar(Fixture *f,VliwEstado esperado)
{
    VliwPlan *p=NULL;VliwDiagnostico d;
    C(vliw_planificar(&f->e,&p,&d)==esperado);C(p==NULL && d.estado==esperado && d.mensaje);
}
static void bloques(Fixture *f,size_t b,size_t inicio,size_t fin)
{
    f->b[b]=(VliwBloque){inicio,fin,f->sucesores[b],0};
    for(size_t i=inicio;i<fin;++i)f->i[i].bloque=b;
}
static void control(Fixture *f,size_t i,VliwControl c)
{
    f->i[i].slot=VLIW_SLOT_BRU;f->i[i].control=c;
    if(c==VLIW_CONTROL_LLAMADA)f->i[i].efectos=VLIW_EF_DESCONOCIDOS;
}
static void unidad(Fixture *f,size_t i,VliwSlot s)
{
    f->i[i].slot=s;
    if(s==VLIW_SLOT_LSU)f->i[i].efectos=VLIW_EF_LEE_MEMORIA;
    if(s==VLIW_SLOT_CRIPTO)f->i[i].efectos=VLIW_EF_EXCEPCION;
}

static void casos_basicos(void)
{
    Fixture f;VliwPlan *p;
    ++pruebas;iniciar(&f,0);f.e.metadatos_completos=false;p=plan(&f);
    C(!p->cantidad_bundles && !p->cantidad_instrucciones);vliw_liberar(&p);vliw_liberar(&p);vliw_liberar(NULL);
    ++pruebas;iniciar(&f,1);p=plan(&f);
    C(p->cantidad_bundles==1 && p->bundles[0].instrucciones[0]==0);
    for(unsigned s=1;s<4;++s)C(p->bundles[0].instrucciones[s]==VLIW_VACIO);
    vliw_liberar(&p);
    /* ALU/LSU sin riesgos comparten ciclo0. No se fuerza una BRU o CRIPTO
     * potencialmente excepcional al mismo bundle solo para llenar cuatro slots. */
    ++pruebas;iniciar(&f,2);unidad(&f,1,VLIW_SLOT_LSU);p=plan(&f);
    C(p->cantidad_bundles==1 && p->bundles[0].operaciones==2);vliw_liberar(&p);
    ++pruebas;iniciar(&f,4);unidad(&f,1,VLIW_SLOT_LSU);unidad(&f,2,VLIW_SLOT_CRIPTO);control(&f,3,VLIW_CONTROL_SALTO);
    p=plan(&f);C(p->cantidad_bundles==7 && p->cantidad_vacios==4);
    C(p->bundle_por_instruccion[2]==3 && p->bundle_por_instruccion[3]==6);vliw_liberar(&p);
    ++pruebas;iniciar(&f,2);p=plan(&f);
    C(p->bundle_por_instruccion[0]==0 && p->bundle_por_instruccion[1]==1);vliw_liberar(&p);
}

static void riesgos_y_disponibilidad(void)
{
    Fixture f;VliwPlan *p;
    /* RAW se mide en bundles, no en número de instrucciones. La cadena debe
     * emitir en 0,3,6; el consumidor nunca puede observar el WB simultáneo a ID. */
    ++pruebas;iniciar(&f,2);f.i[0].escribe=R(5);f.i[1].lee=R(5);
    arista(&f,0,1,VLIW_DEP_RAW,3);p=plan(&f);C(p->cantidad_bundles==4 && p->cantidad_vacios==2);vliw_liberar(&p);
    ++pruebas;iniciar(&f,3);f.i[0].escribe=R(5);f.i[1].lee=R(5);f.i[1].escribe=R(6);f.i[2].lee=R(6);
    arista(&f,0,1,VLIW_DEP_RAW,3);arista(&f,1,2,VLIW_DEP_RAW,3);p=plan(&f);C(p->bundle_por_instruccion[2]==6);vliw_liberar(&p);
    ++pruebas;iniciar(&f,2);f.i[0].escribe=f.i[1].escribe=R(5);unidad(&f,1,VLIW_SLOT_LSU);
    arista(&f,0,1,VLIW_DEP_WAW,1);p=plan(&f);C(p->bundle_por_instruccion[1]==1);vliw_liberar(&p);
    ++pruebas;iniciar(&f,2);f.i[0].lee=R(5);f.i[1].escribe=R(5);unidad(&f,1,VLIW_SLOT_LSU);
    arista(&f,0,1,VLIW_DEP_WAR,1);p=plan(&f);C(p->bundle_por_instruccion[1]==1);vliw_liberar(&p);
    ++pruebas;iniciar(&f,4);unidad(&f,1,VLIW_SLOT_LSU);unidad(&f,3,VLIW_SLOT_LSU);
    f.i[0].escribe=R(5);f.i[1].escribe=R(6);f.i[2].lee=f.i[3].lee=R(5)|R(6);
    for(size_t a=0;a<2;++a)for(size_t b=2;b<4;++b)arista(&f,a,b,VLIW_DEP_RAW,3);
    p=plan(&f);C(p->cantidad_bundles==4 && p->bundles[3].operaciones==2);vliw_liberar(&p);
    ++pruebas;iniciar(&f,3);unidad(&f,0,VLIW_SLOT_CRIPTO);unidad(&f,2,VLIW_SLOT_LSU);
    f.i[0].escribe=R(4)|R(5);f.i[1].lee=R(4);f.i[2].lee=R(5);
    arista(&f,0,1,VLIW_DEP_RAW,3);arista(&f,0,2,VLIW_DEP_RAW,3);
    p=plan(&f);C(p->bundle_por_instruccion[1]==3 && p->bundle_por_instruccion[2]==3);vliw_liberar(&p);
    ++pruebas;iniciar(&f,2);unidad(&f,1,VLIW_SLOT_LSU);f.i[0].escribe=R(0);f.i[1].lee=f.i[1].escribe=R(0);
    p=plan(&f);C(p->cantidad_bundles==1);vliw_liberar(&p);
    /* Una arista RAW ausente no se reconstruye dentro del scheduler. El
     * verificador rechaza la salida insegura y no publica un plan parcial. */
    ++pruebas;iniciar(&f,2);f.i[0].escribe=R(5);f.i[1].lee=R(5);rechazar(&f,VLIW_METADATOS);
    f.i[1].lee=0;f.i[1].escribe=R(5);unidad(&f,1,VLIW_SLOT_LSU);
    f.i[0].lee=R(6);f.i[0].escribe=0;
    arista(&f,0,1,VLIW_DEP_CONTROL,7);p=plan(&f);C(p->bundle_por_instruccion[1]==7);vliw_liberar(&p);
}

static void efectos_y_cfg(void)
{
    Fixture f;VliwPlan *p;
    /* Aliasing desconocido: load/store/load mantienen orden, aun sin aristas
     * de memoria explícitas. Una ALU independiente puede aprovechar su slot. */
    ++pruebas;iniciar(&f,4);for(size_t i=0;i<3;++i)unidad(&f,i,VLIW_SLOT_LSU);
    f.i[1].efectos=VLIW_EF_ESCRIBE_MEMORIA;arista(&f,0,1,VLIW_DEP_MEMORIA,1);arista(&f,1,2,VLIW_DEP_MEMORIA,1);
    p=plan(&f);C(p->bundle_por_instruccion[0]<p->bundle_por_instruccion[1] && p->bundle_por_instruccion[1]<p->bundle_por_instruccion[2]);vliw_liberar(&p);
    /* La llamada ocupa el cierre de B0 y drena resultados anteriores. El
     * inicio lógico B1 es su protección; ninguna operación cruza la llamada. */
    ++pruebas;iniciar(&f,3);f.e.cantidad_bloques=2;bloques(&f,0,0,2);bloques(&f,1,2,3);control(&f,1,VLIW_CONTROL_LLAMADA);
    f.sucesores[0][0]=1;f.b[0].cantidad_sucesores=1;
    p=plan(&f);C(p->bundle_por_instruccion[1]==3 && p->inicio_por_bloque[1]==4 && p->bundle_por_instruccion[2]==6);vliw_liberar(&p);
    ++pruebas;iniciar(&f,2);f.e.cantidad_bloques=2;bloques(&f,0,0,1);bloques(&f,1,1,2);
    f.sucesores[0][0]=1;f.b[0].cantidad_sucesores=1;f.i[0].escribe=R(5);f.i[1].lee=R(5);arista(&f,0,1,VLIW_DEP_RAW,3);
    p=plan(&f);C(p->inicio_por_bloque[1]==1 && p->bundle_por_instruccion[1]==3);vliw_liberar(&p);
    ++pruebas;iniciar(&f,3);f.e.cantidad_bloques=2;bloques(&f,0,0,1);bloques(&f,1,1,3);
    f.sucesores[0][0]=1;f.b[0].cantidad_sucesores=1;control(&f,2,VLIW_CONTROL_SALTO);
    f.sucesores[1][0]=0;f.b[1].cantidad_sucesores=1;f.i[0].lee=R(5);f.i[1].escribe=R(5);arista(&f,0,1,VLIW_DEP_WAR,1);
    p=plan(&f);C(p->inicio_por_bloque[0]==0 && p->bundle_por_instruccion[0]==2);vliw_liberar(&p);
    /* Un destino compartido se protege una sola vez en su entrada, de modo
     * que ambas rutas ejecuten esos NOP. El CFG puede contener ciclos. */
    ++pruebas;iniciar(&f,5);f.e.cantidad_bloques=3;bloques(&f,0,0,2);bloques(&f,1,2,3);bloques(&f,2,3,5);
    control(&f,1,VLIW_CONTROL_SALTO);control(&f,4,VLIW_CONTROL_RETORNO);
    f.sucesores[0][0]=1;f.sucesores[0][1]=2;f.b[0].cantidad_sucesores=2;f.sucesores[1][0]=2;f.b[1].cantidad_sucesores=1;
    p=plan(&f);C(p->bundle_por_instruccion[3]>=p->inicio_por_bloque[2]+2);vliw_liberar(&p);
    ++pruebas;iniciar(&f,3);unidad(&f,1,VLIW_SLOT_CRIPTO);f.i[1].efectos=VLIW_EF_ESTADO;
    p=plan(&f);C(p->bundle_por_instruccion[1]==3 && p->bundle_por_instruccion[2]==6);vliw_liberar(&p);
}

static void entradas_invalidas(void)
{
    Fixture f;VliwPlan *p=NULL;VliwDiagnostico d;
    ++pruebas;iniciar(&f,2);arista(&f,0,1,VLIW_DEP_CONTROL,1);arista(&f,1,0,VLIW_DEP_CONTROL,1);rechazar(&f,VLIW_CICLO);
    iniciar(&f,2);arista(&f,1,0,VLIW_DEP_CONTROL,1);rechazar(&f,VLIW_DEPENDENCIA);
    iniciar(&f,1);arista(&f,0,0,VLIW_DEP_CONTROL,1);rechazar(&f,VLIW_CICLO);
    ++pruebas;iniciar(&f,1);f.i[0].slot=(VliwSlot)4;rechazar(&f,VLIW_METADATOS);
    iniciar(&f,1);f.i[0].slot=(VliwSlot)-1;rechazar(&f,VLIW_METADATOS);
    iniciar(&f,2);arista(&f,0,2,VLIW_DEP_RAW,3);rechazar(&f,VLIW_DEPENDENCIA);
    iniciar(&f,2);arista(&f,0,1,VLIW_DEP_RAW,2);rechazar(&f,VLIW_DEPENDENCIA);
    iniciar(&f,2);arista(&f,0,1,VLIW_DEP_RAW,3);rechazar(&f,VLIW_DEPENDENCIA);
    iniciar(&f,2);arista(&f,0,1,VLIW_DEP_CONTROL,0);rechazar(&f,VLIW_DEPENDENCIA);
    iniciar(&f,2);arista(&f,0,1,VLIW_DEP_CONTROL,SIZE_MAX);rechazar(&f,VLIW_CAPACIDAD);
    iniciar(&f,1);f.e.metadatos_completos=false;rechazar(&f,VLIW_METADATOS);
    iniciar(&f,1);f.i[0].lee=R(32);rechazar(&f,VLIW_METADATOS);
    iniciar(&f,1);f.i[0].escribe=R(2)|R(3);rechazar(&f,VLIW_METADATOS);
    iniciar(&f,1);f.i[0].lee=R(2)|R(3)|R(4);rechazar(&f,VLIW_METADATOS);
    iniciar(&f,1);f.i[0].efectos=128;rechazar(&f,VLIW_METADATOS);
    iniciar(&f,1);f.i[0].slot=VLIW_SLOT_LSU;rechazar(&f,VLIW_METADATOS);
    iniciar(&f,1);f.i[0].slot=VLIW_SLOT_CRIPTO;rechazar(&f,VLIW_METADATOS);
    iniciar(&f,2);f.i[1].id=f.i[0].id;rechazar(&f,VLIW_METADATOS);
    iniciar(&f,1);f.i[0].bloque=1;rechazar(&f,VLIW_BLOQUES);
    iniciar(&f,2);f.e.cantidad_bloques=2;bloques(&f,0,0,2);bloques(&f,1,1,2);rechazar(&f,VLIW_BLOQUES);
    iniciar(&f,2);f.b[0].fin=1;rechazar(&f,VLIW_BLOQUES);
    iniciar(&f,1);f.b[0].fin=0;rechazar(&f,VLIW_BLOQUES);
    iniciar(&f,2);control(&f,0,VLIW_CONTROL_SALTO);rechazar(&f,VLIW_BLOQUES);
    iniciar(&f,1);f.sucesores[0][0]=1;f.b[0].sucesores=f.sucesores[0];f.b[0].cantidad_sucesores=1;rechazar(&f,VLIW_BLOQUES);
    iniciar(&f,1);C(vliw_planificar(NULL,&p,&d)==VLIW_ARGUMENTO && !p);
    C(vliw_planificar(&f.e,NULL,&d)==VLIW_ARGUMENTO);
    p=plan(&f);VliwPlan *original=p;
    C(vliw_planificar(&f.e,&p,NULL)==VLIW_ARGUMENTO && p==original);vliw_liberar(&p);
    iniciar(&f,1);f.e.instrucciones=NULL;rechazar(&f,VLIW_ARGUMENTO);
    iniciar(&f,1);f.e.cantidad_dependencias=SIZE_MAX;rechazar(&f,VLIW_ARGUMENTO);
}

static void salida_corrupta(void)
{
    /* No basta con validar nuestros propios planes: cada mutación ataca una
     * invariante diferente del verificador público y luego se restaura. */
    ++pruebas;Fixture f;iniciar(&f,2);unidad(&f,1,VLIW_SLOT_LSU);VliwPlan *p=plan(&f);VliwDiagnostico d;
    C(vliw_verificar(&f.e,NULL,&d)==VLIW_PLAN_INVALIDO);
    p->bundles[0].instrucciones[1]=0;C(vliw_verificar(&f.e,p,&d)==VLIW_PLAN_INVALIDO);p->bundles[0].instrucciones[1]=1;
    p->bundles[0].instrucciones[0]=VLIW_VACIO;C(vliw_verificar(&f.e,p,&d)==VLIW_PLAN_INVALIDO);p->bundles[0].instrucciones[0]=0;
    p->bundle_por_instruccion[1]=1;C(vliw_verificar(&f.e,p,&d)==VLIW_PLAN_INVALIDO);p->bundle_por_instruccion[1]=0;
    p->cantidad_vacios=1;C(vliw_verificar(&f.e,p,&d)==VLIW_PLAN_INVALIDO);p->cantidad_vacios=0;
    p->bundles[0].operaciones=1;C(vliw_verificar(&f.e,p,&d)==VLIW_PLAN_INVALIDO);p->bundles[0].operaciones=2;
    p->bundles[0].bloque=1;C(vliw_verificar(&f.e,p,&d)==VLIW_PLAN_INVALIDO);p->bundles[0].bloque=0;
    p->cantidad_bundles=1024;C(vliw_verificar(&f.e,p,&d)==VLIW_PLAN_INVALIDO);p->cantidad_bundles=1;
    f.i[0].escribe=f.i[1].escribe=R(5);C(vliw_verificar(&f.e,p,&d)==VLIW_PLAN_INVALIDO);
    f.i[0].escribe=0;f.i[0].lee=R(5);C(vliw_verificar(&f.e,p,&d)==VLIW_PLAN_INVALIDO);
    f.i[0].lee=0;f.i[0].escribe=R(5);f.i[1].escribe=0;f.i[1].lee=R(5);C(vliw_verificar(&f.e,p,&d)==VLIW_PLAN_INVALIDO);
    f.i[0].escribe=f.i[1].lee=0;f.i[0].efectos=VLIW_EF_BARRERA;C(vliw_verificar(&f.e,p,&d)==VLIW_PLAN_INVALIDO);
    vliw_liberar(&p);
}

static void capacidad_y_determinismo(void)
{
    ++pruebas;Fixture f;iniciar(&f,8);
    for(size_t i=1;i<8;i+=2)unidad(&f,i,VLIW_SLOT_LSU);
    VliwPlan *p=plan(&f);
    for(unsigned rep=0;rep<20;++rep){VliwPlan *q=plan(&f);
        C(p->cantidad_bundles==q->cantidad_bundles);
        for(size_t b=0;b<p->cantidad_bundles;++b)for(unsigned s=0;s<4;++s)C(p->bundles[b].instrucciones[s]==q->bundles[b].instrucciones[s]);
        C(!memcmp(p->bundle_por_instruccion,q->bundle_por_instruccion,8*sizeof(size_t)));vliw_liberar(&q);}
    C(p->cantidad_bundles==4 && p->cantidad_bundles<3*f.e.cantidad_instrucciones);vliw_liberar(&p);
    ++pruebas;size_t n=2046;VliwInstruccion *ins=calloc(n+1,sizeof(*ins));C(ins!=NULL);
    for(size_t i=0;i<=n;++i)ins[i]=(VliwInstruccion){.id=i,.slot=i%2?VLIW_SLOT_LSU:VLIW_SLOT_ALU,.efectos=i%2?VLIW_EF_LEE_MEMORIA:0};
    VliwBloque b={0,n,NULL,0};VliwEntrada e={ins,n,NULL,0,&b,1,true};VliwDiagnostico d;
    clock_t inicio=clock();C(vliw_planificar(&e,&p,&d)==VLIW_OK);
    C(p->cantidad_bundles==1023 && vliw_verificar(&e,p,&d)==VLIW_OK);
    printf("Rendimiento: %zu operaciones, %zu bundles, %.6f s CPU\n",n,p->cantidad_bundles,(double)(clock()-inicio)/CLOCKS_PER_SEC);
    vliw_liberar(&p);++b.fin;++e.cantidad_instrucciones;
    C(vliw_planificar(&e,&p,&d)==VLIW_CAPACIDAD && !p);
    e.cantidad_instrucciones=SIZE_MAX;C(vliw_planificar(&e,&p,&d)==VLIW_CAPACIDAD && !p);
    /* El límite incluye protección entre bloques: 1021 ALU más dos NOP
     * caben exactamente; una ALU adicional no puede invadir TRAP_VECTOR. */
    ++pruebas;
    for(size_t i=0;i<1022;++i)ins[i]=(VliwInstruccion){.id=i,.slot=VLIW_SLOT_ALU,.bloque=i?1:0};
    size_t sucesor=1;VliwBloque dos[]={{0,1,&sucesor,1},{1,1021,NULL,0}};
    e=(VliwEntrada){ins,1021,NULL,0,dos,2,true};
    C(vliw_planificar(&e,&p,&d)==VLIW_OK && p->cantidad_bundles==1023);
    C(p->inicio_por_bloque[1]==1 && p->bundle_por_instruccion[1]==3);vliw_liberar(&p);
    dos[1].fin=1022;e.cantidad_instrucciones=1022;
    C(vliw_planificar(&e,&p,&d)==VLIW_CAPACIDAD && !p);free(ins);
}

#ifdef VLIW_PRUEBA_RESERVAS
/* Interposición exclusiva del ejecutable de prueba: fuerza cada fallo de
 * malloc/calloc y comprueba que la biblioteca no publique ni pierda el plan. */
void *__real_malloc(size_t n);
void *__real_calloc(size_t n,size_t s);
static size_t reserva,fallar;
void *__wrap_malloc(size_t n){if(++reserva==fallar)return NULL;return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s){if(++reserva==fallar)return NULL;return __real_calloc(n,s);}
static void fallos_reserva(void)
{
    Fixture f;iniciar(&f,3);VliwDiagnostico d;
    size_t efectivos=0;
    for(size_t k=1;k<=32;++k){VliwPlan *p=NULL;reserva=0;fallar=k;
        VliwEstado s=vliw_planificar(&f.e,&p,&d);fallar=0;
        C(s==VLIW_OK || s==VLIW_MEMORIA);C((s==VLIW_OK)==(p!=NULL));efectivos+=s==VLIW_MEMORIA;vliw_liberar(&p);}
    C(efectivos>0 && efectivos<32);
    printf("Reservas: 32 posiciones de inyección; %zu fallos efectivos comprobados\n",efectivos);
}
#endif

static int fixture_archivo(const char *ruta)
{
    /* Protocolo numérico de fixtures para Python, no lenguaje ni parser ISA.
     * Las cadenas ensamblador permanecen en el productor de la prueba. */
    FILE *f=fopen(ruta,"r");C(f!=NULL);size_t n,m,k;
    C(fscanf(f,"%zu %zu %zu",&n,&m,&k)==3 && n<=4092 && m<=65536 && k<=4092);
    VliwInstruccion *ins=calloc(n?n:1,sizeof(*ins));VliwDependencia *ar=calloc(m?m:1,sizeof(*ar));
    VliwBloque *bb=calloc(k?k:1,sizeof(*bb));C(ins&&ar&&bb);
    for(size_t i=0;i<n;++i){unsigned slot,control_,ef;unsigned long long l,w;size_t block,id;
        C(fscanf(f,"%zu %u %llx %llx %u %u %zu",&id,&slot,&l,&w,&ef,&control_,&block)==7);
        ins[i]=(VliwInstruccion){id,(VliwSlot)slot,NULL,l,w,ef,(VliwControl)control_,block};}
    for(size_t a=0;a<m;++a){unsigned tipo;C(fscanf(f,"%zu %zu %u %zu",&ar[a].antes,&ar[a].despues,&tipo,&ar[a].distancia)==4);ar[a].tipo=(VliwTipoDependencia)tipo;}
    for(size_t b=0;b<k;++b){size_t ns;C(fscanf(f,"%zu %zu %zu",&bb[b].inicio,&bb[b].fin,&ns)==3 && ns<=k);
        size_t *su=calloc(ns?ns:1,sizeof(size_t));C(su!=NULL);for(size_t j=0;j<ns;++j)C(fscanf(f,"%zu",&su[j])==1);bb[b].sucesores=su;bb[b].cantidad_sucesores=ns;}
    C(fclose(f)==0);VliwEntrada e={ins,n,ar,m,bb,k,true};VliwPlan *p=NULL;VliwDiagnostico d;
    VliwEstado estado=vliw_planificar(&e,&p,&d);printf("{\"estado\":%d,\"mensaje\":\"%s\"",estado,d.mensaje);
    if(p){C(vliw_verificar(&e,p,&d)==VLIW_OK);printf(",\"vacios\":%zu,\"mapa\":[",p->cantidad_vacios);
        for(size_t i=0;i<n;++i)printf("%s%zu",i?",":"",p->bundle_por_instruccion[i]);
        printf("],\"bloques\":[");for(size_t b=0;b<k;++b)printf("%s%zu",b?",":"",p->inicio_por_bloque[b]);
        printf("],\"bundles\":[");for(size_t b=0;b<p->cantidad_bundles;++b){printf("%s[",b?",":"");for(unsigned s=0;s<4;++s){if(s)putchar(',');if(p->bundles[b].instrucciones[s]==VLIW_VACIO)printf("null");else printf("%zu",p->bundles[b].instrucciones[s]);}putchar(']');}putchar(']');}
    puts("}");vliw_liberar(&p);for(size_t b=0;b<k;++b)free((void *)bb[b].sucesores);free(bb);free(ar);free(ins);return 0;
}
int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"fixture"))return fixture_archivo(argv[2]);
#ifdef VLIW_PRUEBA_RESERVAS
    if(argc==2 && !strcmp(argv[1],"reservas")){fallos_reserva();return 0;}
#endif
    C(argc==1);casos_basicos();riesgos_y_disponibilidad();efectos_y_cfg();entradas_invalidas();salida_corrupta();capacidad_y_determinismo();
    printf("S7: %zu escenarios, %zu comprobaciones PASS\n",pruebas,comprobaciones);return 0;
}
