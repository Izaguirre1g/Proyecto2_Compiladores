/* Verifica proyección propia con ISA explícita, CFG/DAG reales e I1-A1.
 * Los fixtures aportan símbolos y créditos locales documentados, nunca BAL.
 * Los oráculos comprueban campos y vida útil; no comparan padding indefinido.
 * Puede incluirse desde la suite de cadena para reutilizar solo estos fixtures. */
#include "vliw_proyeccion_fisica.h"
#include "codificacion_binaria.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define VAC VLIW_VACIO
#define MAX 16
static size_t escenarios,comprobaciones;
#define EXIGIR(x) do{++comprobaciones;if(!(x)){fprintf(stderr,"línea %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#ifdef FIS_FALLOS
static size_t llamadas,fallar;static bool activo;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
/* Interceptamos exclusivamente la llamada evaluada. Preparar los datos y
 * comprobarlos después queda fuera, para no confundir el oráculo con el API. */
void *__wrap_malloc(size_t n){if(activo && ++llamadas==fallar)return NULL;return __real_malloc(n);}
/* Las salidas parciales usan calloc: fallarlo exige liberar los hijos previos. */
void *__wrap_calloc(size_t n,size_t s){if(activo && ++llamadas==fallar)return NULL;return __real_calloc(n,s);}
/* Realloc fallido debe conservar el puntero anterior para la limpieza. */
void *__wrap_realloc(void *p,size_t n){if(activo && ++llamadas==fallar)return NULL;return __real_realloc(p,n);}
#endif
typedef struct {
    VliwCfgOperacion op[MAX];VliwMetadatosParciales meta[MAX];unsigned efectos[MAX];
    VliwCfgFuncion fn[2];VliwCfgSimbolo simbolos[MAX];VliwCfgTransferencia transferencias[MAX];
    size_t fronteras[MAX],posibles[MAX];char texto[MAX][160];const char *textos[MAX];
    VliwCfgEntrada flujo;VliwCfg *cfg;VliwDag *dag;VliwProyeccionEntrada entrada;
} Fisica;
/* CB prepara palabras, no expectativas. Las operaciones aisladas se ubican en
 * su slot, con NOP en los otros tres; el flujo lógico usa posiciones e IDs dispersos. */
static void operacion(Fisica *f,size_t i,unsigned slot,const char *s)
{
    const char *ss[4]={"nop","nop","nop","nop"};ss[slot]=s;char fuente[256];
    snprintf(fuente,sizeof fuente,"%s | %s | %s | %s\n",ss[0],ss[1],ss[2],ss[3]);
    CbPrograma cb={0};EXIGIR(cb_ensamblar(fuente,&cb,stderr)==0);EXIGIR(cb.cantidad==1);
    SimInstruccion real;EXIGIR(sim_decode_palabra(cb.bundles[0].slots[slot],slot,&real)==SIM_DECODE_OK);
    f->op[i]=(VliwCfgOperacion){100+7*i,cb.bundles[0].slots[slot],(VliwSlot)slot,real.salto,
        slot==2?VLIW_CONTROL_SALTO:VLIW_CONTROL_NINGUNO};
    snprintf(f->texto[i],sizeof f->texto[i],"%s",s);f->textos[i]=f->texto[i];cb_liberar(&cb);
}
/* Un único intervalo de función cubre inicialmente todo el flujo; cada ALU
 * escribe un GPR distinto. Los créditos solo describen estos datos preparados. */
static void iniciar_fisica(Fisica *f,size_t n)
{
    memset(f,0,sizeof *f);EXIGIR(n<=MAX);
    f->fn[0]=(VliwCfgFuncion){10,0,n};
    f->flujo=(VliwCfgEntrada){.operaciones=f->op,.cantidad_operaciones=n,.funciones=f->fn,
        .cantidad_funciones=n?1:0,.simbolos=f->simbolos,.transferencias=f->transferencias,
        .fronteras=f->fronteras,.generacion=71,.generacion_referencias=71,.fronteras_acreditadas=true};
    for(size_t i=0;i<n;++i){char s[64];snprintf(s,sizeof s,"sumai x%zu,x0,7",4+i);operacion(f,i,0,s);}
}
/* Declara símbolo y transferencia lógicos; el offset textual cero es solo el
 * marcador que S8-A reemplazará. Nunca se calcula el destino desde ese cero. */
