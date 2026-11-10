/* Comprueba emisión exacta, rechazos atómicos y coherencia con CB/S2.
 * Los metadatos son fixtures explícitos, no un analizador de dependencias. */
#include "vliw_emisor.h"
#include "codificacion_binaria.h"
#include "sim_decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R(x) (UINT64_C(1)<<(x))
static size_t casos,checks;
#define C(x) do { ++checks; if(!(x)){fprintf(stderr,"FAIL S8-A %d: %s\n",__LINE__,#x);exit(1);} } while(0)
typedef struct {
    VliwInstruccion i[16];VliwDependencia a[32];VliwBloque b[8];size_t su[8][8];
    VliwDestino t[16];VliwEntrada e;VliwDestinos d;
} Fixture;
static void iniciar(Fixture *f,size_t n)
{
    memset(f,0,sizeof(*f));C(n<=16);
    f->e=(VliwEntrada){f->i,n,f->a,0,f->b,n?1:0,true};f->d=(VliwDestinos){f->t,0};f->b[0]=(VliwBloque){0,n,NULL,0};
    for(size_t i=0;i<n;++i)f->i[i]=(VliwInstruccion){.id=100+3*i,.slot=VLIW_SLOT_ALU,.texto="sumai x0,x0,0"};
}
static VliwPlan *plan(Fixture *f)
{
    VliwPlan *p=NULL;VliwDiagnostico d;VliwEstado s=vliw_planificar(&f->e,&p,&d);
    if(s)fprintf(stderr,"S7: %s\n",d.mensaje);
    C(s==VLIW_OK && p);return p;
}
static void bloque(Fixture *f,size_t b,size_t inicio,size_t fin)
{
    f->b[b]=(VliwBloque){inicio,fin,f->su[b],0};for(size_t i=inicio;i<fin;++i)f->i[i].bloque=b;
}
static void salto(Fixture *f,size_t i,const char *texto,VliwControl control,VliwDestinoTipo tipo,size_t tomado,size_t caida)
{
    f->i[i].slot=VLIW_SLOT_BRU;f->i[i].texto=texto;f->i[i].control=control;
    if(control==VLIW_CONTROL_LLAMADA)f->i[i].efectos=VLIW_EF_DESCONOCIDOS;
    f->t[f->d.cantidad++]=(VliwDestino){i,tipo,tomado,caida};
}
static char *emitir(Fixture *f,VliwPlan *p)
{
    char *s=NULL;VliwEmisorDiagnostico d;VliwEmisorEstado e=vliw_emitir(&f->e,p,&f->d,&s,&d);
    if(e)fprintf(stderr,"emisor: %s\n",d.mensaje);
    C(e==VLIW_EM_OK && s);return s;
}
static void rechazar(Fixture *f,VliwPlan *p,VliwEmisorEstado esperado)
{
    char *s=NULL;VliwEmisorDiagnostico d;VliwEmisorEstado e=vliw_emitir(&f->e,p,&f->d,&s,&d);
    if(e!=esperado)fprintf(stderr,"estado %d esperado %d: %s\n",e,esperado,d.mensaje);
    C(e==esperado && !s && d.estado==esperado && *d.mensaje);
}
static size_t lineas_bundle(const char *s)
{
    size_t n=0;for(const char *p=s;*p;){const char *fin=strchr(p,'\n');C(fin!=NULL);
        if(memchr(p,'|',(size_t)(fin-p)))++n;
        p=fin+1;}return n;
}
static CbPrograma codificar(const char *s)
{
    CbPrograma p={0};C(cb_ensamblar(s,&p,stderr)==0);return p;
}

