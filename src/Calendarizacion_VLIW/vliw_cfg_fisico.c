/* Particiona operaciones ISA explícitas y construye sus sucesores lógicos.
 * Valida palabras mediante S2 y entrega arreglos propios compatibles S7/S8-A.
 * No deduce ABI, no interpreta PCs, no crea dependencias ni ejecuta código. */
#include "vliw_cfg_fisico.h"
#include <stdlib.h>
#define VAC VLIW_VACIO

/* Publica un diagnóstico local, sin adquirir recursos; índice se interpreta
 * con el nombre de tabla incluido en el mensaje. Admite diagnóstico ausente. */
static VliwCfgEstado informar(VliwCfgDiagnostico *d,VliwCfgEstado e,size_t i,const char *m)
{
    if(d) *d=(VliwCfgDiagnostico){e,i,m};
    return e;
}
#define ERROR(e,i,m) informar(d,e,i,m)
/* Busca por identidad simbólica, nunca por PC o nombre textual ambiguo.
 * La tabla ya fue validada; devuelve VAC si la referencia está sin definir. */
static size_t simbolo(const VliwCfgEntrada *e,size_t id)
{
    for(size_t j=0;j<e->cantidad_simbolos;++j)
        if(e->simbolos[j].id==id) return j;
    return VAC;
}
/* Obtiene la función que contiene una posición de la partición ya validada;
 * no publica referencias a sus elementos ni infiere funciones desde saltos. */
static size_t funcion(const VliwCfgEntrada *e,size_t pos)
{
    for(size_t f=0;f<e->cantidad_funciones;++f)
        if(pos>=e->funciones[f].inicio && pos<e->funciones[f].fin) return f;
    return VAC;
}
/* Deduplica sucesores como conjunto, incluso si etiquetas distintas apuntan
 * a una entrada. El auxiliar tiene capacidad para todos los bloques. */
static void agregar(size_t *v,size_t *n,size_t b)
{
    for(size_t j=0;j<*n;++j) if(v[j]==b) return;
    v[(*n)++]=b;
}
/* Destruye también una construcción parcial inicializada con calloc: solo
 * los bloques cuyo sucesor se reservó poseen memoria. Nunca libera entradas. */
void vliw_cfg_liberar(VliwCfg **c)
{
    if(!c || !*c) return;
    for(size_t b=0;(*c)->bloques && b<(*c)->cantidad_bloques;++b) free((void *)(*c)->bloques[b].sucesores);
    free((*c)->bloques); free((*c)->bloque_por_operacion); free((*c)->destinos);
    free(*c); *c=NULL;
}
/* Verifica dimensiones antes de recorrer tablas o multiplicar reservas.
 * La cota física es necesaria, no demuestra que el futuro plan quepa. */
static VliwCfgEstado dimensiones(const VliwCfgEntrada *e,VliwCfgDiagnostico *d)
{
    size_t n=e->cantidad_operaciones;
    if(n>VLIW_MAX_BUNDLES*(size_t)VLIW_SLOTS ||
       e->cantidad_simbolos>SIZE_MAX/sizeof(VliwCfgSimbolo))
        return ERROR(VLIW_CFG_CAPACIDAD,VAC,"cantidades fuera de capacidad");
    if(e->cantidad_funciones>n || e->cantidad_transferencias>n ||
       e->cantidad_fronteras>n || e->cantidad_regiones>n)
        return ERROR(VLIW_CFG_ESTRUCTURA,VAC,"cantidades incompatibles con operaciones");
    if((n && !e->operaciones) || (e->cantidad_funciones && !e->funciones) ||
       (e->cantidad_simbolos && !e->simbolos) ||
       (e->cantidad_transferencias && !e->transferencias) ||
       (e->cantidad_fronteras && !e->fronteras) || (e->cantidad_regiones && !e->regiones))
        return ERROR(VLIW_CFG_ARGUMENTO,VAC,"arreglo requerido ausente");
    if((!n && e->cantidad_simbolos) || (n && !e->cantidad_funciones))
        return ERROR(VLIW_CFG_ESTRUCTURA,VAC,"partición de funciones requerida");
    if(e->generacion!=e->generacion_referencias || e->pendientes || !e->fronteras_acreditadas)
        return ERROR(VLIW_CFG_INCOMPLETO,VAC,"generación o fronteras sin acreditar");
    return VLIW_CFG_OK;
}
/* Comprueba funciones, símbolos, palabras y referencias antes de publicar
 * bloques. Los auxiliares marcan líderes y referencia por posición original. */