static void salto_fisico(Fisica *f,size_t i,size_t destino,bool cond)
{
    operacion(f,i,2,cond?"igualsi x0,x0,0":"sye x0,0");size_t id=500+i;
    f->simbolos[f->flujo.cantidad_simbolos++]=(VliwCfgSimbolo){id,10,destino};
    f->transferencias[f->flujo.cantidad_transferencias++]=(VliwCfgTransferencia){i,
        cond?VLIW_DEST_CONDICIONAL:VLIW_DEST_DIRECTO,id,cond?i+1:VAC,NULL,0,true};
}
/* Call/ret acredita forma y cobertura estática del fixture. No demuestra que
 * main termine: la continuación gira sobre sí misma. SYE recibe layout nuevo;
 * SYER usa x1 publicado por la llamada, sin constantes antiguas de x13. */
static void llamada_fisica(Fisica *f)
{
    iniciar_fisica(f,3);operacion(f,0,2,"sye x1,0");f->op[0].control=VLIW_CONTROL_LLAMADA;
    salto_fisico(f,1,1,false);operacion(f,2,2,"syer x0,x1,0");f->op[2].control=VLIW_CONTROL_RETORNO;
    f->fn[0]=(VliwCfgFuncion){10,0,2};f->fn[1]=(VliwCfgFuncion){20,2,3};f->flujo.cantidad_funciones=2;
    f->simbolos[f->flujo.cantidad_simbolos++]=(VliwCfgSimbolo){600,20,2};f->posibles[0]=501;
    f->transferencias[f->flujo.cantidad_transferencias++]=(VliwCfgTransferencia){0,VLIW_DEST_DIRECTO,600,1,NULL,0,true};
    f->transferencias[f->flujo.cantidad_transferencias++]=(VliwCfgTransferencia){2,VLIW_DEST_RETORNO,VAC,VAC,f->posibles,1,true};
    f->efectos[0]=VLIW_EF_DESCONOCIDOS;
}
/* Extrae y construye mediante las APIs existentes; no hay aristas manuales.
 * El test suministra efectos/ABI/completitud por los datos explícitos anteriores. */
static void preparar_fisica(Fisica *f)
{
    size_t n=f->flujo.cantidad_operaciones;
    for(size_t i=0;i<n;++i)EXIGIR(vliw_metadatos_extraer(f->op[i].palabra,(unsigned)f->op[i].slot,&f->meta[i],NULL)==SIM_DECODE_OK);
    EXIGIR(vliw_cfg_construir(&f->flujo,&f->cfg,NULL)==VLIW_CFG_OK);
    VliwDagEntrada de={&f->flujo,f->cfg,f->meta,f->efectos,n,71,0,true,true};
    EXIGIR(vliw_dag_construir(&de,&f->dag,NULL)==VLIW_DAG_OK);
    f->entrada=(VliwProyeccionEntrada){de,f->dag,f->textos,n,{0,true,true}};
}
/* Cada campo de metadatos es significativo; la representación publicada por
 * asignación puede dejar relleno indeterminado y no se utiliza como oráculo. */
static void conservar_meta(const VliwMetadatosParciales *a,const VliwMetadatosParciales *b)
{
    EXIGIR(a->operacion==b->operacion && a->familia==b->familia && a->slot==b->slot);
    EXIGIR(a->lee==b->lee && a->escribe==b->escribe && a->efectos_minimos==b->efectos_minimos);
    EXIGIR(a->salto==b->salto && a->requiere_auth==b->requiere_auth && a->requiere_init==b->requiere_init && a->pendientes==b->pendientes);
}
/* Contrasta todos los miembros del descriptor y las tablas de entrada,
 * incluso los elementos inactivos del almacenamiento fijo. Ningún supuesto
 * sobre padding se utiliza para demostrar que los préstamos no cambiaron. */