static void texto_y_layout(void)
{
    Fixture f;VliwPlan *p;char *s;
    ++casos;iniciar(&f,1);f.i[0].texto="sumai x5,x0,10";f.i[0].escribe=R(5);p=plan(&f);s=emitir(&f,p);
    C(!strcmp(s,"__vliw_b0:\nsumai x5,x0,10 | nop | nop | nop\n"));C(lineas_bundle(s)==1);
    vliw_emisor_liberar(&s);vliw_emisor_liberar(&s);vliw_emisor_liberar(NULL);vliw_liberar(&p);
    ++casos;iniciar(&f,2);f.i[0].texto="sumai x5,x0,10";f.i[0].escribe=R(5);
    f.i[1]=(VliwInstruccion){.id=307,.slot=VLIW_SLOT_LSU,.texto="cargai x8,x3,0",.lee=R(3),.escribe=R(8),.efectos=VLIW_EF_LEE_MEMORIA};
    p=plan(&f);s=emitir(&f,p);C(!strcmp(s,"__vliw_b0:\nsumai x5,x0,10 | cargai x8,x3,0 | nop | nop\n"));vliw_emisor_liberar(&s);vliw_liberar(&p);
    /* RAW preserva los dos bundles vacíos y el índice3 del consumidor. */
    ++casos;iniciar(&f,2);f.i[0].texto="sumai x5,x0,10";f.i[0].escribe=R(5);
    f.i[1].texto="suma x6,x5,x0";f.i[1].lee=R(5);f.i[1].escribe=R(6);
    f.a[f.e.cantidad_dependencias++]=(VliwDependencia){0,1,VLIW_DEP_RAW,3};p=plan(&f);s=emitir(&f,p);
    C(!strcmp(s,"__vliw_b0:\nsumai x5,x0,10 | nop | nop | nop\nnop | nop | nop | nop\nnop | nop | nop | nop\nsuma x6,x5,x0 | nop | nop | nop\n"));
    C(lineas_bundle(s)==p->cantidad_bundles);vliw_emisor_liberar(&s);vliw_liberar(&p);
    ++casos;iniciar(&f,0);p=plan(&f);s=emitir(&f,p);C(!*s && !lineas_bundle(s));vliw_emisor_liberar(&s);vliw_liberar(&p);
    ++casos;iniciar(&f,2);f.i[0].texto="suma x0,x5,x0";f.i[0].lee=R(5);
    f.i[1].texto="sumai x5,x0,2";f.i[1].escribe=R(5);f.a[f.e.cantidad_dependencias++]=(VliwDependencia){0,1,VLIW_DEP_WAR,1};
    p=plan(&f);s=emitir(&f,p);C(strstr(s,"suma x0,x5,x0 | nop | nop | nop\nsumai x5,x0,2"));vliw_emisor_liberar(&s);vliw_liberar(&p);
    ++casos;iniciar(&f,2);f.i[0].texto="sumai x5,x0,1";f.i[1].texto="sumai x5,x0,2";f.i[0].escribe=f.i[1].escribe=R(5);
    f.a[f.e.cantidad_dependencias++]=(VliwDependencia){0,1,VLIW_DEP_WAW,1};p=plan(&f);s=emitir(&f,p);C(lineas_bundle(s)==2);vliw_emisor_liberar(&s);vliw_liberar(&p);
}
static void etiquetas_y_saltos(void)
{
    Fixture f;VliwPlan *p;char *s;
    /* El destino no es el primer sucesor. El desplazamiento antiguo999 se
     * descarta; CB calcula el desplazamiento negativo al primer NOP de B0. */
    ++casos;iniciar(&f,2);f.e.cantidad_bloques=2;bloque(&f,0,0,1);bloque(&f,1,1,2);
    f.su[0][0]=1;f.su[0][1]=0;f.b[0].cantidad_sucesores=2;
    salto(&f,0,"igualno x0,x0,999",VLIW_CONTROL_SALTO,VLIW_DEST_CONDICIONAL,0,1);
    p=plan(&f);s=emitir(&f,p);C(strstr(s,"igualno x0,x0,__vliw_b0"));
    C(strstr(s,"__vliw_b0:\nnop | nop | nop | nop\nnop | nop | nop | nop\n"));
    CbPrograma cb=codificar(s);SimInstruccion i;C(sim_decode_palabra(cb.bundles[p->bundle_por_instruccion[0]].slots[2],2,&i)==SIM_DECODE_OK);
    C((int32_t)(16*p->inicio_por_bloque[0])-(int32_t)(16*p->bundle_por_instruccion[0])==i.inmediato);
    cb_liberar(&cb);vliw_emisor_liberar(&s);vliw_liberar(&p);
    ++casos;iniciar(&f,2);f.e.cantidad_bloques=2;bloque(&f,0,0,1);bloque(&f,1,1,2);
    f.su[0][0]=1;f.b[0].cantidad_sucesores=1;salto(&f,0,"sye x0,-16",VLIW_CONTROL_SALTO,VLIW_DEST_DIRECTO,1,VLIW_VACIO);
    p=plan(&f);s=emitir(&f,p);C(strstr(s,"sye x0,__vliw_b1"));C(strstr(s,"__vliw_b1:\nnop | nop | nop | nop\nnop | nop | nop | nop\n"));
    cb=codificar(s);C(sim_decode_palabra(cb.bundles[0].slots[2],2,&i)==SIM_DECODE_OK && i.inmediato==16);cb_liberar(&cb);
    /* No se deriva un bloque del entero viejo y las rutas deben coincidir. */
    f.t[0].tomado=2;rechazar(&f,p,VLIW_EM_DESTINO);f.t[0].tomado=0;rechazar(&f,p,VLIW_EM_DESTINO);f.t[0].tomado=1;
    f.d.cantidad=0;rechazar(&f,p,VLIW_EM_DESTINO);f.d.cantidad=1;
    f.t[0].tipo=VLIW_DEST_NO_RESUELTO;rechazar(&f,p,VLIW_EM_NO_RESUELTO);f.t[0].tipo=VLIW_DEST_DIRECTO;
    vliw_emisor_liberar(&s);vliw_liberar(&p);
    /* Retornos y syer permanecen indirectos, byte a byte; no se inventa una
     * etiqueta tomada ni se transforma su offset en desplazamiento de PC. */
    ++casos;iniciar(&f,1);salto(&f,0,"syer x0,x2,-16",VLIW_CONTROL_SALTO,VLIW_DEST_INDIRECTO,VLIW_VACIO,VLIW_VACIO);f.i[0].lee=R(2);
    p=plan(&f);s=emitir(&f,p);C(strstr(s,"nop | nop | syer x0,x2,-16 | nop"));vliw_emisor_liberar(&s);vliw_liberar(&p);
    ++casos;iniciar(&f,1);salto(&f,0,"ret",VLIW_CONTROL_RETORNO,VLIW_DEST_RETORNO,VLIW_VACIO,VLIW_VACIO);f.i[0].lee=R(1);
    p=plan(&f);s=emitir(&f,p);C(strstr(s,"nop | nop | ret | nop"));vliw_emisor_liberar(&s);vliw_liberar(&p);
}
static void cripto_y_preservacion(void)
{
    ++casos;Fixture f;iniciar(&f,6);
    const char *textos[]={"setpwd x0","vcr 0","ell 0,0,x2,x4","fsl x6,x2,0,0","fsli x8,x6,0,0","camcon 0,7"};
    const unsigned ef[]={28,9,20,16,16,23};
    for(size_t j=0;j<6;++j){f.i[j].slot=VLIW_SLOT_CRIPTO;f.i[j].texto=textos[j];f.i[j].efectos=ef[j];}
    f.i[2].lee=R(2)|R(4);f.i[3].lee=R(2)|R(3);f.i[3].escribe=R(6)|R(7);
    f.i[4].lee=R(6)|R(7);f.i[4].escribe=R(8)|R(9);f.a[f.e.cantidad_dependencias++]=(VliwDependencia){3,4,VLIW_DEP_RAW,3};
    VliwPlan *p=plan(&f);VliwPlan plan_copia=*p;size_t inicio=p->inicio_por_bloque[0];
    Fixture copia=f;size_t mapa[6];memcpy(mapa,p->bundle_por_instruccion,sizeof(mapa));
    VliwBundle *bundles=malloc(p->cantidad_bundles*sizeof(*bundles));C(bundles!=NULL);memcpy(bundles,p->bundles,p->cantidad_bundles*sizeof(*bundles));
    char *s=emitir(&f,p);
    for(size_t j=0;j<6;++j){char linea[200];snprintf(linea,sizeof(linea),"nop | nop | nop | %s\n",textos[j]);C(strstr(s,linea)!=NULL);}
    C(!memcmp(p,&plan_copia,sizeof(*p)) && p->inicio_por_bloque[0]==inicio);
    C(!memcmp(&f,&copia,sizeof(f)));C(!memcmp(mapa,p->bundle_por_instruccion,sizeof(mapa)));C(!memcmp(bundles,p->bundles,p->cantidad_bundles*sizeof(*bundles)));
    ++casos;for(unsigned k=0;k<20;++k){char *otra=emitir(&f,p);C(!strcmp(s,otra));vliw_emisor_liberar(&otra);}
    free(bundles);vliw_emisor_liberar(&s);vliw_liberar(&p);
}
static void rechazos(void)
{
    Fixture f;iniciar(&f,1);VliwPlan *p=plan(&f);char *s=NULL;VliwEmisorDiagnostico d;
    ++casos;C(vliw_emitir(NULL,p,&f.d,&s,&d)==VLIW_EM_ARGUMENTO && !s);
    C(vliw_emitir(&f.e,NULL,&f.d,&s,&d)==VLIW_EM_ARGUMENTO && !s);
    C(vliw_emitir(&f.e,p,&f.d,NULL,&d)==VLIW_EM_ARGUMENTO);
    s=emitir(&f,p);char *anterior=s;C(vliw_emitir(&f.e,p,&f.d,&s,NULL)==VLIW_EM_ARGUMENTO && s==anterior);vliw_emisor_liberar(&s);
    ++casos;f.i[0].texto=NULL;rechazar(&f,p,VLIW_EM_TEXTO);
    const char *malos[]={"","  ","sumai x0,x0,0\nnop","nop\r","nop | nop","etiqueta: nop","nop # comentario","nop ; comentario","nop // comentario","nop\\n","nop\v","nop\xff"};
    ++casos;for(size_t i=0;i<sizeof(malos)/sizeof(*malos);++i){f.i[0].texto=malos[i];rechazar(&f,p,VLIW_EM_TEXTO);}
    f.i[0].texto="xyz x0,x0,0";rechazar(&f,p,VLIW_EM_CODIFICADOR);
    f.i[0].texto="nop";rechazar(&f,p,VLIW_EM_TEXTO);
    f.i[0].texto="sumai x1,x0,0";rechazar(&f,p,VLIW_EM_METADATOS);
    char largo[161];memset(largo,' ',160);largo[160]=0;f.i[0].texto=largo;rechazar(&f,p,VLIW_EM_TEXTO);f.i[0].texto="sumai x0,x0,0";
    ++casos;p->bundles[0].instrucciones[0]=99;rechazar(&f,p,VLIW_EM_PLAN);p->bundles[0].instrucciones[0]=0;
    p->bundle_por_instruccion[0]=1;rechazar(&f,p,VLIW_EM_PLAN);p->bundle_por_instruccion[0]=0;
    p->bundles[0].operaciones=2;rechazar(&f,p,VLIW_EM_PLAN);p->bundles[0].operaciones=1;
    p->bundles[0].instrucciones[1]=0;rechazar(&f,p,VLIW_EM_PLAN);p->bundles[0].instrucciones[1]=VLIW_VACIO;
    p->cantidad_bundles=1024;rechazar(&f,p,VLIW_EM_PLAN);p->cantidad_bundles=1;
    f.t[0]=(VliwDestino){0,VLIW_DEST_DIRECTO,0,VLIW_VACIO};f.d.cantidad=1;rechazar(&f,p,VLIW_EM_DESTINO);vliw_liberar(&p);
    ++casos;iniciar(&f,2);f.e.cantidad_bloques=2;bloque(&f,0,0,1);bloque(&f,1,1,2);f.su[0][0]=1;f.b[0].cantidad_sucesores=1;
    salto(&f,0,"igualsi x0,x0,0",VLIW_CONTROL_SALTO,VLIW_DEST_CONDICIONAL,1,1);p=plan(&f);
    f.t[1]=f.t[0];f.d.cantidad=2;rechazar(&f,p,VLIW_EM_DESTINO);f.d.cantidad=1;
    f.t[0].caida=0;rechazar(&f,p,VLIW_EM_DESTINO);f.t[0].caida=1;
    f.i[0].texto="igualsi x0,x0,?";rechazar(&f,p,VLIW_EM_TEXTO);
    f.i[0].texto="igualsi x0,x0,";rechazar(&f,p,VLIW_EM_TEXTO);
    f.i[0].texto="sye x0,0";rechazar(&f,p,VLIW_EM_DESTINO);
    f.i[0].texto="igualsi x0,x0,0";
    p->bundles[p->inicio_por_bloque[1]].instrucciones[0]=1;rechazar(&f,p,VLIW_EM_PLAN);
    p->bundles[p->inicio_por_bloque[1]].instrucciones[0]=VLIW_VACIO;vliw_liberar(&p);
}
static void cfg_y_control_oculto(void)
{
    /* Una máscara de registros correcta no basta para ocultar una BRU ni
     * para inventar aristas que el programa emitido no podrá recorrer. */
    ++casos;Fixture f;iniciar(&f,1);f.i[0].slot=VLIW_SLOT_BRU;f.i[0].texto="sye x0,0";
    f.i[0].control=VLIW_CONTROL_SALTO;VliwPlan *p=plan(&f);
    f.i[0].control=VLIW_CONTROL_NINGUNO;rechazar(&f,p,VLIW_EM_PLAN);vliw_liberar(&p);
    ++casos;iniciar(&f,2);f.e.cantidad_bloques=2;bloque(&f,0,0,1);bloque(&f,1,1,2);
    f.su[0][0]=1;f.b[0].cantidad_sucesores=1;p=plan(&f);
    f.su[0][0]=0;rechazar(&f,p,VLIW_EM_PLAN);vliw_liberar(&p);
}
static void limite_y_rango(void)
{
    /* No se relajan ramas: un condicional de más de1023 bytes se rechaza por
     * CB, conservando el layout S7. El mismo destino sí cabe en sye16bits. */
    ++casos;size_t n=70;VliwInstruccion *ins=calloc(1023,sizeof(*ins));C(ins!=NULL);
    for(size_t i=0;i<1023;++i)ins[i]=(VliwInstruccion){.id=i,.slot=VLIW_SLOT_ALU,.texto="sumai x0,x0,0",.bloque=i==0?0:i<69?1:2};
    ins[0].slot=VLIW_SLOT_BRU;ins[0].control=VLIW_CONTROL_SALTO;ins[0].texto="igualsi x0,x0,0";
    size_t su0[]={1,2},su1=2;VliwBloque bb[]={{0,1,su0,2},{1,69,&su1,1},{69,70,NULL,0}};
    VliwEntrada e={ins,n,NULL,0,bb,3,true};VliwPlan *p=NULL;VliwDiagnostico sd;C(vliw_planificar(&e,&p,&sd)==VLIW_OK);
    VliwDestino t={0,VLIW_DEST_CONDICIONAL,2,1};VliwDestinos dest={&t,1};char *s=NULL;VliwEmisorDiagnostico d;
    C(vliw_emitir(&e,p,&dest,&s,&d)==VLIW_EM_RANGO && !s && strstr(d.mensaje,"relajación"));
    ins[0].texto="sye x0,0";bb[0].sucesores=&su0[1];bb[0].cantidad_sucesores=1;t.tipo=VLIW_DEST_DIRECTO;t.caida=VLIW_VACIO;
    C(vliw_emitir(&e,p,&dest,&s,&d)==VLIW_EM_OK && s);vliw_emisor_liberar(&s);vliw_liberar(&p);
    ++casos;for(size_t i=0;i<1023;++i)ins[i]=(VliwInstruccion){.id=i,.slot=VLIW_SLOT_ALU,.texto="sumai x0,x0,0"};
    VliwBloque uno={0,1023,NULL,0};e=(VliwEntrada){ins,1023,NULL,0,&uno,1,true};C(vliw_planificar(&e,&p,&sd)==VLIW_OK);
    C(vliw_emitir(&e,p,NULL,&s,&d)==VLIW_EM_OK && lineas_bundle(s)==1023);
    p->cantidad_bundles=1024;char *otra=NULL;C(vliw_emitir(&e,p,NULL,&otra,&d)==VLIW_EM_PLAN && !otra);p->cantidad_bundles=1023;
    vliw_emisor_liberar(&s);vliw_liberar(&p);free(ins);
}