static VliwCfgEstado validar(const VliwCfgEntrada *e,unsigned char *lider,
    size_t *por_operacion,VliwCfgDiagnostico *d)
{
    size_t n=e->cantidad_operaciones,fin=0;
    /* Primero acreditamos la partición de funciones. Así una referencia
     * posterior no puede usar una función vacía, solapada o fuera del flujo. */
    for(size_t f=0;f<e->cantidad_funciones;++f) {
        const VliwCfgFuncion *v=&e->funciones[f];
        if(v->id==VAC || v->inicio!=fin || v->fin<=v->inicio || v->fin>n)
            return ERROR(VLIW_CFG_FRONTERA,f,"función vacía o partición inválida");
        for(size_t j=0;j<f;++j) if(e->funciones[j].id==v->id)
            return ERROR(VLIW_CFG_IDENTIDAD,f,"ID de función duplicado");
        lider[v->inicio]=1; fin=v->fin;
    }
    if(fin!=n) return ERROR(VLIW_CFG_FRONTERA,VAC,"funciones no cubren el flujo");
    /* Cada símbolo conserva función y posición; identidades distintas pueden
     * compartir entrada. Marcar todas las etiquetas evita saltar al medio
     * de un bloque, incluso si todavía no examinamos las transferencias. */
    for(size_t s=0;s<e->cantidad_simbolos;++s) {
        const VliwCfgSimbolo *v=&e->simbolos[s];
        size_t f=funcion(e,v->posicion);
        if(v->id==VAC || f==VAC || e->funciones[f].id!=v->funcion)
            return ERROR(VLIW_CFG_DESTINO,s,"símbolo fuera de su función");
        for(size_t j=0;j<s;++j) if(e->simbolos[j].id==v->id)
            return ERROR(VLIW_CFG_IDENTIDAD,s,"ID de símbolo duplicado o ambiguo");
        lider[v->posicion]=1;
    }
    /* Las fronteras adicionales vienen justificadas por el productor; no
     * se deducen restricciones invisibles de una expansión desde sus opcodes. */
    for(size_t j=0;j<e->cantidad_fronteras;++j) {
        if(e->fronteras[j]>=n) return ERROR(VLIW_CFG_FRONTERA,j,"frontera fuera del flujo");
        lider[e->fronteras[j]]=1;
    }
    /* S2 valida palabra, slot y forma física. La intención de llamada/retorno
     * es externa: no inferimos ABI a partir de escrituras o lecturas de x1.
     * Una BRU deja a la operación siguiente en un bloque diferente. */
    for(size_t i=0;i<n;++i) {
        const VliwCfgOperacion *o=&e->operaciones[i]; SimInstruccion real;
        for(size_t j=0;j<i;++j) if(e->operaciones[j].id==o->id)
            return ERROR(VLIW_CFG_IDENTIDAD,i,"ID de operación duplicado");
        if(sim_decode_palabra(o->palabra,(unsigned)o->slot,&real)!=SIM_DECODE_OK)
            return ERROR(VLIW_CFG_ISA,i,"operación no reconocida por S2");
        if(o->salto!=real.salto || (unsigned)o->control>VLIW_CONTROL_RETORNO ||
           ((o->slot==VLIW_SLOT_BRU)!=(o->control!=VLIW_CONTROL_NINGUNO)))
            return ERROR(VLIW_CFG_CONTROL,i,"forma o clasificación de operación incompatible");
        por_operacion[i]=VAC;
        if(o->control!=VLIW_CONTROL_NINGUNO && i+1<n) lider[i+1]=1;
    }
    /* Construimos una referencia única por posición BRU y contrastamos tipo,
     * continuación y crédito del destino. Los IDs simbólicos no son PCs. */
    for(size_t t=0;t<e->cantidad_transferencias;++t) {
        const VliwCfgTransferencia *v=&e->transferencias[t];size_t i=v->operacion;
        if(i>=n) return ERROR(VLIW_CFG_DESTINO,t,"transferencia fuera del flujo");
        const VliwCfgOperacion *o=&e->operaciones[i];
        if(o->control==VLIW_CONTROL_NINGUNO || por_operacion[i]!=VAC ||
           (unsigned)v->tipo>=VLIW_DEST_NO_RESUELTO)
            return ERROR(VLIW_CFG_CONTROL,t,"transferencia ausente, duplicada o no resuelta");
        por_operacion[i]=t;
        bool directo=v->tipo==VLIW_DEST_DIRECTO || v->tipo==VLIW_DEST_CONDICIONAL;
        bool cond=v->tipo==VLIW_DEST_CONDICIONAL,call=o->control==VLIW_CONTROL_LLAMADA;
        if((cond && (o->salto!=SIM_SALTO_COND_PC || o->control!=VLIW_CONTROL_SALTO)) ||
           (v->tipo==VLIW_DEST_DIRECTO && (o->salto!=SIM_SALTO_PC || o->control==VLIW_CONTROL_RETORNO)) ||
           (!directo && (o->salto!=SIM_SALTO_REGISTRO ||
              ((v->tipo==VLIW_DEST_RETORNO)!=(o->control==VLIW_CONTROL_RETORNO)))))
            return ERROR(VLIW_CFG_CONTROL,t,"tipo de transferencia contradice forma S2");
        size_t f=funcion(e,i),siguiente=i+1<n ? i+1 : VAC;
        if(v->continuacion!=((cond || call) ? siguiente : VAC))
            return ERROR(VLIW_CFG_FRONTERA,t,"continuación no secuencial");
        if((cond || call) && siguiente!=VAC && funcion(e,siguiente)!=f)
            return ERROR(VLIW_CFG_FRONTERA,t,"caída cruza funciones");
        if(call && siguiente==VAC)
            return ERROR(VLIW_CFG_INCOMPLETO,t,"llamada sin continuación física");
        if(v->cantidad_posibles>e->cantidad_simbolos ||
           (v->cantidad_posibles && !v->posibles))
            return ERROR(VLIW_CFG_ARGUMENTO,t,"tabla de destinos posibles inválida");
        if((directo && (v->cantidad_posibles || !v->completos)) ||
           (!directo && (v->tomado!=VAC || (v->completos && !v->cantidad_posibles))))
            return ERROR(VLIW_CFG_INCOMPLETO,t,"destino directo no acreditado o indirecto estático");
        size_t cantidad=directo ? 1 : v->cantidad_posibles;
        /* Todas las referencias se resuelven mediante símbolos acreditados.
         * Una llamada solo entra por el inicio de función; retornos dinámicos
         * pueden ir a continuaciones de otra función sin inventarlas aquí. */
        for(size_t k=0;k<cantidad;++k) {
            size_t s=simbolo(e,directo ? v->tomado : v->posibles[k]);
            if(s==VAC) return ERROR(VLIW_CFG_DESTINO,t,"símbolo destino no definido");
            size_t p=e->simbolos[s].posicion,g=funcion(e,p);
            if((call && p!=e->funciones[g].inicio) ||
               (!call && o->control!=VLIW_CONTROL_RETORNO && g!=f))
                return ERROR(VLIW_CFG_DESTINO,t,"destino incompatible con función o llamada");
            lider[p]=1;
        }
    }
    /* Revisar cobertura al final detecta BRU sin referencia. Una frontera de
     * función nunca introduce por sí sola un retorno ni una caída segura. */
    for(size_t i=0;i<n;++i) {
        if(e->operaciones[i].control!=VLIW_CONTROL_NINGUNO && por_operacion[i]==VAC)
            return ERROR(VLIW_CFG_INCOMPLETO,i,"BRU sin referencia de transferencia");
        if(i+1<n && funcion(e,i)!=funcion(e,i+1) && e->operaciones[i].control==VLIW_CONTROL_NINGUNO)
            return ERROR(VLIW_CFG_FRONTERA,i,"fin de función sin transferencia explícita");
    }
    /* Solo después de conocer todos los líderes podemos rechazar entradas
     * interiores a regiones protegidas, incluidas las creadas por una BRU. */
    for(size_t r=0;r<e->cantidad_regiones;++r) {
        const VliwCfgRegion *v=&e->regiones[r];
        if(v->inicio>=v->fin || v->fin>n || funcion(e,v->inicio)!=funcion(e,v->fin-1))
            return ERROR(VLIW_CFG_FRONTERA,r,"región indivisible inválida");
        for(size_t i=v->inicio+1;i<v->fin;++i) if(lider[i])
            return ERROR(VLIW_CFG_FRONTERA,r,"entrada al interior de región indivisible");
    }
    return VLIW_CFG_OK;
}
/* Coordina validación, líderes, partición y resolución de sucesores. La
 * salida del cliente cambia una sola vez; cualquier fallo libera temporales. */
