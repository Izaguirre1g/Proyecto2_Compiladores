/* Proyecta operaciones físicas acreditadas a una entrada propia para S7.
 * Reutiliza CFG/DAG, I1-A1 y CB/S2; no genera ISA ni asigna registros.
 * Las garantías externas no certifican por sí solas semántica BAL o ABI. */
#ifndef VLIW_PROYECCION_FISICA_H
#define VLIW_PROYECCION_FISICA_H
#include "vliw_dag_fisico.h"
#include "vliw_integracion.h"

/* Declaraciones del productor sobre ESTA generación. Reubicaciones resueltas
 * incluye ausencia de direcciones materializadas obsoletas, también en ALU.
 * No se deduce completitud de que CFG/DAG o una decodificación hayan pasado. */
typedef struct {
    unsigned pendientes;
    bool completitud_declarada, reubicaciones_resueltas;
} VliwGarantiasFisicas;
typedef struct {
    VliwDagEntrada fisica;
    const VliwDag *dag;
    const char *const *textos;
    size_t cantidad_textos;
    VliwGarantiasFisicas garantias;
} VliwProyeccionEntrada;
/* Posee instrucciones, sus textos, aristas, bloques/sucesores y destinos.
 * Las vistas entrada/destinos apuntan exclusivamente a esos recursos propios.
 * No alterar punteros/cantidades antes de liberar; no depende de la entrada. */
typedef struct {
    uint64_t generacion;
    VliwEntrada entrada;
    VliwDestinos destinos;
} VliwProyeccion;
typedef enum {
    VLIW_PROY_OK=0, VLIW_PROY_ARGUMENTO, VLIW_PROY_INCOMPLETO,
    VLIW_PROY_REUBICACION, VLIW_PROY_DAG, VLIW_PROY_TEXTO,
    VLIW_PROY_INTEGRACION, VLIW_PROY_CAPACIDAD, VLIW_PROY_MEMORIA,
    VLIW_PROY_IO
} VliwProyeccionEstado;
typedef enum {
    VLIW_PROY_INICIO=0,VLIW_PROY_CONTRASTE_DAG,VLIW_PROY_COPIA,
    VLIW_PROY_VALIDACION,VLIW_PROY_CONTRASTE_TEXTO,VLIW_PROY_COMPLETA
} VliwProyeccionFase;
typedef struct {
    VliwProyeccionEstado estado;
    VliwProyeccionFase fase;
    int estado_original; /* Estado de DAG/I1-A1/CB según la fase indicada. */
    size_t instruccion, dependencia, bloque, destino;
    VliwIntegracionDiagnostico integracion;
    char mensaje[512]; /* Copia propia; no retiene mensajes temporales. */
} VliwProyeccionDiagnostico;
/* Entradas prestadas, accesibles e inmutables; salida inicialmente NULL y
 * diagnóstico separado. Revalida CFG/metadatos con DAG real y contrasta el DAG
 * recibido, copia almacenamiento, ejecuta I1-A1 y contrasta textos con CB/S2.
 * Solo OK publica; error libera todas las reservas y conserva salida/entradas.
 * Cantidades no prueban accesibilidad ni generaciones autentican contenido.
 * Cada texto es una operación ASCII de menos de160 bytes. El destino directo
 * es un marcador lógico que S8-A sustituye; ningún otro inmediato se descarta. */
VliwProyeccionEstado vliw_proyeccion_construir(const VliwProyeccionEntrada *entrada,
    VliwProyeccion **salida,VliwProyeccionDiagnostico *diagnostico);
/* Libera solo recursos propios, admite NULL y deja NULL; no libera entradas. */
void vliw_proyeccion_liberar(VliwProyeccion **proyeccion);
/* Compara campos S2 definidos de operaciones válidas. Ignora exclusivamente
 * el offset PC de BRU directa/condicional, pues cambia con el layout. El
 * consumidor debe verificar aparte su destino lógico. No reserva recursos. */
bool vliw_proyeccion_misma_operacion(const SimInstruccion *a,const SimInstruccion *b);
#endif