static void conservar_fisica(const Fisica *a,const Fisica *b)
{
#define CAMPO(c) EXIGIR(a->flujo.c==b->flujo.c)
    CAMPO(operaciones);CAMPO(cantidad_operaciones);CAMPO(funciones);CAMPO(cantidad_funciones);
    CAMPO(simbolos);CAMPO(cantidad_simbolos);CAMPO(transferencias);CAMPO(cantidad_transferencias);
    CAMPO(fronteras);CAMPO(cantidad_fronteras);CAMPO(regiones);CAMPO(cantidad_regiones);
    CAMPO(generacion);CAMPO(generacion_referencias);CAMPO(pendientes);CAMPO(fronteras_acreditadas);
#undef CAMPO
#define CAMPO(c) EXIGIR(a->entrada.fisica.c==b->entrada.fisica.c)
    CAMPO(flujo);CAMPO(cfg);CAMPO(metadatos);CAMPO(efectos_adicionales);CAMPO(cantidad_metadatos);
    CAMPO(generacion);CAMPO(pendientes);CAMPO(efectos_acreditados);CAMPO(abi_acreditado);
#undef CAMPO
    EXIGIR(a->cfg==b->cfg && a->dag==b->dag && a->entrada.dag==b->entrada.dag &&
        a->entrada.textos==b->entrada.textos && a->entrada.cantidad_textos==b->entrada.cantidad_textos);
    EXIGIR(a->entrada.garantias.pendientes==b->entrada.garantias.pendientes &&
        a->entrada.garantias.completitud_declarada==b->entrada.garantias.completitud_declarada &&
        a->entrada.garantias.reubicaciones_resueltas==b->entrada.garantias.reubicaciones_resueltas);
    for(size_t i=0;i<MAX;++i){
        EXIGIR(a->op[i].id==b->op[i].id && a->op[i].palabra==b->op[i].palabra && a->op[i].slot==b->op[i].slot &&
            a->op[i].salto==b->op[i].salto && a->op[i].control==b->op[i].control);
        conservar_meta(&a->meta[i],&b->meta[i]);EXIGIR(a->efectos[i]==b->efectos[i] && a->textos[i]==b->textos[i]);
        EXIGIR(!memcmp(a->texto[i],b->texto[i],sizeof a->texto[i]));
        EXIGIR(a->simbolos[i].id==b->simbolos[i].id && a->simbolos[i].funcion==b->simbolos[i].funcion && a->simbolos[i].posicion==b->simbolos[i].posicion);
        EXIGIR(a->transferencias[i].operacion==b->transferencias[i].operacion && a->transferencias[i].tipo==b->transferencias[i].tipo &&
            a->transferencias[i].tomado==b->transferencias[i].tomado && a->transferencias[i].continuacion==b->transferencias[i].continuacion &&
            a->transferencias[i].posibles==b->transferencias[i].posibles && a->transferencias[i].cantidad_posibles==b->transferencias[i].cantidad_posibles &&
            a->transferencias[i].completos==b->transferencias[i].completos);
    }
    for(size_t i=0;i<2;++i)EXIGIR(a->fn[i].id==b->fn[i].id && a->fn[i].inicio==b->fn[i].inicio && a->fn[i].fin==b->fn[i].fin);
    EXIGIR(!memcmp(a->fronteras,b->fronteras,sizeof a->fronteras));EXIGIR(!memcmp(a->posibles,b->posibles,sizeof a->posibles));
}
/* Captura tablas pequeñas y verifica campos después del éxito/rechazo. Los
 * buffers char y escalares sí estaban completamente inicializados. No reserva
 * durante el API evaluado ni interpreta padding como identidad arquitectónica. */
