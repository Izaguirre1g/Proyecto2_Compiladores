/* Construye un DAG de emisión con cobertura ponderada de riesgos físicos.
 * Usa I1-A2a/S2 y el CFG existente, sin duplicar decodificación ni producir BAL.
 * La reducción conserva distancias globales; ABI y autenticidad siguen externas. */
#include "vliw_dag_fisico.h"
#include <stdlib.h>
#define VAC VLIW_VACIO
#define MEM (VLIW_EF_LEE_MEMORIA|VLIW_EF_ESCRIBE_MEMORIA)
#define GLOBAL (VLIW_EF_BOVEDA|VLIW_EF_ESTADO|VLIW_EF_EXCEPCION|VLIW_EF_DESCONOCIDOS|VLIW_EF_BARRERA)

/* Publica diagnóstico opcional por valor; no reserva ni retiene entradas. */
static VliwDagEstado informar(VliwDagDiagnostico *d,VliwDagEstado e,size_t i,const char *m)
{
    if(d) *d=(VliwDagDiagnostico){e,i,m};
    return e;
}
#define ERROR(e,i,m) informar(d,e,i,m)

/* Destruye también resultados privados incompletos inicializados con calloc.
 * No libera CFG, metadatos ni arreglos prestados por el llamador. */
void vliw_dag_liberar(VliwDag **dag)
{
    if(!dag || !*dag) return;
    free((*dag)->dependencias);free(*dag);*dag=NULL;
}

/* Contrasta el resultado recibido con un CFG reconstruido por su constructor
 * real. Sucesores son conjuntos; destinos conservan el orden de transferencias.
 * Cantidades se comparan antes de recorrer arreglos accesibles del cliente. */
static bool mismo_cfg(const VliwCfg *a,const VliwCfg *b)
{
    size_t n=b->cantidad_operaciones,nb=b->cantidad_bloques,nt=b->cantidad_destinos;
    if(a->generacion!=b->generacion || a->cantidad_operaciones!=n ||
       a->cantidad_bloques!=nb || a->cantidad_destinos!=nt ||
       a->destinos_dinamicos_acreditados!=b->destinos_dinamicos_acreditados ||
       (n && !a->bloque_por_operacion) || (nb && !a->bloques) || (nt && !a->destinos)) return false;
    for(size_t i=0;i<n;++i) if(a->bloque_por_operacion[i]!=b->bloque_por_operacion[i]) return false;
    for(size_t k=0;k<nb;++k) {
        const VliwBloque *u=&a->bloques[k],*v=&b->bloques[k];
        if(u->inicio!=v->inicio || u->fin!=v->fin || u->cantidad_sucesores!=v->cantidad_sucesores ||
           (u->cantidad_sucesores && !u->sucesores)) return false;
        for(size_t s=0;s<u->cantidad_sucesores;++s) {
            size_t t=0;
            for(;t<v->cantidad_sucesores;++t) if(u->sucesores[s]==v->sucesores[t]) break;
            if(t==v->cantidad_sucesores) return false;
            for(size_t p=0;p<s;++p) if(u->sucesores[p]==u->sucesores[s]) return false;
        }
    }
    for(size_t t=0;t<nt;++t) {
        const VliwDestino *u=&a->destinos[t],*v=&b->destinos[t];
        if(u->instruccion!=v->instruccion || u->tipo!=v->tipo || u->tomado!=v->tomado || u->caida!=v->caida) return false;
    }
    return true;
}

/* Compara campos significativos, nunca padding. I1-A2a es la referencia local:
 * rechaza máscaras virtuales, efectos omitidos o un opcode desactualizado.
 * Los bits pendientes parciales permanecen intactos y siguen siendo parciales. */
static bool mismos_metadatos(const VliwMetadatosParciales *a,const VliwMetadatosParciales *b)
{
    return a->operacion==b->operacion && a->familia==b->familia && a->slot==b->slot &&
        a->lee==b->lee && a->escribe==b->escribe && a->efectos_minimos==b->efectos_minimos &&
        a->salto==b->salto && a->requiere_auth==b->requiere_auth &&
        a->requiere_init==b->requiere_init && a->pendientes==b->pendientes;
}

/* Valida efectos combinados compatibles con S7/I1-A1. Una llamada exige
 * DESCONOCIDOS, sin inventar accesos LSU ni clobbers en sus máscaras GPR. */
static bool efectos_validos(const VliwCfgOperacion *o,unsigned ef)
{
    return !(ef & ~(MEM|GLOBAL)) &&
        (!(ef & MEM) || o->slot==VLIW_SLOT_LSU || o->slot==VLIW_SLOT_CRIPTO) &&
        (!(ef & (VLIW_EF_BOVEDA|VLIW_EF_ESTADO)) || o->slot==VLIW_SLOT_CRIPTO) &&
        (o->control!=VLIW_CONTROL_LLAMADA || (ef & VLIW_EF_DESCONOCIDOS));
}

