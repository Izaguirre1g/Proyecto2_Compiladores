/* Compara estados públicos de dos programas ejecutados mediante M2 y métricas M1. */
#ifndef SIM_COMPARADOR_H
#define SIM_COMPARADOR_H
#include "sim_ejecucion.h"
typedef struct SimComparador SimComparador;
typedef enum { SIM_COMPARADOR_OK=0, SIM_COMPARADOR_ARGUMENTO,
    SIM_COMPARADOR_MEMORIA, SIM_COMPARADOR_GENERACION } SimComparadorCodigo;
typedef enum { SIM_COMPARADOR_EQUIVALENTE=0, SIM_COMPARADOR_DIFERENTE,
    SIM_COMPARADOR_INCONCLUSO, SIM_COMPARADOR_ERROR } SimComparadorResultado;
typedef enum { SIM_COMPARADOR_SIN_MOTIVO=0, SIM_COMPARADOR_INICIALES,
    SIM_COMPARADOR_NO_TERMINADO, SIM_COMPARADOR_CARGA,
    SIM_COMPARADOR_EJECUCION, SIM_COMPARADOR_OBSERVACION,
    SIM_COMPARADOR_CONSULTA } SimComparadorMotivo;
typedef struct { const char *ruta; SimEjecucionFormato formato; } SimComparadorPrograma;
typedef struct {
    SimComparadorPrograma referencia, candidato;
    SimModoBusqueda modo;
    uint64_t presupuesto;
    /* Máximo TOTAL de llamadas a reintentar por programa (0..16). No repite
     * flancos: solo resuelve observaciones M1 que M2 conserva pendientes. */
    unsigned reintentos;
} SimComparadorOpciones;
typedef struct {
    SimComparadorResultado resultado;
    SimComparadorMotivo motivo;
    bool creadas[2], ejecutadas[2], resumen_disponible[2];
    bool iniciales_compatibles, comparacion_realizada, metricas_disponibles;
    SimEjecucionEstado estados[2];
    SimMetricasResumen resumenes[2];
    SimMetricasComparacion metricas;
    /* -1 si no hubo error de una ejecución; 0 referencia, 1 candidato. */
    int lado_error;
    SimEjecucionCodigo error_ejecucion;
    SimMetricasEstado error_metricas;
    size_t bytes_iniciales_distintos, primer_byte_inicial;
    unsigned registros_distintos, primer_registro;
    uint32_t valores_registro[2];
    size_t bytes_distintos, primer_byte;
    uint8_t valores_byte[2];
    bool crypto_distinto, cantidad_traps_distinta, vector_distinto;
    size_t primer_vector;
    uint32_t valores_vector[2];
} SimComparadorResumen;

/* Crea dos sesiones propias y verifica el reset público y toda DMEM inicial
 * antes de ejecutar. El presupuesto se aplica por separado a cada programa.
 * Ambas deben terminar para comparar 32 GPR, toda DMEM, AUTH/INIT y traps.
 * PC/ciclos/layout no son criterios funcionales. Las diferencias de métricas
 * se delegan a M1, incluso si la ejecución limitada resulta INCONCLUSA.
 * salida comienza en NULL. Solo se publica un resultado íntegro, dueño de
 * rutas, snapshots e historial. CARGA/ejecución producen un resultado ERROR;
 * argumentos/reservas de B1 fallidos devuelven código sin publicar salida.
 * diagnosticos es prestado y admite NULL. No se modifica ningún componente. */
SimComparadorCodigo sim_comparador_ejecutar(const SimComparadorOpciones *,
    SimComparador **salida, FILE *diagnosticos);
/* Copias propias; no entregan acceso a sesiones ya destruidas ni a la bóveda.
 * Solo se ofrece contrato estricto: otro renombramiento requiere acuerdos
 * futuros, no se ignoran temporales ni registros de enlace arbitrariamente. */
SimComparadorCodigo sim_comparador_resumen(const SimComparador *, SimComparadorResumen *);
SimComparadorCodigo sim_comparador_trap(const SimComparador *, unsigned lado,
    size_t indice, SimMetricaTrap *);
/* Reporte B1 nuevo TEXTO/JSON v1; no cambia ni reproduce el JSON completo M1.
 * Salida inicialmente NULL, publicación transaccional. Liberar la cadena con
 * esta API, independientemente de la vida del resultado. */
SimComparadorCodigo sim_comparador_exportar(const SimComparador *, SimMetricasFormato, char **);
void sim_comparador_liberar_reporte(char **);
void sim_comparador_liberar(SimComparador **);
#endif
