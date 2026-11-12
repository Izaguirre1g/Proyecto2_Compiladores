/* Construye dependencias físicas S7 desde operaciones, metadatos y CFG vigentes.
 * No produce BAL ni acredita semántica, ABI o reubicaciones del compilador. */
#ifndef VLIW_DAG_FISICO_H
#define VLIW_DAG_FISICO_H
#include "vliw_cfg_fisico.h"

/* Todos los arreglos son prestados durante la llamada. cantidad_metadatos
 * corresponde tanto a metadatos como a efectos_adicionales (incluso ceros).
 * Las acreditaciones son obligaciones externas, no verificaciones de autoría.
 * pendientes describe obligaciones de ESTA entrada; no borra pendientes de S2. */
typedef struct {
    const VliwCfgEntrada *flujo;
    const VliwCfg *cfg;
    const VliwMetadatosParciales *metadatos;
    const unsigned *efectos_adicionales;
    size_t cantidad_metadatos;
    uint64_t generacion;
    unsigned pendientes;
    bool efectos_acreditados, abi_acreditado;
} VliwDagEntrada;

/* Resultado propio, independiente de la vida de las entradas; solo contiene
 * restricciones visibles. camino_critico mide separación, no bundles finales.
 * No constituye VliwEntrada ni autoriza metadatos_completos=true. */
typedef struct {
    uint64_t generacion;
    size_t cantidad_operaciones, cantidad_dependencias, camino_critico;
    VliwDependencia *dependencias;
} VliwDag;
enum { VLIW_DAG_MAX_ARISTAS = 65536 };
typedef enum {
    VLIW_DAG_OK=0, VLIW_DAG_ARGUMENTO, VLIW_DAG_CFG,
    VLIW_DAG_METADATOS, VLIW_DAG_INCOMPLETO,
    VLIW_DAG_CAPACIDAD, VLIW_DAG_MEMORIA
} VliwDagEstado;
typedef struct {
    VliwDagEstado estado;
    size_t indice; /* Operación; VAC si es un error global o de CFG. */
    const char *mensaje; /* Literal prestado, sin liberación. */
} VliwDagDiagnostico;

/* Exige salida inicialmente NULL y almacenamiento accesible real, separado
 * del diagnóstico. Solo OK publica. Cualquier rechazo conserva entradas y
 * salida; libera todas las reservas parciales. No valida punteros arbitrarios.
 * Reconstruye CFG para contrastar su estructura y reextrae metadatos con S2.
 * CFG no conserva palabras/IDs: el productor garantiza su correspondencia
 * histórica y debe renovar generación/análisis ante cualquier modificación. */
VliwDagEstado vliw_dag_construir(const VliwDagEntrada *entrada,
    VliwDag **salida, VliwDagDiagnostico *diagnostico);
/* Libera únicamente resultado y aristas propios; admite NULL y deja NULL.
 * El cliente no debe alterar punteros/cantidades antes de liberar. */
void vliw_dag_liberar(VliwDag **dag);
#endif