VliwCfgEstado vliw_cfg_construir(const VliwCfgEntrada *e,VliwCfg **salida,VliwCfgDiagnostico *d)
{
    if(!e || !salida || *salida) return ERROR(VLIW_CFG_ARGUMENTO,VAC,"entrada y salida inicial NULL requeridas");
    VliwCfgEstado estado=dimensiones(e,d); if(estado) return estado;
    size_t n=e->cantidad_operaciones;
    unsigned char *lider=n ? calloc(n,1) : NULL;
    size_t *referencia=n ? malloc(n*sizeof *referencia) : NULL;
    VliwCfg *c=NULL;size_t *sucesores=NULL;
    if(n && (!lider || !referencia)) {estado=ERROR(VLIW_CFG_MEMORIA,VAC,"sin auxiliares para fronteras");goto fin;}
    estado=validar(e,lider,referencia,d);if(estado) goto fin;
    c=calloc(1,sizeof *c);
    if(!c) {estado=ERROR(VLIW_CFG_MEMORIA,VAC,"sin resultado CFG");goto fin;}
    c->generacion=e->generacion;c->cantidad_operaciones=n;c->destinos_dinamicos_acreditados=true;
    c->cantidad_destinos=e->cantidad_transferencias;
    for(size_t i=0;i<n;++i) c->cantidad_bloques+=lider[i]!=0;
    size_t nb=c->cantidad_bloques;
    /* Los líderes dividen sin huecos. Cada posición recibe exactamente un
     * índice de bloque, conservando IDs/operaciones originales inmutables. */
    c->bloques=nb ? calloc(nb,sizeof *c->bloques) : NULL;
    c->bloque_por_operacion=n ? malloc(n*sizeof(size_t)) : NULL;
    c->destinos=c->cantidad_destinos ? calloc(c->cantidad_destinos,sizeof *c->destinos) : NULL;
    sucesores=nb ? malloc(nb*sizeof *sucesores) : NULL;
    if((nb && (!c->bloques || !sucesores)) || (n && !c->bloque_por_operacion) ||
       (c->cantidad_destinos && !c->destinos)) {estado=ERROR(VLIW_CFG_MEMORIA,VAC,"sin arreglos CFG");goto fin;}
    size_t b=0;
    for(size_t i=0;i<n;++i) {
        if(lider[i]) {if(i) {c->bloques[b].fin=i;++b;} c->bloques[b].inicio=i;}
        c->bloque_por_operacion[i]=b;
    }
    if(nb) c->bloques[nb-1].fin=n;
    /* El CFG puede contener ciclos: agregar un sucesor anterior no crea
     * una dependencia de emisión ni inicia un recorrido recursivo. */
    for(b=0;b<nb;++b) {
        size_t i=c->bloques[b].fin-1,t=referencia[i],ns=0;
        if(t==VAC) {if(b+1<nb) agregar(sucesores,&ns,b+1);}
        else {
            const VliwCfgTransferencia *v=&e->transferencias[t];
            bool directo=v->tipo==VLIW_DEST_DIRECTO || v->tipo==VLIW_DEST_CONDICIONAL;
            size_t tomado=VAC,caida=VAC;
            if(directo) {tomado=c->bloque_por_operacion[e->simbolos[simbolo(e,v->tomado)].posicion];agregar(sucesores,&ns,tomado);}
            else {
                if(!v->completos) c->destinos_dinamicos_acreditados=false;
                for(size_t k=0;k<v->cantidad_posibles;++k)
                    agregar(sucesores,&ns,c->bloque_por_operacion[e->simbolos[simbolo(e,v->posibles[k])].posicion]);
            }
            if(v->continuacion!=VAC) {caida=c->bloque_por_operacion[v->continuacion];agregar(sucesores,&ns,caida);}
            c->destinos[t]=(VliwDestino){i,v->tipo,tomado,caida};
        }
        if(ns) {
            size_t *propios=malloc(ns*sizeof *propios);
            if(!propios) {estado=ERROR(VLIW_CFG_MEMORIA,b,"sin sucesores propios del bloque");goto fin;}
            for(size_t j=0;j<ns;++j) propios[j]=sucesores[j];
            c->bloques[b].sucesores=propios;c->bloques[b].cantidad_sucesores=ns;
        }
    }
    *salida=c;c=NULL;estado=ERROR(VLIW_CFG_OK,VAC,"CFG estructural construido; ABI/DAG/reubicaciones externos");
fin:
    free(lider);free(referencia);free(sucesores);vliw_cfg_liberar(&c);
    return estado;
}