#ifdef VLIW_EMISOR_FALLOS
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
FILE *__real_tmpfile(void);int __real_fflush(FILE *);int __real_fseek(FILE *,long,int);int __real_fclose(FILE *);
static size_t reserva,fallar;static unsigned io;
void *__wrap_malloc(size_t n){if(++reserva==fallar)return NULL;return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s){if(++reserva==fallar)return NULL;return __real_calloc(n,s);}
void *__wrap_realloc(void *p,size_t n){if(++reserva==fallar)return NULL;return __real_realloc(p,n);}
FILE *__wrap_tmpfile(void){return io==1?NULL:__real_tmpfile();}
int __wrap_fflush(FILE *f){return io==2?EOF:__real_fflush(f);}
int __wrap_fseek(FILE *f,long n,int w){return io==3?-1:__real_fseek(f,n,w);}
int __wrap_fclose(FILE *f){int r=__real_fclose(f);return io==4?EOF:r;}
static void fallos(void)
{
    Fixture f;iniciar(&f,3);VliwPlan *p=plan(&f);size_t efectivos=0;VliwEmisorDiagnostico d;
    /* Solo se inyecta durante emitir: el plan original sobrevive a todos los
     * intentos y cualquier fallo deja la salida NULL. Incluye reservas de CB. */
    for(size_t k=1;k<=96;++k){char *s=NULL;reserva=0;fallar=k;VliwEmisorEstado e=vliw_emitir(&f.e,p,&f.d,&s,&d);fallar=0;
        C(e==VLIW_EM_OK || e==VLIW_EM_MEMORIA);C((e==VLIW_EM_OK)==(s!=NULL));efectivos+=e==VLIW_EM_MEMORIA;vliw_emisor_liberar(&s);}
    for(io=1;io<=4;++io){char *s=NULL;C(vliw_emitir(&f.e,p,&f.d,&s,&d)==VLIW_EM_IO && !s);}io=0;
    C(efectivos>0 && efectivos<96);vliw_liberar(&p);
    printf("Fallos: 96 posiciones de reserva, %zu fallos efectivos, 4 fallos IO PASS\n",efectivos);
}
#endif