/* Reconoce barreras reales según S7: CRIPTO, efectos globales, call/ret.
 * La clasificación ABI proviene del flujo acreditado, nunca de x1. */
static bool barrera(const VliwDagEntrada *e,size_t i)
{
    const VliwCfgOperacion *o=&e->flujo->operaciones[i];
    return o->slot==VLIW_SLOT_CRIPTO ||
        ((e->metadatos[i].efectos_minimos|e->efectos_adicionales[i]) & GLOBAL) ||
        o->control==VLIW_CONTROL_LLAMADA || o->control==VLIW_CONTROL_RETORNO;
}

/* Devuelve la restricción dominante y un tipo físicamente justificado. RAW y
 * barreras exigen tres bundles por N+3; los demás riesgos conservan orden1.
 * La prioridad solo elige representación cuando coinciden causas reales. */
static size_t riesgo(const VliwDagEntrada *e,size_t i,size_t j,VliwTipoDependencia *tipo)
{
    const VliwMetadatosParciales *a=&e->metadatos[i],*b=&e->metadatos[j];
    uint64_t ar=a->lee & ~UINT64_C(1),aw=a->escribe & ~UINT64_C(1);
    uint64_t br=b->lee & ~UINT64_C(1),bw=b->escribe & ~UINT64_C(1);
    if(barrera(e,i) || barrera(e,j)) {*tipo=VLIW_DEP_BARRERA;return 3;}
    if(aw & br) {*tipo=VLIW_DEP_RAW;return VLIW_RAW_DISTANCIA;}
    if(ar & bw) {*tipo=VLIW_DEP_WAR;return 1;}
    if(aw & bw) {*tipo=VLIW_DEP_WAW;return 1;}
    // Sin análisis de alias acreditado se conserva también load/load, aunque
    // las direcciones o las rutas CFG parezcan distintas o excluyentes.
    if(((a->efectos_minimos|e->efectos_adicionales[i]) & MEM) &&
       ((b->efectos_minimos|e->efectos_adicionales[j]) & MEM)) {*tipo=VLIW_DEP_MEMORIA;return 1;}
    if(e->flujo->operaciones[i].control!=VLIW_CONTROL_NINGUNO ||
       e->flujo->operaciones[j].control!=VLIW_CONTROL_NINGUNO) {*tipo=VLIW_DEP_CONTROL;return 1;}
    return 0;
}

/* Añade una arista real a un vector propio de crecimiento geométrico acotado.
 * Un realloc fallido conserva el puntero anterior para la limpieza común.
 * El límite explícito permite rechazar densidad no verificada sin desbordar. */
static VliwDagEstado agregar(VliwDag *g,size_t *cap,VliwDependencia ar,VliwDagDiagnostico *d)
{
    size_t m=g->cantidad_dependencias;
    if(m==VLIW_DAG_MAX_ARISTAS) return ERROR(VLIW_DAG_CAPACIDAD,ar.despues,"demasiadas aristas para este constructor");
    if(m==*cap) {
        size_t nueva=*cap ? *cap*2 : 32;
        if(nueva>VLIW_DAG_MAX_ARISTAS) nueva=VLIW_DAG_MAX_ARISTAS;
        if(nueva>SIZE_MAX/sizeof(VliwDependencia)) return ERROR(VLIW_DAG_CAPACIDAD,VAC,"overflow de aristas");
        VliwDependencia *p=realloc(g->dependencias,nueva*sizeof *p);
        if(!p) return ERROR(VLIW_DAG_MEMORIA,ar.despues,"sin memoria para aristas");
        g->dependencias=p;*cap=nueva;
    }
    g->dependencias[m]=ar;g->cantidad_dependencias=m+1;
    return VLIW_DAG_OK;
}

/* Construye por destinos crecientes y orígenes decrecientes. dist[k*n+j]
 * conserva el mayor peso conocido k→j; cero representa ausencia de camino.
 * Cada arista nueva i→j propaga dist[k,i]+peso para todos los ancestros k.
 * Al examinar i, las rutas a j que pasan por vértices posteriores ya existen;
 * solo añadimos una arista cuando ninguna ruta alcanza la separación exigida.
 * No copiamos backedges CFG: antes<después hace acíclico el DAG por construcción. */
