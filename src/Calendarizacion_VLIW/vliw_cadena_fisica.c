// La cadena recibe operaciones físicas y ejecuta en orden las etapas necesarias para generar el programa.
/* Consume operaciones físicas explícitas y compone las APIs reales del equipo.
 * Posee resultados e intermediarios; no genera instrucciones desde BAL/IR ni
 * completa símbolos, ABI o reubicaciones. Publica únicamente la cadena validada. */
#include "vliw_cadena_fisica.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VAC VLIW_VACIO


// Registra el tipo de error, la fase donde ocurrió y sus detalles; devuelve el estado.
// Guarda en el diagnóstico los códigos definidos en el .h, la fase y el mensaje del error.


/* Conserva fase/categoría/estado original y copia el mensaje antes de liberar
 * intermediarios. Los índices no disponibles permanecen VAC, nunca se inventan. */
static VliwCadenaEstado informar(VliwCadenaDiagnostico *d,VliwCadenaEstado estado,
    VliwCadenaFase fase,int original,size_t i,const char *mensaje)
{
    if(d){d->estado=estado;d->fase=fase;d->estado_original=original;d->instruccion=i;
        (void)snprintf(d->mensaje,sizeof d->mensaje,"%s",mensaje);}
    return estado;
}


// Libera todos los recursos de la cadena física y deja su puntero en NULL.
// Libera la memoria reservada durante la construcción VLIW, evitando fugas y punteros colgantes.
// Libera la cadena si falla su construcción o cuando ya no se necesitan sus resultados.

/* El plan indexa la proyección: se destruye antes de sus instrucciones/bloques.
 * CFG/DAG no prestan sus arreglos a la proyección, pero se liberan al final para
 * que cualquier construcción parcial siga el mismo orden seguro. */
void vliw_cadena_liberar(VliwCadena **salida)
{
    if(!salida || !*salida)return; // Si no existe una cadena válida, termina sin intentar liberar memoria.
    VliwCadena *c=*salida;
    vliw_visual_liberar(&c->visualizacion_json);cb_liberar(&c->codigo);
    vliw_emisor_liberar(&c->asm_texto);vliw_liberar(&c->plan);
    vliw_proyeccion_liberar(&c->proyeccion);vliw_dag_liberar(&c->dag);
    vliw_cfg_liberar(&c->cfg);free(c->metadatos);free(c);*salida=NULL;
}



// Convierte el ensamblador de nuestra ISA a binario mediante cb_ensamblar().
// Comprueba errores del archivo temporal y del codificador antes de confirmar el binario.


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


// Comprueba que el código binario generado sea correcto.
// Compara las instrucciones originales con las codificadas y revisa los saltos.

