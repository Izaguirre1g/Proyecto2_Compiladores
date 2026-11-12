/* Consume operaciones físicas explícitas y compone las APIs reales del equipo.
 * Posee resultados e intermediarios; no genera instrucciones desde BAL/IR ni
 * completa símbolos, ABI o reubicaciones. Publica únicamente la cadena validada. */
#include "vliw_cadena_fisica.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VAC VLIW_VACIO

/* Conserva fase/categoría/estado original y copia el mensaje antes de liberar
 * intermediarios. Los índices no disponibles permanecen VAC, nunca se inventan. */
static VliwCadenaEstado informar(VliwCadenaDiagnostico *d,VliwCadenaEstado estado,
    VliwCadenaFase fase,int original,size_t i,const char *mensaje)
{
    if(d){d->estado=estado;d->fase=fase;d->estado_original=original;d->instruccion=i;
        (void)snprintf(d->mensaje,sizeof d->mensaje,"%s",mensaje);}
    return estado;
}
/* El plan indexa la proyección: se destruye antes de sus instrucciones/bloques.
 * CFG/DAG no prestan sus arreglos a la proyección, pero se liberan al final para
 * que cualquier construcción parcial siga el mismo orden seguro. */
void vliw_cadena_liberar(VliwCadena **salida)
{
    if(!salida || !*salida)return;
    VliwCadena *c=*salida;
    vliw_visual_liberar(&c->visualizacion_json);cb_liberar(&c->codigo);
    vliw_emisor_liberar(&c->asm_texto);vliw_liberar(&c->plan);
    vliw_proyeccion_liberar(&c->proyeccion);vliw_dag_liberar(&c->dag);
    vliw_cfg_liberar(&c->cfg);free(c->metadatos);free(c);*salida=NULL;
}
/* CB conserva su interfaz entero+diagnóstico FILE. Se captura el diagnóstico
 * en un flujo privado para distinguir E/S y reserva de un rechazo de código.
 * El programa permanece privado hasta que toda la cadena termine en éxito. */
static VliwCadenaEstado codificar(VliwCadena *c,VliwCadenaDiagnostico *d)
{
    if(!c->cantidad_metadatos)return VLIW_CAD_OK;
    FILE *f=tmpfile();if(!f)return informar(d,VLIW_CAD_IO,VLIW_CAD_CODIFICACION,0,VAC,"no se pudo crear diagnóstico CB");
    int cb=cb_ensamblar(c->asm_texto,&c->codigo,f);char mensaje[512]={0};
    bool io=fflush(f)!=0 || fseek(f,0,SEEK_SET)!=0;
    if(!io && !fgets(mensaje,sizeof mensaje,f))io=ferror(f)!=0;
    if(fclose(f)!=0)io=true;
    if(io)return informar(d,VLIW_CAD_IO,VLIW_CAD_CODIFICACION,cb,VAC,"fallo de E/S del diagnóstico CB");
    if(cb)return informar(d,strstr(mensaje,"sin memoria")?VLIW_CAD_MEMORIA:VLIW_CAD_CODIGO,
        VLIW_CAD_CODIFICACION,cb,VAC,*mensaje?mensaje:"CB rechazó la emisión");
    c->codificado=true;return VLIW_CAD_OK;
}
/* Contrasta todas las posiciones, incluidos NOP, mediante S2. Los offsets
 * directos se comprueban contra las etiquetas de protección del plan, no los
 * inmediatos antiguos. SYER conserva su inmediato: las direcciones dinámicas
 * siguen siendo una garantía externa, nunca se transforman en saltos directos. */
