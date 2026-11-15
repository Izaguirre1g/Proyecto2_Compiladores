/* Construye una vista S7 independiente de la vida del productor físico.
 * Recibe CFG/DAG/metadatos reales y garantías explícitas; posee las copias
 * publicadas. Reutiliza validadores, sin backend, enlace ni acreditación BAL. */
#include "vliw_proyeccion_fisica.h"
#include "codificacion_binaria.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VAC VLIW_VACIO

/* Copia diagnóstico por valor, sin prestar el buffer local de CB. El estado
 * original permite distinguir un rechazo de coherencia de falta de recursos. */
static VliwProyeccionEstado informar(VliwProyeccionDiagnostico *d,
    VliwProyeccionEstado e,int original,size_t i,const char *mensaje)
{
    if(d){d->estado=e;d->estado_original=original;d->instruccion=i;
        (void)snprintf(d->mensaje,sizeof d->mensaje,"%s",mensaje);}
    return e;
}
/* Las reservas se inicializan antes de adquirir hijos. Esta limpieza sirve
 * tanto a un resultado publicado como a uno parcial; nunca libera préstamos. */
void vliw_proyeccion_liberar(VliwProyeccion **salida)
{
    if(!salida || !*salida)return;
    VliwProyeccion *p=*salida;
    if(p->entrada.instrucciones)for(size_t i=0;i<p->entrada.cantidad_instrucciones;++i)
        free((void *)p->entrada.instrucciones[i].texto);
    if(p->entrada.bloques)for(size_t b=0;b<p->entrada.cantidad_bloques;++b)
        free((void *)p->entrada.bloques[b].sucesores);
    free((void *)p->entrada.instrucciones);free((void *)p->entrada.dependencias);
    free((void *)p->entrada.bloques);free((void *)p->destinos.items);free(p);*salida=NULL;
}
/* Se comparan valores arquitectónicos, nunca padding ni bits que S2 ignora.
 * El offset de un salto directo será sustituido por S8-A; SYER y las ALU que
 * materializan direcciones conservan todos sus inmediatos y obligaciones. */
bool vliw_proyeccion_misma_operacion(const SimInstruccion *a,const SimInstruccion *b)
{
    if(!a || !b || a->estado!=SIM_DECODE_OK || b->estado!=SIM_DECODE_OK)return false;
    bool directo=a->salto==SIM_SALTO_PC || a->salto==SIM_SALTO_COND_PC;
    return a->slot==b->slot && a->familia==b->familia && a->formato==b->formato &&
        a->operacion==b->operacion && a->rd==b->rd && a->rd2==b->rd2 &&
        a->rs1==b->rs1 && a->rs2==b->rs2 && a->salto==b->salto &&
        a->ancho_inmediato==b->ancho_inmediato && a->inmediato_con_signo==b->inmediato_con_signo &&
        (directo || (a->inmediato==b->inmediato && a->inmediato_bruto==b->inmediato_bruto)) &&
        a->corrimiento==b->corrimiento && a->lk==b->lk && a->rk==b->rk &&
        a->off_boveda==b->off_boveda && a->direccion==b->direccion &&
        a->tiene_direccion==b->tiene_direccion && a->requiere_auth==b->requiere_auth &&
        a->requiere_init==b->requiere_init;
}
/* Reutiliza CB sobre una operación ya validada por I1-A1. Solo el operando
 * directo que S8-A reemplazará se normaliza a cero; no se interpreta como PC.
 * S2 compara operación, operandos, inmediatos y campos CRIPTO con la palabra
 * original. Así un texto con máscaras iguales pero otro inmediato se rechaza.
 * El contexto, programa y flujo de diagnóstico son privados y se liberan aquí. */