static VliwProyeccion *proyectar(Fisica *f,VliwProyeccionEstado esperado)
{
    Fisica antes=*f;VliwCfg cfg=*f->cfg;VliwDag dag=*f->dag;
    VliwBloque bloques[MAX];size_t mapa[MAX],su[MAX][MAX];VliwDestino dest[MAX];VliwDependencia ar[256];
    EXIGIR(cfg.cantidad_bloques<=MAX && dag.cantidad_dependencias<=256);
    for(size_t b=0;b<cfg.cantidad_bloques;++b){bloques[b]=cfg.bloques[b];
        if(bloques[b].cantidad_sucesores)memcpy(su[b],bloques[b].sucesores,bloques[b].cantidad_sucesores*sizeof(size_t));}
    if(cfg.cantidad_operaciones)memcpy(mapa,cfg.bloque_por_operacion,cfg.cantidad_operaciones*sizeof(size_t));
    for(size_t t=0;t<cfg.cantidad_destinos;++t)dest[t]=cfg.destinos[t];
    for(size_t a=0;a<dag.cantidad_dependencias;++a)ar[a]=dag.dependencias[a];
    VliwProyeccion *p=NULL;VliwProyeccionDiagnostico d;
    VliwProyeccionEstado estado=vliw_proyeccion_construir(&f->entrada,&p,&d);
    if(esperado!=(VliwProyeccionEstado)-1 && estado!=esperado)fprintf(stderr,"proyección: %d != %d, %s\n",estado,esperado,d.mensaje);
    EXIGIR(esperado==(VliwProyeccionEstado)-1?estado!=VLIW_PROY_OK:estado==esperado);EXIGIR(d.estado==estado && *d.mensaje);EXIGIR((p!=NULL)==(estado==VLIW_PROY_OK));
    conservar_fisica(&antes,f);
    EXIGIR(cfg.generacion==f->cfg->generacion && cfg.cantidad_operaciones==f->cfg->cantidad_operaciones &&
        cfg.cantidad_bloques==f->cfg->cantidad_bloques && cfg.bloques==f->cfg->bloques &&
        cfg.bloque_por_operacion==f->cfg->bloque_por_operacion && cfg.destinos==f->cfg->destinos &&
        cfg.cantidad_destinos==f->cfg->cantidad_destinos && cfg.destinos_dinamicos_acreditados==f->cfg->destinos_dinamicos_acreditados);
    for(size_t b=0;b<cfg.cantidad_bloques;++b){const VliwBloque *v=&cfg.bloques[b];
        EXIGIR(bloques[b].inicio==v->inicio && bloques[b].fin==v->fin && bloques[b].sucesores==v->sucesores && bloques[b].cantidad_sucesores==v->cantidad_sucesores);
        for(size_t k=0;k<v->cantidad_sucesores;++k)EXIGIR(su[b][k]==v->sucesores[k]);}
    for(size_t i=0;i<cfg.cantidad_operaciones;++i)EXIGIR(mapa[i]==cfg.bloque_por_operacion[i]);
    for(size_t t=0;t<cfg.cantidad_destinos;++t)EXIGIR(dest[t].instruccion==cfg.destinos[t].instruccion && dest[t].tipo==cfg.destinos[t].tipo && dest[t].tomado==cfg.destinos[t].tomado && dest[t].caida==cfg.destinos[t].caida);
    EXIGIR(dag.generacion==f->dag->generacion && dag.cantidad_operaciones==f->dag->cantidad_operaciones &&
        dag.cantidad_dependencias==f->dag->cantidad_dependencias && dag.camino_critico==f->dag->camino_critico && dag.dependencias==f->dag->dependencias);
    for(size_t a=0;a<dag.cantidad_dependencias;++a)EXIGIR(ar[a].antes==dag.dependencias[a].antes && ar[a].despues==dag.dependencias[a].despues && ar[a].tipo==dag.dependencias[a].tipo && ar[a].distancia==dag.dependencias[a].distancia);
    ++escenarios;return p;
}
/* Libera solo los objetos del fixture; las tablas internas son almacenamiento
 * automático. La proyección debe sobrevivir a esta destrucción de préstamos. */
static void liberar_fisica(Fisica *f)
{
    vliw_dag_liberar(&f->dag);vliw_cfg_liberar(&f->cfg);
}
/* Contrasta relaciones exactas con el DAG/CFG recibidos y comprueba que los
 * punteros publicados ya no apunten a esos préstamos, incluidos textos y sucesores. */