static VliwCadenaEstado contrastar(const VliwCadenaEntrada *e,const VliwCadena *c,
    VliwCadenaDiagnostico *d)
{
    if(c->codigo.cantidad!=c->plan->cantidad_bundles)
        return informar(d,VLIW_CAD_CODIGO,VLIW_CAD_CONTRASTE,0,VAC,"CB alteró la cantidad de bundles");
    for(size_t q=0;q<c->codigo.cantidad;++q)for(unsigned s=0;s<VLIW_SLOTS;++s){
        size_t i=c->plan->bundles[q].instrucciones[s];uint32_t palabra=c->codigo.bundles[q].slots[s];
        if(i==VAC){if(palabra)return informar(d,VLIW_CAD_CODIGO,VLIW_CAD_CONTRASTE,0,VAC,"slot vacío distinto de NOP");continue;}
        SimInstruccion a,b;
        if(sim_decode_palabra(e->flujo->operaciones[i].palabra,s,&a)!=SIM_DECODE_OK ||
            sim_decode_palabra(palabra,s,&b)!=SIM_DECODE_OK || !vliw_proyeccion_misma_operacion(&a,&b))
            return informar(d,VLIW_CAD_CODIGO,VLIW_CAD_CONTRASTE,0,i,"S2 detectó una operación emitida distinta");
        if(a.salto==SIM_SALTO_PC || a.salto==SIM_SALTO_COND_PC){
            const VliwDestinos *ds=&c->proyeccion->destinos;size_t t=0;
            while(t<ds->cantidad && ds->items[t].instruccion!=i)++t;
            if(t==ds->cantidad)return informar(d,VLIW_CAD_CODIGO,VLIW_CAD_CONTRASTE,0,i,"falta destino lógico emitido");
            int64_t efectivo=(int64_t)(q*16)+b.inmediato;
            size_t esperado=c->plan->inicio_por_bloque[ds->items[t].tomado]*16;
            if(efectivo!=(int64_t)esperado){if(d)d->destino=t;
                return informar(d,VLIW_CAD_CODIGO,VLIW_CAD_CONTRASTE,0,i,"offset emitido no alcanza el destino lógico");}
        }
    }
    return VLIW_CAD_OK;
}
/* Ejecuta cada fase real una sola vez a nivel coordinador. La proyección
 * revalida internamente sus préstamos para poder usarse también de forma
 * independiente. Un rechazo detiene esta entrada: no corrige planes ni activa
 * garantías automáticamente. Todos los recursos se adquieren en privado. */