static VliwDagEstado construir(const VliwDagEntrada *e,VliwDag *g,uint16_t *dist,VliwDagDiagnostico *d)
{
    size_t n=g->cantidad_operaciones,cap=0;
    for(size_t j=1;j<n;++j) for(size_t fin=j;fin>0;) {
        size_t i=--fin;VliwTipoDependencia tipo=VLIW_DEP_RAW;
        size_t peso=riesgo(e,i,j,&tipo);
        if(!peso || dist[i*n+j]>=peso) continue;
        VliwDagEstado estado=agregar(g,&cap,(VliwDependencia){i,j,tipo,peso},d);
        if(estado) return estado;
        dist[i*n+j]=(uint16_t)peso;
        if(peso>g->camino_critico) g->camino_critico=peso;
        for(size_t k=0;k<i;++k) if(dist[k*n+i]) {
            size_t valor=(size_t)dist[k*n+i]+peso;
            // Incluso un camino entre extremos sin riesgo directo consume
            // separación real. Rechazamos antes de truncar el entero de16 bits.
            if(valor>=VLIW_MAX_BUNDLES) return ERROR(VLIW_DAG_CAPACIDAD,j,"camino crítico no cabe en IMEM ordinaria");
            if(valor>dist[k*n+j]) dist[k*n+j]=(uint16_t)valor;
            if(valor>g->camino_critico) g->camino_critico=valor;
        }
    }
    return VLIW_DAG_OK;
}

/* Coordina validación por capas, recursos privados y publicación única.
 * Revalidar CFG reutiliza sus reglas sin sustituirlas por una aproximación.
 * Los créditos externos no se convierten en certificación BAL ni en entrada S7. */
VliwDagEstado vliw_dag_construir(const VliwDagEntrada *e,VliwDag **salida,VliwDagDiagnostico *d)
{
    if(!e || !salida || *salida || !e->flujo || !e->cfg)
        return ERROR(VLIW_DAG_ARGUMENTO,VAC,"entrada, flujo, CFG y salida inicial NULL requeridos");
    size_t n=e->flujo->cantidad_operaciones;
    if(n>VLIW_MAX_BUNDLES*(size_t)VLIW_SLOTS || (n && n>SIZE_MAX/n/sizeof(uint16_t)))
        return ERROR(VLIW_DAG_CAPACIDAD,VAC,"dimensión fuera de capacidad");
    if(e->cantidad_metadatos!=n || (n && (!e->metadatos || !e->efectos_adicionales)))
        return ERROR(VLIW_DAG_ARGUMENTO,VAC,"metadatos y efectos requieren una entrada por operación");
    if(e->generacion!=e->flujo->generacion || e->generacion!=e->cfg->generacion ||
       e->pendientes || (n && !e->efectos_acreditados) || !e->cfg->destinos_dinamicos_acreditados)
        return ERROR(VLIW_DAG_INCOMPLETO,VAC,"generación, efectos o cobertura dinámica sin acreditar");
    VliwCfg *control=NULL;VliwCfgDiagnostico cd;
    VliwCfgEstado ce=vliw_cfg_construir(e->flujo,&control,&cd);
    if(ce!=VLIW_CFG_OK) return ERROR(ce==VLIW_CFG_MEMORIA ? VLIW_DAG_MEMORIA :
        ce==VLIW_CFG_CAPACIDAD ? VLIW_DAG_CAPACIDAD : VLIW_DAG_CFG,VAC,cd.mensaje);
    bool coincide=mismo_cfg(e->cfg,control);vliw_cfg_liberar(&control);
    if(!coincide) return ERROR(VLIW_DAG_CFG,VAC,"CFG incompatible con el flujo actual");
    for(size_t i=0;i<n;++i) {
        const VliwCfgOperacion *o=&e->flujo->operaciones[i];VliwMetadatosParciales real;
        if(vliw_metadatos_extraer(o->palabra,(unsigned)o->slot,&real,NULL)!=SIM_DECODE_OK ||
           !mismos_metadatos(&e->metadatos[i],&real))
            return ERROR(VLIW_DAG_METADATOS,i,"metadatos distintos de la operación física S2");
        if(!efectos_validos(o,real.efectos_minimos|e->efectos_adicionales[i]))
            return ERROR(VLIW_DAG_METADATOS,i,"efectos adicionales incompatibles o llamada sin DESCONOCIDOS");
        if((o->control==VLIW_CONTROL_LLAMADA || o->control==VLIW_CONTROL_RETORNO) && !e->abi_acreditado)
            return ERROR(VLIW_DAG_INCOMPLETO,i,"obligaciones ABI de llamada/retorno pendientes");
    }
    VliwDag *g=calloc(1,sizeof *g);
    if(!g) return ERROR(VLIW_DAG_MEMORIA,VAC,"sin resultado DAG");
    g->generacion=e->generacion;g->cantidad_operaciones=n;
    uint16_t *dist=n ? calloc(n*n,sizeof *dist) : NULL;
    VliwDagEstado estado=VLIW_DAG_OK;
    if(n && !dist) estado=ERROR(VLIW_DAG_MEMORIA,VAC,"sin matriz de cobertura ponderada");
    else estado=construir(e,g,dist,d);
    free(dist);
    if(estado) {vliw_dag_liberar(&g);return estado;}
    *salida=g;
    return ERROR(VLIW_DAG_OK,VAC,"DAG físico construido; semántica y reubicaciones externas");
}