static void comprobar_copia(const Fisica *f,const VliwProyeccion *p)
{
    EXIGIR(p->generacion==71 && p->entrada.cantidad_instrucciones==f->flujo.cantidad_operaciones);
    EXIGIR(p->entrada.cantidad_dependencias==f->dag->cantidad_dependencias && p->destinos.cantidad==f->cfg->cantidad_destinos);
    for(size_t i=0;i<p->entrada.cantidad_instrucciones;++i){const VliwInstruccion *a=&p->entrada.instrucciones[i];
        EXIGIR(a->id==f->op[i].id && a->slot==f->op[i].slot && a->control==f->op[i].control && a->bloque==f->cfg->bloque_por_operacion[i]);
        EXIGIR(a->lee==f->meta[i].lee && a->escribe==f->meta[i].escribe && a->efectos==(f->meta[i].efectos_minimos|f->efectos[i]));
        EXIGIR(a->texto!=f->textos[i] && !strcmp(a->texto,f->textos[i]));}
    for(size_t k=0;k<p->entrada.cantidad_dependencias;++k){VliwDependencia a=p->entrada.dependencias[k],b=f->dag->dependencias[k];
        EXIGIR(a.antes==b.antes && a.despues==b.despues && a.tipo==b.tipo && a.distancia==b.distancia);}
    for(size_t b=0;b<p->entrada.cantidad_bloques;++b){VliwBloque a=p->entrada.bloques[b],v=f->cfg->bloques[b];
        EXIGIR(a.inicio==v.inicio && a.fin==v.fin && a.cantidad_sucesores==v.cantidad_sucesores);
        if(a.cantidad_sucesores)EXIGIR(a.sucesores!=v.sucesores);
        for(size_t k=0;k<a.cantidad_sucesores;++k)EXIGIR(a.sucesores[k]==v.sucesores[k]);}
    for(size_t k=0;k<p->destinos.cantidad;++k){VliwDestino a=p->destinos.items[k],b=f->cfg->destinos[k];
        EXIGIR(a.instruccion==b.instruccion && a.tipo==b.tipo && a.tomado==b.tomado && a.caida==b.caida);}
}
/* Vacío, secuencias y las seis CRIPTO/LSU utilizan mínimos S2 reales. No se
 * atribuyen requisitos AUTH/INIT satisfechos en ejecución: esta fase es estática. */
static void positivas_proyeccion(void)
{
    for(size_t n=0;n<=8;++n){Fisica f;iniciar_fisica(&f,n);preparar_fisica(&f);VliwProyeccion *p=proyectar(&f,VLIW_PROY_OK);
        comprobar_copia(&f,p);liberar_fisica(&f);memset(&f,0,sizeof f);
        EXIGIR(vliw_integracion_validar(&p->entrada,&p->destinos,NULL)==VLIW_INT_OK);
        vliw_proyeccion_liberar(&p);EXIGIR(!p);vliw_proyeccion_liberar(&p);}
    const char *textos[]={"cargai x4,x2,0","guardap x2,x4,0","guardab x2,x4,0","cargabai x4,x2,0",
        "fsl x6,x2,0,1","fsli x8,x4,3,2","ell 2,0,x4,x10","vcr 0","camcon 16,7","setpwd x25","sumai x0,x0,7"};
    for(size_t k=0;k<sizeof textos/sizeof textos[0];++k){Fisica f;iniciar_fisica(&f,1);operacion(&f,0,k<4?1:k<10?3:0,textos[k]);
        preparar_fisica(&f);VliwProyeccion *p=proyectar(&f,VLIW_PROY_OK);comprobar_copia(&f,p);vliw_proyeccion_liberar(&p);liberar_fisica(&f);}
    vliw_proyeccion_liberar(NULL);
}
/* Pares con riesgos manuales distinguen RAW3, WAR1 y WAW1. La copia debe
 * conservar las aristas generadas, incluso la cobertura transitiva entre bloques. */
static void riesgos_proyeccion(void)
{
    const char *a[]={"sumai x4,x0,7","sumai x5,x4,1","sumai x4,x0,7","cargai x4,x2,0","fsl x6,x2,0,1"};
    const char *b[]={"sumai x5,x4,1","sumai x4,x0,7","sumai x4,x0,9","guardap x2,x4,0","sumai x4,x7,1"};
    const VliwTipoDependencia tipos[]={VLIW_DEP_RAW,VLIW_DEP_WAR,VLIW_DEP_WAW,VLIW_DEP_RAW,VLIW_DEP_BARRERA};
    for(size_t k=0;k<5;++k){Fisica f;iniciar_fisica(&f,2);operacion(&f,0,k==3?1:k==4?3:0,a[k]);operacion(&f,1,k==3?1:0,b[k]);preparar_fisica(&f);
        EXIGIR(f.dag->cantidad_dependencias==1 && f.dag->dependencias[0].tipo==tipos[k]);
        EXIGIR(f.dag->dependencias[0].distancia==(k==0 || k>=3?3u:1u));
        VliwProyeccion *p=proyectar(&f,VLIW_PROY_OK);comprobar_copia(&f,p);vliw_proyeccion_liberar(&p);liberar_fisica(&f);}
    Fisica f;iniciar_fisica(&f,3);operacion(&f,1,0,"sumai x4,x0,8");operacion(&f,2,0,"sumai x6,x4,1");f.fronteras[0]=2;f.flujo.cantidad_fronteras=1;
    preparar_fisica(&f);EXIGIR(f.cfg->cantidad_bloques==2 && f.dag->camino_critico==4);
    VliwProyeccion *p=proyectar(&f,VLIW_PROY_OK);comprobar_copia(&f,p);vliw_proyeccion_liberar(&p);liberar_fisica(&f);
}
/* Los destinos provienen de símbolos explícitos: el backedge permanece en
 * CFG, nunca se copia como arista retrospectiva DAG. Call/ret no infiere ABI por x1. */