static VliwProyeccionEstado contrastar_texto(const VliwCfgOperacion *o,
    const char *texto,size_t i,VliwProyeccionDiagnostico *d)
{
    SimInstruccion original,real;
    if(sim_decode_palabra(o->palabra,(unsigned)o->slot,&original)!=SIM_DECODE_OK)
        return informar(d,VLIW_PROY_TEXTO,0,i,"S2 rechazó la operación física");
    char copia[160];size_t n=strlen(texto);memcpy(copia,texto,n+1);
    if(original.salto==SIM_SALTO_PC || original.salto==SIM_SALTO_COND_PC){
        char *coma=strrchr(copia,',');
        if(!coma)return informar(d,VLIW_PROY_TEXTO,0,i,"falta marcador de destino directo");
        coma[1]='0';coma[2]='\0';
    }
    const char *slots[4]={"nop","nop","nop","nop"};slots[o->slot]=copia;
    char contexto[256],detalle[256]={0};
    (void)snprintf(contexto,sizeof contexto,"%s | %s | %s | %s\n",slots[0],slots[1],slots[2],slots[3]);
    FILE *f=tmpfile();if(!f)return informar(d,VLIW_PROY_IO,0,i,"no se pudo crear diagnóstico CB");
    CbPrograma cb={0};int estado=cb_ensamblar(contexto,&cb,f);
    bool io=fflush(f)!=0 || fseek(f,0,SEEK_SET)!=0;
    if(!io && !fgets(detalle,sizeof detalle,f))io=ferror(f)!=0;
    if(fclose(f)!=0)io=true;
    bool coincide=!estado && cb.cantidad==1 &&
        sim_decode_palabra(cb.bundles[0].slots[o->slot],(unsigned)o->slot,&real)==SIM_DECODE_OK &&
        vliw_proyeccion_misma_operacion(&original,&real);
    cb_liberar(&cb);
    if(io)return informar(d,VLIW_PROY_IO,estado,i,"fallo de E/S del diagnóstico CB");
    if(!coincide)return informar(d,strstr(detalle,"sin memoria")?VLIW_PROY_MEMORIA:VLIW_PROY_TEXTO,
        estado,i,"texto ISA distinto de la operación física o rechazado por CB");
    return VLIW_PROY_OK;
}
/* Revalida el CFG/metadatos con el constructor DAG real y compara exactamente
 * generación, operaciones, camino y cada arista recibida. No reconstruye las
 * dependencias para sustituir una entrada errónea: la reconstrucción solo es
 * contraste, se libera y se copian las aristas originales si coinciden. */
static VliwProyeccionEstado comprobar_dag(const VliwProyeccionEntrada *e,VliwProyeccionDiagnostico *d)
{
    const VliwDag *a=e->dag;
    if(a->cantidad_dependencias>VLIW_DAG_MAX_ARISTAS || (a->cantidad_dependencias && !a->dependencias))
        return informar(d,VLIW_PROY_DAG,VLIW_DAG_ARGUMENTO,VAC,"arreglo DAG fuera de contrato");
    VliwDag *b=NULL;VliwDagDiagnostico gd;
    VliwDagEstado estado=vliw_dag_construir(&e->fisica,&b,&gd);
    if(estado)return informar(d,estado==VLIW_DAG_MEMORIA?VLIW_PROY_MEMORIA:
        estado==VLIW_DAG_CAPACIDAD?VLIW_PROY_CAPACIDAD:VLIW_PROY_DAG,estado,gd.indice,gd.mensaje);
    bool igual=a->generacion==b->generacion && a->cantidad_operaciones==b->cantidad_operaciones &&
        a->cantidad_dependencias==b->cantidad_dependencias && a->camino_critico==b->camino_critico;
    for(size_t k=0;igual && k<a->cantidad_dependencias;++k){
        const VliwDependencia *u=&a->dependencias[k],*v=&b->dependencias[k];
        igual=u->antes==v->antes && u->despues==v->despues && u->tipo==v->tipo && u->distancia==v->distancia;
        if(!igual && d)d->dependencia=k;
    }
    vliw_dag_liberar(&b);
    return igual?VLIW_PROY_OK:informar(d,VLIW_PROY_DAG,0,VAC,"DAG recibido distinto del flujo físico vigente");
}
/* Coordina guardas, contraste real y copias propias. Las cantidades públicas
 * se fijan antes de reservar para permitir liberar cualquier construcción
 * parcial; los arreglos de hijos empiezan en cero. Solo el último punto publica. */