/* Puente numérico de fixtures: recibe metadatos ya construidos y texto literal.
 * No interpreta ISA. S7 y el emisor producen el único ensamblador optimizado. */
static int fixture(const char *ruta,const char *salida)
{
    FILE *f=fopen(ruta,"rb");C(f!=NULL);
    size_t n,m,k,nt;C(fscanf(f,"%zu%zu%zu%zu",&n,&m,&k,&nt)==4);
    C(n<=1023 && m<=10000 && k<=1023 && nt<=n);
    VliwInstruccion *in=calloc(n+1,sizeof(*in));
    VliwDependencia *ar=calloc(m+1,sizeof(*ar));VliwBloque *bl=calloc(k+1,sizeof(*bl));
    VliwDestino *ds=calloc(nt+1,sizeof(*ds));char (*textos)[160]=calloc(n+1,sizeof(*textos));
    C(in && ar && bl && ds && textos);
    for(size_t i=0;i<n;++i){unsigned slot,ctrl;unsigned long long lee,escribe;
        C(fscanf(f,"%zu%u%llx%llx%u%u%zu",&in[i].id,&slot,&lee,&escribe,&in[i].efectos,&ctrl,&in[i].bloque)==7);
        in[i].slot=(VliwSlot)slot;in[i].control=(VliwControl)ctrl;in[i].lee=lee;in[i].escribe=escribe;
    }
    for(size_t i=0;i<m;++i){unsigned tipo;C(fscanf(f,"%zu%zu%u%zu",&ar[i].antes,&ar[i].despues,&tipo,&ar[i].distancia)==4);ar[i].tipo=(VliwTipoDependencia)tipo;}
    for(size_t b=0;b<k;++b){C(fscanf(f,"%zu%zu%zu",&bl[b].inicio,&bl[b].fin,&bl[b].cantidad_sucesores)==3);C(bl[b].cantidad_sucesores<=k);
        size_t *su=calloc(bl[b].cantidad_sucesores+1,sizeof(*su));C(su!=NULL);bl[b].sucesores=su;
        for(size_t j=0;j<bl[b].cantidad_sucesores;++j)C(fscanf(f,"%zu",&su[j])==1);
    }
    for(size_t t=0;t<nt;++t){unsigned tipo;C(fscanf(f,"%zu%u%zu%zu",&ds[t].instruccion,&tipo,&ds[t].tomado,&ds[t].caida)==4);ds[t].tipo=(VliwDestinoTipo)tipo;}
    int c;do{c=fgetc(f);}while(c!=EOF && c!='\n');
    for(size_t i=0;i<n;++i){C(fgets(textos[i],160,f)!=NULL);size_t l=strlen(textos[i]);C(l && textos[i][l-1]=='\n');textos[i][l-1]=0;in[i].texto=textos[i];}
    C(fclose(f)==0);VliwEntrada e={in,n,ar,m,bl,k,true};VliwPlan *p=NULL;VliwDiagnostico sd;
    C(vliw_planificar(&e,&p,&sd)==VLIW_OK);VliwDestinos d={ds,nt};VliwEmisorDiagnostico ed;char *s=NULL;
    VliwEmisorEstado estado=vliw_emitir(&e,p,&d,&s,&ed);
    if(estado)fprintf(stderr,"emisor %d: %s\n",estado,ed.mensaje);
    if(!estado){f=fopen(salida,"wb");C(f!=NULL);C(fwrite(s,1,strlen(s),f)==strlen(s));C(fclose(f)==0);}
    printf("{\"estado\":%d,\"vacios\":%zu,\"bundles\":[",estado,p->cantidad_vacios);
    for(size_t q=0;q<p->cantidad_bundles;++q){if(q)putchar(',');putchar('[');for(unsigned j=0;j<4;++j){if(j)putchar(',');size_t v=p->bundles[q].instrucciones[j];if(v==VLIW_VACIO)fputs("null",stdout);else printf("%zu",v);}putchar(']');}
    fputs("],\"mapa\":[",stdout);for(size_t i=0;i<n;++i)printf("%s%zu",i?",":"",p->bundle_por_instruccion[i]);
    fputs("],\"bloques\":[",stdout);for(size_t b=0;b<k;++b)printf("%s%zu",b?",":"",p->inicio_por_bloque[b]);puts("]}");
    vliw_emisor_liberar(&s);vliw_liberar(&p);for(size_t b=0;b<k;++b)free((void *)bl[b].sucesores);
    free(in);free(ar);free(bl);free(ds);free(textos);return estado?1:0;
}

int main(int argc,char **argv)
{
#ifdef VLIW_EMISOR_FALLOS
    if(argc==2 && !strcmp(argv[1],"fallos")){fallos();return 0;}
#endif
    if(argc==4 && !strcmp(argv[1],"fixture"))return fixture(argv[2],argv[3]);
    C(argc==1);texto_y_layout();etiquetas_y_saltos();cripto_y_preservacion();rechazos();cfg_y_control_oculto();limite_y_rango();
    printf("S8-A: %zu escenarios, %zu comprobaciones PASS\n",casos,checks);return 0;
}