static void control_proyeccion(void)
{
    for(unsigned cond=0;cond<2;++cond){Fisica f;iniciar_fisica(&f,3);salto_fisico(&f,1,0,cond!=0);preparar_fisica(&f);
        VliwProyeccion *p=proyectar(&f,VLIW_PROY_OK);comprobar_copia(&f,p);vliw_proyeccion_liberar(&p);liberar_fisica(&f);}
    Fisica f;llamada_fisica(&f);preparar_fisica(&f);VliwProyeccion *p=proyectar(&f,VLIW_PROY_OK);
    comprobar_copia(&f,p);vliw_proyeccion_liberar(&p);liberar_fisica(&f);
    llamada_fisica(&f);preparar_fisica(&f);f.entrada.fisica.abi_acreditado=false;p=proyectar(&f,VLIW_PROY_DAG);EXIGIR(!p);liberar_fisica(&f);
    llamada_fisica(&f);preparar_fisica(&f);f.entrada.garantias.reubicaciones_resueltas=false;p=proyectar(&f,VLIW_PROY_REUBICACION);EXIGIR(!p);liberar_fisica(&f);
}
/* Mutamos cada garantía/texto/metadata/dimensión después de construir los
 * análisis. Cada rechazo debe conservar el dato erróneo, no repararlo o publicar. */
static void negativas_proyeccion(void)
{
    for(unsigned caso=0;caso<32;++caso){Fisica f;iniciar_fisica(&f,2);operacion(&f,1,0,"sumai x5,x4,1");preparar_fisica(&f);
        VliwProyeccionEstado esperado=VLIW_PROY_DAG;
        switch(caso){
        case 0:f.entrada.garantias.completitud_declarada=false;esperado=VLIW_PROY_INCOMPLETO;break;
        case 1:f.entrada.garantias.pendientes=1;esperado=VLIW_PROY_INCOMPLETO;break;
        case 2:f.entrada.garantias.reubicaciones_resueltas=false;esperado=VLIW_PROY_REUBICACION;break;
        case 3:f.entrada.fisica.generacion++;esperado=VLIW_PROY_INCOMPLETO;break;
        case 4:f.entrada.cantidad_textos=1;esperado=VLIW_PROY_ARGUMENTO;break;
        case 5:f.textos[0]=NULL;esperado=VLIW_PROY_TEXTO;break;
        case 6:f.textos[0]="sumai x4,x0,99";esperado=VLIW_PROY_TEXTO;break;
        case 7:f.textos[0]="restai x4,x0,7";esperado=VLIW_PROY_TEXTO;break;
        case 8:f.textos[0]="nop";esperado=VLIW_PROY_INTEGRACION;break;
        case 9:f.textos[0]="sumai x4,x0,7 | nop";esperado=VLIW_PROY_INTEGRACION;break;
        case 10:f.meta[0].lee=UINT64_C(1)<<63;break;
        case 11:f.meta[0].slot=VLIW_SLOT_LSU;break;
        case 12:f.meta[0].efectos_minimos=VLIW_EF_LEE_MEMORIA;break;
        case 13:f.efectos[0]=VLIW_EF_BOVEDA;break;
        case 14:f.dag->dependencias[0].distancia=2;break;
        case 15:f.dag->dependencias[0].antes=1;break;
        case 16:f.cfg->bloque_por_operacion[0]=9;break;
        case 17:f.entrada.fisica.efectos_acreditados=false;break;
        case 18:f.cfg->generacion++;break;
        case 19:f.dag->generacion++;esperado=VLIW_PROY_INCOMPLETO;break;
        case 20:f.dag->cantidad_operaciones--;break;
        case 21:f.dag->camino_critico++;break;
        case 22:f.meta[0].operacion=SIM_OP_RESTA;break;
        case 23:f.entrada.cantidad_textos=SIZE_MAX;esperado=VLIW_PROY_ARGUMENTO;break;
        case 24:f.entrada.fisica.cantidad_metadatos=1;break;
        case 25:f.entrada.fisica.metadatos=NULL;break;
        case 26:f.entrada.dag=NULL;esperado=VLIW_PROY_ARGUMENTO;break;
        case 27:f.entrada.fisica.cfg=NULL;esperado=VLIW_PROY_ARGUMENTO;break;
        case 28:f.entrada.textos=NULL;esperado=VLIW_PROY_ARGUMENTO;break;
        case 29:f.dag->dependencias[0].despues=99;break;
        case 30:f.dag->dependencias[0].tipo=VLIW_DEP_MEMORIA;break;
        default:f.cfg->destinos_dinamicos_acreditados=false;break;}
        VliwProyeccion *p=proyectar(&f,esperado);EXIGIR(!p);liberar_fisica(&f);
    }
    Fisica f;iniciar_fisica(&f,0);preparar_fisica(&f);VliwProyeccion *p=NULL;
    EXIGIR(vliw_proyeccion_construir(NULL,&p,NULL)==VLIW_PROY_ARGUMENTO);++escenarios;
    EXIGIR(vliw_proyeccion_construir(&f.entrada,NULL,NULL)==VLIW_PROY_ARGUMENTO);++escenarios;
    VliwProyeccion ocupado={0};p=&ocupado;EXIGIR(vliw_proyeccion_construir(&f.entrada,&p,NULL)==VLIW_PROY_ARGUMENTO && p==&ocupado);++escenarios;
    p=NULL;f.flujo.cantidad_operaciones=SIZE_MAX;EXIGIR(vliw_proyeccion_construir(&f.entrada,&p,NULL)==VLIW_PROY_CAPACIDAD && !p);++escenarios;
    f.flujo.cantidad_operaciones=0;liberar_fisica(&f);
}
/* Un fallo en cualquiera de las reservas observadas recorre también errores
 * de CB/I1-A1. No exigimos que un diagnóstico ajeno desconocido se convierta
 * artificialmente en MEMORIA: sí exigimos rechazo, salida NULL y recuperación. */