VliwProyeccionEstado vliw_proyeccion_construir(const VliwProyeccionEntrada *e,
    VliwProyeccion **salida,VliwProyeccionDiagnostico *d)
{
    if(d){memset(d,0,sizeof *d);d->instruccion=d->dependencia=d->bloque=d->destino=VAC;}
    if(!e || !salida || *salida || !e->fisica.flujo || !e->fisica.cfg || !e->dag)
        return informar(d,VLIW_PROY_ARGUMENTO,0,VAC,"entrada física, CFG, DAG y salida NULL requeridos");
    size_t n=e->fisica.flujo->cantidad_operaciones;
    if(n>VLIW_MAX_BUNDLES*(size_t)VLIW_SLOTS)
        return informar(d,VLIW_PROY_CAPACIDAD,0,VAC,"cantidad física excede capacidad estructural");
    if(e->cantidad_textos!=n || (n && !e->textos))
        return informar(d,VLIW_PROY_ARGUMENTO,0,VAC,"se requiere un texto por operación");
    if(e->garantias.pendientes || (n && !e->garantias.completitud_declarada) ||
        e->fisica.generacion!=e->dag->generacion)
        return informar(d,VLIW_PROY_INCOMPLETO,0,VAC,"completitud, obligaciones o generación sin acreditar");
    if(n && !e->garantias.reubicaciones_resueltas)
        return informar(d,VLIW_PROY_REUBICACION,0,VAC,"reubicaciones o direcciones materializadas pendientes");
    if(d)d->fase=VLIW_PROY_CONTRASTE_DAG;
    VliwProyeccionEstado estado=comprobar_dag(e,d);if(estado)return estado;
    const VliwCfg *cfg=e->fisica.cfg;
    if(d)d->fase=VLIW_PROY_COPIA;
    VliwProyeccion *p=calloc(1,sizeof *p);
    if(!p)return informar(d,VLIW_PROY_MEMORIA,0,VAC,"sin memoria para proyección");
    p->generacion=e->fisica.generacion;p->entrada.cantidad_instrucciones=n;
    p->entrada.cantidad_dependencias=e->dag->cantidad_dependencias;
    p->entrada.cantidad_bloques=cfg->cantidad_bloques;p->destinos.cantidad=cfg->cantidad_destinos;
    // Esta copia procede exclusivamente de una declaración explícita aceptada.
    // Un éxito de S2/CFG/DAG no activa garantías que el productor no proporcionó.
    p->entrada.metadatos_completos=e->garantias.completitud_declarada;
    VliwInstruccion *in=n?calloc(n,sizeof *in):NULL;p->entrada.instrucciones=in;
    VliwBloque *bloques=cfg->cantidad_bloques?calloc(cfg->cantidad_bloques,sizeof *bloques):NULL;p->entrada.bloques=bloques;
    VliwDependencia *ar=e->dag->cantidad_dependencias?malloc(e->dag->cantidad_dependencias*sizeof *ar):NULL;p->entrada.dependencias=ar;
    VliwDestino *dest=cfg->cantidad_destinos?malloc(cfg->cantidad_destinos*sizeof *dest):NULL;p->destinos.items=dest;
    if((n && !in) || (cfg->cantidad_bloques && !bloques) || (e->dag->cantidad_dependencias && !ar) ||
        (cfg->cantidad_destinos && !dest))goto memoria;
    for(size_t k=0;k<e->dag->cantidad_dependencias;++k)ar[k]=e->dag->dependencias[k];
    for(size_t k=0;k<cfg->cantidad_destinos;++k)dest[k]=cfg->destinos[k];
    // Las tablas conservan partición y destinos físicos del CFG. Cada sucesor
    // se copia a almacenamiento independiente; no se conserva la tabla virtual IR.
    for(size_t b=0;b<cfg->cantidad_bloques;++b){
        size_t ns=cfg->bloques[b].cantidad_sucesores;
        size_t *su=ns?malloc(ns*sizeof *su):NULL;if(ns && !su)goto memoria;
        if(ns)memcpy(su,cfg->bloques[b].sucesores,ns*sizeof *su);
        bloques[b]=(VliwBloque){cfg->bloques[b].inicio,cfg->bloques[b].fin,su,ns};
    }
    for(size_t i=0;i<n;++i){
        const char *s=e->textos[i];size_t len=0;
        if(!s){estado=informar(d,VLIW_PROY_TEXTO,0,i,"falta texto ISA");goto fin;}
        while(len<160 && s[len])++len;
        if(!len || len==160){estado=informar(d,VLIW_PROY_TEXTO,0,i,"texto ISA vacío o demasiado largo");goto fin;}
        char *copia=malloc(len+1);if(!copia)goto memoria;memcpy(copia,s,len+1);
        const VliwCfgOperacion *o=&e->fisica.flujo->operaciones[i];
        const VliwMetadatosParciales *m=&e->fisica.metadatos[i];
        in[i]=(VliwInstruccion){o->id,o->slot,copia,m->lee,m->escribe,
            m->efectos_minimos|e->fisica.efectos_adicionales[i],o->control,cfg->bloque_por_operacion[i]};
    }
    // I1-A1 conserva su función de validar ISA, destinos y cobertura global.
    // Después se comprueba además identidad semántica palabra/texto, incluidos
    // inmediatos que no aparecen en las máscaras de dependencias.
    if(d)d->fase=VLIW_PROY_VALIDACION;
    VliwIntegracionDiagnostico id;
    VliwIntegracionEstado ie=vliw_integracion_validar(&p->entrada,&p->destinos,&id);
    if(d){d->integracion=id;d->dependencia=id.dependencia;d->bloque=id.bloque;d->destino=id.destino;}
    if(ie){estado=informar(d,ie==VLIW_INT_MEMORIA?VLIW_PROY_MEMORIA:ie==VLIW_INT_IO?VLIW_PROY_IO:
        VLIW_PROY_INTEGRACION,ie,id.instruccion,id.mensaje);goto fin;}
    if(d)d->fase=VLIW_PROY_CONTRASTE_TEXTO;
    for(size_t i=0;i<n;++i){estado=contrastar_texto(&e->fisica.flujo->operaciones[i],in[i].texto,i,d);if(estado)goto fin;}
    if(d)d->fase=VLIW_PROY_COMPLETA;
    *salida=p;return informar(d,VLIW_PROY_OK,0,VAC,"proyección propia validada; garantías externas conservadas");
memoria:
    estado=informar(d,VLIW_PROY_MEMORIA,0,VAC,"sin memoria para copiar la entrada física");
fin:
    vliw_proyeccion_liberar(&p);return estado;
}
