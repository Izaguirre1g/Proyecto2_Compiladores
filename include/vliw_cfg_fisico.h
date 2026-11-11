/* Construye CFG físicos desde palabras ISA y referencias lógicas explícitas.
 * Reutiliza S2 y los tipos S7/S8-A; posee bloques/mapa/destinos, sin DAG,
 * productor BAL, asignación ABI, direcciones finales ni calendarización. */
#ifndef VLIW_CFG_FISICO_H
#define VLIW_CFG_FISICO_H
#include "vliw_metadatos.h"
#include "vliw_emisor.h"

/* ID de operación independiente de su posición. La forma declarada se
 * contrasta con S2; control es intención externa, no una deducción de x1. */
typedef struct {
    size_t id;
    uint32_t palabra;
    VliwSlot slot;
    SimSalto salto;
    VliwControl control;
} VliwCfgOperacion;
/* Las funciones particionan todo el flujo. IDs de funciones y símbolos son
 * inequívocos, distintos de VLIW_VACIO, y pertenecen a espacios separados. */
typedef struct { size_t id, inicio, fin; } VliwCfgFuncion;
typedef struct { size_t id, funcion, posicion; } VliwCfgSimbolo;
/* Intervalos que no admiten entradas internas: no se heredan de IR. */
typedef struct { size_t inicio, fin; } VliwCfgRegion;
typedef struct {
    size_t operacion;              /* Posición, no ID. Una referencia por BRU. */
    VliwDestinoTipo tipo;
    size_t tomado;                 /* ID de símbolo directo, VAC si dinámico. */
    size_t continuacion;           /* Posición i+1 para condición/call; otra: VAC. */
    const size_t *posibles;        /* IDs de símbolos acreditados; solo dinámicos. */
    size_t cantidad_posibles;
    bool completos;               /* Declaración externa de cobertura dinámica. */
} VliwCfgTransferencia;
typedef struct {
    const VliwCfgOperacion *operaciones; size_t cantidad_operaciones;
    const VliwCfgFuncion *funciones; size_t cantidad_funciones;
    const VliwCfgSimbolo *simbolos; size_t cantidad_simbolos;
    const VliwCfgTransferencia *transferencias; size_t cantidad_transferencias;
    const size_t *fronteras; size_t cantidad_fronteras;
    const VliwCfgRegion *regiones; size_t cantidad_regiones;
    uint64_t generacion, generacion_referencias;
    /* Cualquier obligación CFG no resuelta impide construir. La afirmación
     * de fronteras incluye que regiones/fronteras adicionales son suficientes.
     * No afirma ABI, efectos, DAG o reubicaciones completos. */
    unsigned pendientes;
    bool fronteras_acreditadas;
} VliwCfgEntrada;
typedef struct {
    uint64_t generacion;
    size_t cantidad_operaciones, cantidad_bloques;
    VliwBloque *bloques;
    size_t *bloque_por_operacion;
    VliwDestino *destinos; size_t cantidad_destinos;
    bool destinos_dinamicos_acreditados;
} VliwCfg;
typedef enum {
    VLIW_CFG_OK=0, VLIW_CFG_ARGUMENTO, VLIW_CFG_ESTRUCTURA,
    VLIW_CFG_IDENTIDAD, VLIW_CFG_ISA, VLIW_CFG_CONTROL,
    VLIW_CFG_DESTINO, VLIW_CFG_FRONTERA, VLIW_CFG_INCOMPLETO,
    VLIW_CFG_CAPACIDAD, VLIW_CFG_MEMORIA
} VliwCfgEstado;
typedef struct {
    VliwCfgEstado estado;
    size_t indice;                 /* Posición en la tabla indicada por mensaje. */
    const char *mensaje;           /* Literal estático, no se libera. */
} VliwCfgDiagnostico;
/* Entradas prestadas/accesibles e inmutables; cantidades describen memoria
 * real; salida y diagnóstico separados de la entrada. Exige salida inicial NULL; conserva *salida
 * ante cualquier error. Generaciones iguales no autentican al productor.
 * Vacío exige todas las tablas vacías. Solo OK publica un resultado propio;
 * pendientes de ABI/DAG/reubicación siguen externos incluso con éxito. */
VliwCfgEstado vliw_cfg_construir(const VliwCfgEntrada *entrada,
    VliwCfg **salida, VliwCfgDiagnostico *diagnostico);
/* Libera resultado propio (incluidos sucesores), admite NULL/vacío y deja
 * NULL. El cliente no debe alterar punteros/cantidades del resultado. */
void vliw_cfg_liberar(VliwCfg **cfg);
#endif