static void recursos_proyeccion(void)
{
#ifdef FIS_FALLOS
    Fisica f;llamada_fisica(&f);preparar_fisica(&f);VliwProyeccion *p=NULL;
    llamadas=0;fallar=SIZE_MAX;activo=true;VliwProyeccionEstado estado=vliw_proyeccion_construir(&f.entrada,&p,NULL);activo=false;
    EXIGIR(estado==VLIW_PROY_OK && p);size_t total=llamadas;EXIGIR(total>10);vliw_proyeccion_liberar(&p);
    for(size_t k=1;k<=total;++k){llamadas=0;fallar=k;activo=true;
        p=proyectar(&f,(VliwProyeccionEstado)-1);activo=false;
        EXIGIR(!p);
        p=proyectar(&f,VLIW_PROY_OK);vliw_proyeccion_liberar(&p);
    }
    printf("RESERVAS proyeccion=%zu; fallos=%zu\n",total,total);liberar_fisica(&f);
#else
    EXIGIR(false);
#endif
}
/* Este dispatcher permite reutilizar fixtures dentro de la suite de cadena;
 * cada suite ejecuta únicamente sus grupos y publica los conteos observados. */
int proyeccion_grupo(const char *grupo)
{
    if(!strcmp(grupo,"positivas"))positivas_proyeccion();else if(!strcmp(grupo,"riesgos"))riesgos_proyeccion();
    else if(!strcmp(grupo,"control"))control_proyeccion();else if(!strcmp(grupo,"negativas"))negativas_proyeccion();
    else if(!strcmp(grupo,"recursos"))recursos_proyeccion();else return 2;
    printf("PASS %s: escenarios=%zu; comprobaciones=%zu\n",grupo,escenarios,comprobaciones);return 0;
}
#ifndef VLIW_PROYECCION_SIN_MAIN
/* Los nombres desconocidos fallan: no se informa éxito sin ejecutar un grupo. */
int main(int argc,char **argv){return argc==2?proyeccion_grupo(argv[1]):2;}
#endif
