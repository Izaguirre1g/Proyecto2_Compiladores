/* Sesión independiente S1–S6/M1: recursos estables y observación por flanco. */
#ifndef SIM_EJECUCION_H
#define SIM_EJECUCION_H
#include "sim_metricas.h"
typedef struct SimEjecucion SimEjecucion;
typedef enum { SIM_EJECUCION_MEM = 0, SIM_EJECUCION_BALB } SimEjecucionFormato;
typedef enum {
    SIM_EJECUCION_OK = 0, SIM_EJECUCION_ARGUMENTO, SIM_EJECUCION_CARGA,
    SIM_EJECUCION_MEMORIA, SIM_EJECUCION_PIPELINE, SIM_EJECUCION_METRICAS,
    SIM_EJECUCION_PENDIENTE
} SimEjecucionCodigo;
typedef struct {
    SimEjecucionCodigo codigo;
    SimPipelineEstado pipeline;
    SimMetricasEstado metricas;
    bool pendiente, fatal;
} SimEjecucionEstado;
/* Salida inicialmente NULL; publicación transaccional. La sesión posee imagen,
 * simulador y recolector hasta liberar. diagnosticos es prestado, admite NULL
 * y nunca se cierra. La creación informa errores de S1 por ese canal. */
SimEjecucionCodigo sim_ejecucion_crear(const char *ruta, SimEjecucionFormato,
    SimModoBusqueda, SimEjecucion **salida, FILE *diagnosticos);
/* Un intento por avanzar; ejecutar recibe un presupuesto ADICIONAL (incluso
 * cero) y conserva LIMITE/TERMINADO de S6. Consultar estado para distinguirlos.
 * FISICA no tiene terminación implícita: siempre exige presupuesto explícito. */
SimEjecucionCodigo sim_ejecucion_avanzar(SimEjecucion *);
SimEjecucionCodigo sim_ejecucion_ejecutar(SimEjecucion *, uint64_t presupuesto);
SimEjecucionCodigo sim_ejecucion_estado(const SimEjecucion *, SimEjecucionEstado *);
/* MEMORIA de M1 conserva un flanco pendiente y bloquea avance/reset hasta
 * reintentar. Esta operación solo observa: nunca repite efectos de CPU.
 * Otra inconsistencia M1 queda fatal hasta un reset explícito. */
SimEjecucionCodigo sim_ejecucion_reintentar(SimEjecucion *);
/* Reset S6 puede publicar un store pendiente en EX. restaurar=true recarga
 * DMEM DESPUÉS del reset. Ambos abren una época sin flanco de ejecución.
 * Copiar resultados previos antes: su snapshot por valor sigue siendo propio. */
SimEjecucionCodigo sim_ejecucion_reiniciar(SimEjecucion *, bool restaurar);
/* Delegaciones M1, sin contadores ni esquema alternativos. Conservan sus
 * contratos transaccionales. Comparar snapshots y liberar reportes con M1.
 * Una observación pendiente impide publicar resultados/historial/reportes. */
SimMetricasEstado sim_ejecucion_resultados(const SimEjecucion *, SimMetricasResumen *);
SimMetricasEstado sim_ejecucion_trap(const SimEjecucion *, size_t, SimMetricaTrap *);
SimMetricasEstado sim_ejecucion_exportar(const SimEjecucion *, SimMetricasFormato, char **);
/* Copia explícita de DMEM pública; tamaño exactamente 65536. No expone bóveda
 * ni estructura interna y no mantiene otra copia persistente. Incluso con
 * observación pendiente refleja la memoria real del flanco ya aceptado. */
SimEjecucionCodigo sim_ejecucion_memoria(const SimEjecucion *, uint8_t *, size_t);
/* Admite NULL/repetición; deja NULL. No conservar alias tras liberar ni usar
 * una misma sesión concurrentemente sin sincronización externa. */
void sim_ejecucion_liberar(SimEjecucion **);
#endif