static VliwCadenaEstado contrastar(
    const VliwCadenaEntrada *e,
    const VliwCadena *c,
    VliwCadenaDiagnostico *d)
{
    // Verifica que el binario tenga la misma cantidad de bundles que el plan.
    if (c->codigo.cantidad != c->plan->cantidad_bundles)
        return informar(d, VLIW_CAD_CODIGO, VLIW_CAD_CONTRASTE,
            0, VAC, "CB alteró la cantidad de bundles");

    // Recorre todos los bundles y sus cuatro slots.
    for (size_t q = 0; q < c->codigo.cantidad; ++q)
    for (unsigned s = 0; s < VLIW_SLOTS; ++s) {

        // Obtiene la instrucción asignada al slot y su código binario.
        size_t i = c->plan->bundles[q].instrucciones[s];
        uint32_t palabra = c->codigo.bundles[q].slots[s];

        // Si el slot estaba vacío, comprueba que se haya codificado como NOP (cero).
        if (i == VAC) {
            if (palabra)
                return informar(d, VLIW_CAD_CODIGO, VLIW_CAD_CONTRASTE,
                    0, VAC, "slot vacío distinto de NOP");
            continue;
        }

        // Guarda la instrucción original en 'a' y la generada en 'b'.
        SimInstruccion a, b;

        // S2 decodifica ambas instrucciones y comprueba que representen la misma operación.
        if (sim_decode_palabra(
                e->flujo->operaciones[i].palabra, s, &a) != SIM_DECODE_OK ||
            sim_decode_palabra(
                palabra, s, &b) != SIM_DECODE_OK ||
            !vliw_proyeccion_misma_operacion(&a, &b))

            return informar(d, VLIW_CAD_CODIGO, VLIW_CAD_CONTRASTE,
                0, i, "S2 detectó una operación emitida distinta");

        // Si la instrucción es un salto directo, verifica que llegue al bloque correcto.
        if (a.salto == SIM_SALTO_PC || a.salto == SIM_SALTO_COND_PC) {

            // Obtiene la lista de destinos de los saltos.
            const VliwDestinos *ds = &c->proyeccion->destinos;
            size_t t = 0;

            // Busca el destino correspondiente a esta instrucción.
            while (t < ds->cantidad && ds->items[t].instruccion != i)
                ++t;

            // Si no encuentra el destino, reporta un error.
            if (t == ds->cantidad)
                return informar(d, VLIW_CAD_CODIGO, VLIW_CAD_CONTRASTE,
                    0, i, "falta destino lógico emitido");

            // Calcula a qué dirección llega realmente el salto generado.
            // Se multiplica por 16 porque cada bundle ocupa 16 bytes.
            int64_t efectivo = (int64_t)(q * 16) + b.inmediato;

            // Obtiene la dirección a la que debería llegar el salto.
            size_t esperado =
                c->plan->inicio_por_bloque[ds->items[t].tomado] * 16;

            // Si ambas direcciones son diferentes, el salto está mal codificado.
            if (efectivo != (int64_t)esperado) {
                if (d)
                    d->destino = t;

                return informar(d, VLIW_CAD_CODIGO, VLIW_CAD_CONTRASTE,
                    0, i, "offset emitido no alcanza el destino lógico");
            }
        }
    }

    // Si todas las verificaciones pasan, el código binario es válido.
    return VLIW_CAD_OK;
}