VliwCadenaEstado vliw_cadena_construir(const VliwCadenaEntrada *e,VliwCadena **salida,
    VliwCadenaDiagnostico *d)
{
    if(d){memset(d,0,sizeof *d);d->instruccion=d->dependencia=d->bloque=d->destino=VAC;d->generacion=e?e->generacion:0;}
    if(!e || !salida || *salida || !e->flujo)
        return informar(d,VLIW_CAD_ARGUMENTO,VLIW_CAD_INICIO,0,VAC,"entrada y salida inicialmente NULL requeridas");
    size_t n=e->flujo->cantidad_operaciones;
    if(n>VLIW_MAX_BUNDLES*(size_t)VLIW_SLOTS)
        return informar(d,VLIW_CAD_CAPACIDAD,VLIW_CAD_INICIO,0,VAC,"demasiadas operaciones físicas");
    if(e->cantidad_textos!=n || e->cantidad_efectos!=n || (n && (!e->textos || !e->efectos_adicionales || !e->flujo->operaciones)))
        return informar(d,VLIW_CAD_ARGUMENTO,VLIW_CAD_INICIO,0,VAC,"se requieren operaciones, textos y efectos por posición");
    if(e->generacion!=e->flujo->generacion || e->garantias.pendientes ||
        (n && (!e->garantias.completitud_declarada || !e->efectos_acreditados)))
        return informar(d,VLIW_CAD_INCOMPLETO,VLIW_CAD_INICIO,0,VAC,"generación, efectos o completitud pendientes");
    if(n && !e->garantias.reubicaciones_resueltas)
        return informar(d,VLIW_CAD_REUBICACION,VLIW_CAD_INICIO,0,VAC,"direcciones o reubicaciones pendientes del productor");
    for(size_t i=0;i<n;++i)if((e->flujo->operaciones[i].control==VLIW_CONTROL_LLAMADA ||
        e->flujo->operaciones[i].control==VLIW_CONTROL_RETORNO) && !e->abi_acreditado)
        return informar(d,VLIW_CAD_ABI,VLIW_CAD_INICIO,0,i,"llamada/retorno sin preservación ABI acreditada");
    VliwCadena *c=calloc(1,sizeof *c);
    if(!c)return informar(d,VLIW_CAD_MEMORIA,VLIW_CAD_INICIO,0,VAC,"sin memoria para cadena");
    c->generacion=e->generacion;c->cantidad_metadatos=n;
    VliwCadenaEstado estado=VLIW_CAD_OK;
    // S2 produce máscaras físicas parciales; sus pendientes no se borran para
    // fingir que el consumidor puede acreditar al productor externo.
    c->metadatos=n?calloc(n,sizeof *c->metadatos):NULL;
    if(n && !c->metadatos){estado=informar(d,VLIW_CAD_MEMORIA,VLIW_CAD_META,0,VAC,"sin memoria para metadatos");goto fin;}
    for(size_t i=0;i<n;++i){VliwMetadatosDiagnostico md;
        SimDecodeEstado se=vliw_metadatos_extraer(e->flujo->operaciones[i].palabra,
            (unsigned)e->flujo->operaciones[i].slot,&c->metadatos[i],&md);
        if(se){estado=informar(d,VLIW_CAD_ISA,VLIW_CAD_META,se,i,md.mensaje);goto fin;}}
    // El CFG usa funciones/símbolos y referencias de esta generación. Compartir
    // el número no autentica contenido: esa responsabilidad sigue en el cliente.
    VliwCfgDiagnostico cd;VliwCfgEstado ce=vliw_cfg_construir(e->flujo,&c->cfg,&cd);
    if(ce){estado=informar(d,ce==VLIW_CFG_MEMORIA?VLIW_CAD_MEMORIA:ce==VLIW_CFG_CAPACIDAD?
        VLIW_CAD_CAPACIDAD:VLIW_CAD_CONTROL,VLIW_CAD_CFG,ce,cd.indice,cd.mensaje);goto fin;}
    // El DAG real cubre riesgos globales. Los backedges permanecen en el CFG;
    // no se convierten en dependencias retrospectivas del orden de emisión.
    VliwDagEntrada de={e->flujo,c->cfg,c->metadatos,e->efectos_adicionales,n,e->generacion,
        e->garantias.pendientes,e->efectos_acreditados,e->abi_acreditado};
    VliwDagDiagnostico dd;VliwDagEstado ds=vliw_dag_construir(&de,&c->dag,&dd);
    if(ds){estado=informar(d,ds==VLIW_DAG_MEMORIA?VLIW_CAD_MEMORIA:ds==VLIW_DAG_CAPACIDAD?
        VLIW_CAD_CAPACIDAD:VLIW_CAD_DEPENDENCIAS,VLIW_CAD_DAG,ds,dd.indice,dd.mensaje);goto fin;}
    // La proyección copia sus préstamos: plan/emisión no dependen de tablas
    // mutables del productor ni sustituyen el DAG por aristas manuales.
    VliwProyeccionEntrada pe={de,c->dag,e->textos,e->cantidad_textos,e->garantias};
    VliwProyeccionDiagnostico pd;VliwProyeccionEstado ps=vliw_proyeccion_construir(&pe,&c->proyeccion,&pd);
    if(ps){if(d){d->fase_proyeccion=pd.fase;d->estado_interno=pd.estado_original;d->dependencia=pd.dependencia;d->bloque=pd.bloque;d->destino=pd.destino;}
        estado=informar(d,ps==VLIW_PROY_MEMORIA?VLIW_CAD_MEMORIA:ps==VLIW_PROY_IO?VLIW_CAD_IO:
            ps==VLIW_PROY_CAPACIDAD?VLIW_CAD_CAPACIDAD:VLIW_CAD_METADATOS,
            VLIW_CAD_PROYECCION,ps,pd.instruccion,pd.mensaje);goto fin;}
    // Se mantiene I1-A1 explícito en la cadena aunque la proyección ya lo usó.
    // Coherencia local no demuestra ABI, equivalencia BAL o capacidad del plan.
    VliwIntegracionDiagnostico id;VliwEntrada *in=&c->proyeccion->entrada;VliwDestinos *dest=&c->proyeccion->destinos;
    VliwIntegracionEstado is=vliw_integracion_validar(in,dest,&id);
    if(is){if(d){d->dependencia=id.dependencia;d->bloque=id.bloque;d->destino=id.destino;}
        estado=informar(d,is==VLIW_INT_MEMORIA?VLIW_CAD_MEMORIA:is==VLIW_INT_IO?VLIW_CAD_IO:
            VLIW_CAD_METADATOS,VLIW_CAD_VALIDACION,is,id.instruccion,id.mensaje);goto fin;}
    // S7 determina bundles estáticos y guardas N+3; sus posiciones no son una
    // medición de ciclos ejecutados ni una estimación dinámica de IPC.
    VliwDiagnostico sd;VliwEstado ss=vliw_planificar(in,&c->plan,&sd);
    if(ss){if(d)d->dependencia=sd.dependencia;
        estado=informar(d,ss==VLIW_MEMORIA?VLIW_CAD_MEMORIA:ss==VLIW_CAPACIDAD?VLIW_CAD_CAPACIDAD:
            VLIW_CAD_PLAN_INVALIDO,VLIW_CAD_PLAN,ss,sd.instruccion,sd.mensaje);goto fin;}
    // La comprobación explícita permanece aunque S7 ya verificó su resultado.
    // No se acepta un plan por el único hecho de que planificar devolvió OK.
    ss=vliw_verificar(in,c->plan,&sd);
    if(ss){if(d)d->dependencia=sd.dependencia;
        estado=informar(d,ss==VLIW_MEMORIA?VLIW_CAD_MEMORIA:VLIW_CAD_PLAN_INVALIDO,
            VLIW_CAD_VERIFICACION,ss,sd.instruccion,sd.mensaje);goto fin;}
    // S8-A reescribe solo destinos directos estructurados. No se llama al
    // enlazador tradicional ni se corrigen constantes antiguas de x13.
    VliwEmisorDiagnostico ed;VliwEmisorEstado es=vliw_emitir(in,c->plan,dest,&c->asm_texto,&ed);
    if(es){if(d){d->bloque=ed.bloque;d->estado_interno=ed.estado_plan;}
        estado=informar(d,es==VLIW_EM_MEMORIA?VLIW_CAD_MEMORIA:es==VLIW_EM_IO?VLIW_CAD_IO:
            es==VLIW_EM_RANGO?VLIW_CAD_CAPACIDAD:VLIW_CAD_EMISOR,
            VLIW_CAD_EMISION,es,ed.instruccion,ed.mensaje);goto fin;}
    // El código completo se ensambla con CB y se contrasta con S2, incluidos
    // los NOP y el destino final del offset que S8-A acaba de materializar.
    estado=codificar(c,d);if(estado)goto fin;
    estado=contrastar(e,c,d);if(estado)goto fin;
    // Las estadísticas y la representación JSON pertenecen a S8-C. Describen
    // ocupación del plan; los ciclos/IPC reales requieren ejecutar el simulador.
    VliwVisualDiagnostico vd;VliwVisualEstado vs=vliw_estadisticas(in,c->plan,&c->estadisticas,&vd);
    if(vs){if(d)d->dependencia=vd.dependencia;
        estado=informar(d,vs==VLIW_VIS_MEMORIA?VLIW_CAD_MEMORIA:VLIW_CAD_PLAN_INVALIDO,
            VLIW_CAD_ESTADISTICAS,vs,vd.instruccion,vd.mensaje);goto fin;}
    vs=vliw_visualizar(in,c->plan,dest,VLIW_VIS_JSON,NULL,&c->visualizacion_json,&vd);
    if(vs){if(d)d->dependencia=vd.dependencia;
        estado=informar(d,vs==VLIW_VIS_MEMORIA?VLIW_CAD_MEMORIA:VLIW_CAD_PLAN_INVALIDO,
            VLIW_CAD_VISUALIZACION,vs,vd.instruccion,vd.mensaje);goto fin;}
    *salida=c;return informar(d,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION,0,VAC,"cadena física completa; semántica externa no acreditada por el consumidor");
fin:
    vliw_cadena_liberar(&c);return estado;
}
