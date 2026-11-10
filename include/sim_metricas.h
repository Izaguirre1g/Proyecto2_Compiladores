/* Recolecta eventos aceptados de S1–S6 y resultados arquitectónicos públicos.
 * No ejecuta instrucciones ni permite consultar secretos de la bóveda. */
#ifndef SIM_METRICAS_H
#define SIM_METRICAS_H
#include "sim_pipeline.h"

enum { SIM_METRICAS_OPERACIONES = SIM_OP_SETPWD + 1 };
typedef enum {
    SIM_METRICAS_OK = 0, SIM_METRICAS_SIN_CICLO, SIM_METRICAS_ARGUMENTO,
    SIM_METRICAS_SECUENCIA, SIM_METRICAS_EPOCA, SIM_METRICAS_INCONSISTENCIA,
    SIM_METRICAS_DECODIFICACION, SIM_METRICAS_MEMORIA,
    SIM_METRICAS_DESBORDAMIENTO, SIM_METRICAS_GENERACION
} SimMetricasEstado;
typedef enum { SIM_METRICAS_TEXTO = 0, SIM_METRICAS_JSON } SimMetricasFormato;
typedef struct { uint64_t ciclo; uint32_t pc, vector; } SimMetricaTrap;
typedef struct {
    bool valido;
    uint64_t epoca, ciclos, admitidos, completados, anulados, pendientes;
    uint64_t bundles_nop, bundles_utiles, instrucciones_utiles;
    uint64_t instrucciones_por_slot[4], operaciones_isa[SIM_METRICAS_OPERACIONES];
    /* Eventos EX, independientes de las instrucciones retiradas en WB. */
    uint64_t bru_ex, saltos_tomados, saltos_no_tomados, flush_salto;
    uint64_t anulados_salto, anulados_excepcion, busquedas_descartadas, traps;
    uint64_t lsu_lecturas_ex, lsu_escrituras_ex;
    uint64_t puerto_b_operaciones_ex, puerto_b_lecturas_ex, puerto_b_escrituras_ex;
    uint64_t conflictos_memoria, conflictos_registro;
    /* Cinco solicitudes habilitadas; un conflicto no elimina su observación.
     * Puertos 3/4 pertenecen a una sola instrucción CRIPTO. */
    uint64_t escrituras_por_puerto[5], descartes_x0_por_slot[4];
    unsigned pendientes_por_etapa[3]; /* IF/ID, ID/EX, EX/WB posteriores. */
    SimPipelineEstado estado;
    uint32_t pc, registros[32], estado_crypto, trap_count32, trap_pc;
    SimMetricaTrap ultimo_trap;
    uint64_t dmem_fnv1a64;
    double ipc, bundles_por_ciclo;
} SimMetricasResumen;
typedef struct {
    /* Almacenamiento propio: iniciar con {0}, no copiar superficialmente ni
     * modificar miembros. La referencia prestada debe permanecer viva y estable. */
    const SimPipeline *simulador;
    const SimImagen *imagen;
    SimModoBusqueda modo;
    SimMetricasResumen acumulado;
    SimMetricaTrap *historial;
    size_t cantidad_traps, capacidad_traps;
    bool iniciado;
} SimMetricas;
typedef struct { bool negativa; uint64_t magnitud; } SimMetricasDiferencia;
typedef struct {
    /* Candidato menos referencia, sin restas signed que puedan desbordarse. */
    SimMetricasDiferencia ciclos, bundles, instrucciones;
    double diferencia_ipc, cambio_porcentual_ciclos;
    bool porcentaje_definido;
} SimMetricasComparacion;

/* Solo inicia en ciclo cero, sin etapas ni contadores pendientes. Cada inicio
 * reemplaza la época anterior y libera su historial. Copiar resultados o
 * exportar antes del reset si deben conservarse. No toca el simulador. */
SimMetricasEstado sim_metricas_iniciar(SimMetricas *, const SimPipeline *);
/* Observar después de CADA flanco aceptado, antes de avanzar otro. Un duplicado,
 * un límite o error sin flanco devuelve SIN_CICLO sin sumar eventos. Un salto
 * de ciclos devuelve SECUENCIA. Reset exige iniciar una época nueva; no puede
 * reconstruirse una época omitida desde el último SimCiclo. MEMORIA permite
 * reintentar el mismo flanco antes de continuar. IMEM es prestada e inmutable. */
SimMetricasEstado sim_metricas_observar(SimMetricas *, const SimPipeline *);
/* Snapshot propio, sin secretos: exige que no falten ciclos por observar.
 * Calcula FNV-1a sobre los 65536 bytes DMEM actuales; no copia esa memoria.
 * Puede reflejar un estado LIMITE/error administrativo sin sumar un flanco. */
SimMetricasEstado sim_metricas_resultados(const SimMetricas *, SimMetricasResumen *);
SimMetricasEstado sim_metricas_trap(const SimMetricas *, size_t indice, SimMetricaTrap *);
/* Salida propia, inicialmente NULL; publicación atómica. Liberar con la función
 * específica. El reporte incluye historial de traps, nunca bóveda/contraseña. */
SimMetricasEstado sim_metricas_exportar(const SimMetricas *, SimMetricasFormato, char **);
void sim_metricas_liberar_reporte(char **);
void sim_metricas_liberar(SimMetricas *);
/* Solo compara métricas: NO certifica equivalencia funcional ni una optimización.
 * Comparar GPR y los 65536 bytes DMEM por separado. Base cero -> porcentaje no
 * definido y valor 0. Los resúmenes deben proceder de resultados válidos. */
SimMetricasEstado sim_metricas_comparar(const SimMetricasResumen *referencia,
    const SimMetricasResumen *candidato, SimMetricasComparacion *);
#endif