// FUNCIÓN PRINCIPAL DE LA CADENA FÍSICA VLIW.
// Recibe instrucciones físicas de nuestra ISA y coordina su organización en bundles.
// Al finalizar, entrega el ensamblador, el binario y los resultados de cada etapa.
// Construye la cadena física completa y solo la entrega si todas las etapas terminan sin errores.
// El productor O2 llama a esta función desde src/ir_vliw_programa.c con operaciones físicas ya preparadas.
VliwCadenaEstado vliw_cadena_construir(const VliwCadenaEntrada *e,VliwCadena **salida,
    VliwCadenaDiagnostico *d)
{
    // e = instrucciones físicas y datos de entrada.
    // salida = donde se guardará la cadena completa.
    // d = donde se guardarán los errores.

    // Prepara el registro de errores y marca como vacíos los índices no utilizados.
    if(d){memset(d,0,sizeof *d);d->instruccion=d->dependencia=d->bloque=d->destino=VAC;d->generacion=e?e->generacion:0;}

    // Comprueba que los datos necesarios para empezar existan.
    if(!e || !salida || *salida || !e->flujo)
        return informar(d,VLIW_CAD_ARGUMENTO,VLIW_CAD_INICIO,0,VAC,"entrada y salida inicialmente NULL requeridas");

    // Obtiene la cantidad de instrucciones físicas recibidas.
    size_t n=e->flujo->cantidad_operaciones;

    // Comprueba que la cantidad no supere el máximo de 1023 bundles por 4 slots.
    if(n>VLIW_MAX_BUNDLES*(size_t)VLIW_SLOTS)
        return informar(d,VLIW_CAD_CAPACIDAD,VLIW_CAD_INICIO,0,VAC,"demasiadas operaciones físicas");

    // Comprueba que cada instrucción tenga su texto y sus datos de efectos.
    if(e->cantidad_textos!=n || e->cantidad_efectos!=n || (n && (!e->textos || !e->efectos_adicionales || !e->flujo->operaciones)))
        return informar(d,VLIW_CAD_ARGUMENTO,VLIW_CAD_INICIO,0,VAC,"se requieren operaciones, textos y efectos por posición");

    // Comprueba que la información de entrada esté completa y corresponda a la misma generación.
    if(e->generacion!=e->flujo->generacion || e->garantias.pendientes ||
        (n && (!e->garantias.completitud_declarada || !e->efectos_acreditados)))
        return informar(d,VLIW_CAD_INCOMPLETO,VLIW_CAD_INICIO,0,VAC,"generación, efectos o completitud pendientes");

    // No continúa si el compilador todavía tiene direcciones o reubicaciones pendientes.
    if(n && !e->garantias.reubicaciones_resueltas)
        return informar(d,VLIW_CAD_REUBICACION,VLIW_CAD_INICIO,0,VAC,"direcciones o reubicaciones pendientes del productor");

    // Comprueba que las llamadas y retornos tengan garantizado el uso correcto de registros y pila (ABI).
    for(size_t i=0;i<n;++i)if((e->flujo->operaciones[i].control==VLIW_CONTROL_LLAMADA ||
        e->flujo->operaciones[i].control==VLIW_CONTROL_RETORNO) && !e->abi_acreditado)
        return informar(d,VLIW_CAD_ABI,VLIW_CAD_INICIO,0,i,"llamada/retorno sin preservación ABI acreditada");

    // Reserva memoria para la estructura que guardará todos los resultados.
    VliwCadena *c=calloc(1,sizeof *c);
    if(!c)return informar(d,VLIW_CAD_MEMORIA,VLIW_CAD_INICIO,0,VAC,"sin memoria para cadena");

    // Guarda la generación y la cantidad de instrucciones recibidas.
    c->generacion=e->generacion;c->cantidad_metadatos=n;

    // Inicialmente no hay errores.
    VliwCadenaEstado estado=VLIW_CAD_OK;


    // ETAPA 1: METADATOS
    // Extrae información de cada instrucción: registros que lee, registros que escribe, slot y efectos.
    // Esta información se guarda en c->metadatos.
    // La función está implementada en vliw_metadatos.c.

    c->metadatos=n?calloc(n,sizeof *c->metadatos):NULL;
    if(n && !c->metadatos){estado=informar(d,VLIW_CAD_MEMORIA,VLIW_CAD_META,0,VAC,"sin memoria para metadatos");goto fin;}
    for(size_t i=0;i<n;++i){VliwMetadatosDiagnostico md;
        // Metadatos: vliw_metadatos_extraer() está en vliw_metadatos.c; extrae registros y efectos usando S2.
        SimDecodeEstado se=vliw_metadatos_extraer(e->flujo->operaciones[i].palabra,
            (unsigned)e->flujo->operaciones[i].slot,&c->metadatos[i],&md);
        if(se){estado=informar(d,VLIW_CAD_ISA,VLIW_CAD_META,se,i,md.mensaje);goto fin;}}


    // ETAPA 2: CFG (CONTROL FLOW GRAPH)
    // Construye los bloques básicos y determina a cuáles puede saltar cada bloque.
    // Recibe el flujo de instrucciones y guarda el resultado en c->cfg.
    // La función está implementada en vliw_cfg_fisico.c.

    // CFG: vliw_cfg_construir() está en vliw_cfg_fisico.c; identifica bloques básicos y sus sucesores.
    VliwCfgDiagnostico cd;VliwCfgEstado ce=vliw_cfg_construir(e->flujo,&c->cfg,&cd);
    if(ce){estado=informar(d,ce==VLIW_CFG_MEMORIA?VLIW_CAD_MEMORIA:ce==VLIW_CFG_CAPACIDAD?
        VLIW_CAD_CAPACIDAD:VLIW_CAD_CONTROL,VLIW_CAD_CFG,ce,cd.indice,cd.mensaje);goto fin;}


    // ETAPA 3: DAG (DIRECTED ACYCLIC GRAPH)
    // Analiza las dependencias entre instrucciones usando el CFG y los metadatos.
    // Permite saber qué instrucciones deben ejecutarse antes que otras.
    // El resultado se guarda en c->dag.
    // La función está implementada en vliw_dag_fisico.c.

    VliwDagEntrada de={e->flujo,c->cfg,c->metadatos,e->efectos_adicionales,n,e->generacion,
        e->garantias.pendientes,e->efectos_acreditados,e->abi_acreditado};
    // DAG: vliw_dag_construir() está en vliw_dag_fisico.c; detecta dependencias entre operaciones.
    VliwDagDiagnostico dd;VliwDagEstado ds=vliw_dag_construir(&de,&c->dag,&dd);
    if(ds){estado=informar(d,ds==VLIW_DAG_MEMORIA?VLIW_CAD_MEMORIA:ds==VLIW_DAG_CAPACIDAD?
        VLIW_CAD_CAPACIDAD:VLIW_CAD_DEPENDENCIAS,VLIW_CAD_DAG,ds,dd.indice,dd.mensaje);goto fin;}


    // ETAPA 4: PROYECCIÓN FÍSICA
    // Prepara las instrucciones y sus dependencias para que el calendarizador pueda utilizarlas.
    // Utiliza el DAG, los textos y las garantías de entrada.
    // Guarda el resultado en c->proyeccion.
    // La función está implementada en vliw_proyeccion_fisica.c.

    VliwProyeccionEntrada pe={de,c->dag,e->textos,e->cantidad_textos,e->garantias};
    // Proyección: vliw_proyeccion_construir() está en vliw_proyeccion_fisica.c; prepara datos propios para S7.
    VliwProyeccionDiagnostico pd;VliwProyeccionEstado ps=vliw_proyeccion_construir(&pe,&c->proyeccion,&pd);
    if(ps){if(d){d->fase_proyeccion=pd.fase;d->estado_interno=pd.estado_original;d->dependencia=pd.dependencia;d->bloque=pd.bloque;d->destino=pd.destino;}
        estado=informar(d,ps==VLIW_PROY_MEMORIA?VLIW_CAD_MEMORIA:ps==VLIW_PROY_IO?VLIW_CAD_IO:
            ps==VLIW_PROY_CAPACIDAD?VLIW_CAD_CAPACIDAD:VLIW_CAD_METADATOS,
            VLIW_CAD_PROYECCION,ps,pd.instruccion,pd.mensaje);goto fin;}


    // ETAPA 5: VALIDACIÓN DE INTEGRACIÓN
    // Obtiene las instrucciones y los destinos de los saltos desde la proyección.
    // Comprueba que esa información sea coherente antes de calendarizar.
    // La función está implementada en vliw_integracion.c.

    VliwIntegracionDiagnostico id;VliwEntrada *in=&c->proyeccion->entrada;VliwDestinos *dest=&c->proyeccion->destinos;
    // Integración: vliw_integracion_validar() está en vliw_integracion.c; revisa consistencia antes de S7.
    VliwIntegracionEstado is=vliw_integracion_validar(in,dest,&id);
    if(is){if(d){d->dependencia=id.dependencia;d->bloque=id.bloque;d->destino=id.destino;}
        estado=informar(d,is==VLIW_INT_MEMORIA?VLIW_CAD_MEMORIA:is==VLIW_INT_IO?VLIW_CAD_IO:
            VLIW_CAD_METADATOS,VLIW_CAD_VALIDACION,is,id.instruccion,id.mensaje);goto fin;}


    // ETAPA 6: CALENDARIZACIÓN VLIW (S7)
    // Organiza las instrucciones en bundles de 4 slots: ALU, LSU, BRU y CRIPTO.
    // Respeta las dependencias y coloca NOP cuando es necesario.
    // El plan resultante se guarda en c->plan.
    // La función está implementada en vliw_scheduler.c.

    // Calendarizador S7: vliw_planificar() está en vliw_scheduler.c; decide el bundle y slot de cada operación.
    VliwDiagnostico sd;VliwEstado ss=vliw_planificar(in,&c->plan,&sd);
    if(ss){if(d)d->dependencia=sd.dependencia;
        estado=informar(d,ss==VLIW_MEMORIA?VLIW_CAD_MEMORIA:ss==VLIW_CAPACIDAD?VLIW_CAD_CAPACIDAD:
            VLIW_CAD_PLAN_INVALIDO,VLIW_CAD_PLAN,ss,sd.instruccion,sd.mensaje);goto fin;}


    // ETAPA 7: VERIFICACIÓN DEL CALENDARIZADOR
    // Comprueba que los bundles respeten los slots y las dependencias, incluida la separación N+3.
    // Si encuentra un problema, rechaza el plan.

    // Verificador S7: vliw_verificar() está en vliw_scheduler.c; comprueba dependencias, slots y NOP.
    ss=vliw_verificar(in,c->plan,&sd);
    if(ss){if(d)d->dependencia=sd.dependencia;
        estado=informar(d,ss==VLIW_MEMORIA?VLIW_CAD_MEMORIA:VLIW_CAD_PLAN_INVALIDO,
            VLIW_CAD_VERIFICACION,ss,sd.instruccion,sd.mensaje);goto fin;}


    // ETAPA 8: GENERACIÓN DEL ENSAMBLADOR (S8)
    // Convierte el plan de bundles en ensamblador de nuestra ISA.
    // También ajusta los destinos de los saltos directos.
    // El ensamblador se guarda en c->asm_texto.
    // La función está implementada en vliw_emisor.c.

    // Emisor S8: vliw_emitir() está en vliw_emisor.c; escribe los bundles como ensamblador.
    VliwEmisorDiagnostico ed;VliwEmisorEstado es=vliw_emitir(in,c->plan,dest,&c->asm_texto,&ed);
    if(es){if(d){d->bloque=ed.bloque;d->estado_interno=ed.estado_plan;}
        estado=informar(d,es==VLIW_EM_MEMORIA?VLIW_CAD_MEMORIA:es==VLIW_EM_IO?VLIW_CAD_IO:
            es==VLIW_EM_RANGO?VLIW_CAD_CAPACIDAD:VLIW_CAD_EMISOR,
            VLIW_CAD_EMISION,es,ed.instruccion,ed.mensaje);goto fin;}


    // ETAPA 9: CODIFICACIÓN BINARIA
    // Llama a codificar(), que utiliza cb_ensamblar() para convertir el ensamblador a binario.
    // El código binario resultante se guarda en c->codigo.

    // Codificación: codificar() llama a CB en codificacion_binaria.c y guarda el binario en c->codigo.
    estado=codificar(c,d);if(estado)goto fin;


    // ETAPA 10: VERIFICACIÓN DEL BINARIO
    // Llama a contrastar() para comparar el binario generado con las instrucciones originales.
    // Utiliza S2 para revisar las operaciones, los NOP y los destinos de los saltos.

    // Comprobación: contrastar() usa S2 (sim_decode.c) para revisar el binario generado.
    estado=contrastar(e,c,d);if(estado)goto fin;


    // ETAPA 11: ESTADÍSTICAS
    // Calcula cuántos bundles se generaron y cómo se utilizaron sus slots.
    // No mide ciclos reales porque todavía no se ha ejecutado el programa.

    // Estadísticas: vliw_estadisticas() está en vliw_visualizador.c; mide ocupación estática del plan.
    VliwVisualDiagnostico vd;VliwVisualEstado vs=vliw_estadisticas(in,c->plan,&c->estadisticas,&vd);
    if(vs){if(d)d->dependencia=vd.dependencia;
        estado=informar(d,vs==VLIW_VIS_MEMORIA?VLIW_CAD_MEMORIA:VLIW_CAD_PLAN_INVALIDO,
            VLIW_CAD_ESTADISTICAS,vs,vd.instruccion,vd.mensaje);goto fin;}


    // ETAPA 12: VISUALIZACIÓN
    // Genera un JSON que muestra cómo quedaron organizadas las instrucciones en bundles.
    // Guarda ese JSON en c->visualizacion_json.

    // Visualización: vliw_visualizar() está en vliw_visualizador.c; genera el JSON de los bundles.
    vs=vliw_visualizar(in,c->plan,dest,VLIW_VIS_JSON,NULL,&c->visualizacion_json,&vd);
    if(vs){if(d)d->dependencia=vd.dependencia;
        estado=informar(d,vs==VLIW_VIS_MEMORIA?VLIW_CAD_MEMORIA:VLIW_CAD_PLAN_INVALIDO,
            VLIW_CAD_VISUALIZACION,vs,vd.instruccion,vd.mensaje);goto fin;}


    // RESULTADO FINAL
    // Si todas las etapas terminaron correctamente, entrega la cadena completa.
    // *salida recibe c, que contiene el plan, el ensamblador, el binario y las estadísticas.

    // Entrega c al llamador; en caso de error salta a fin para liberar todos los resultados parciales.
    *salida=c;return informar(d,VLIW_CAD_OK,VLIW_CAD_VISUALIZACION,0,VAC,"cadena física completa; semántica externa no acreditada por el consumidor");


// MANEJO DE ERRORES
// Si alguna etapa falla, llega aquí mediante goto fin.
// Libera la memoria reservada y devuelve el tipo de error.
fin:
    vliw_cadena_liberar(&c);return estado;
}