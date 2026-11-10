/* Sesión independiente S1–S6/M1: recursos estables y observación por flanco. */
#ifndef SIM_EJECUCION_H
#define SIM_EJECUCION_H
#include "sim_metricas.h"
/* El cliente opera mediante esta API: no puede alterar el pipeline ni M1 y
 * dejar un flanco sin observar. La sesión mantiene vivos ambos componentes. */
typedef struct SimEjecucion SimEjecucion;
typedef enum { SIM_EJECUCION_MEM = 0, SIM_EJECUCION_BALB } SimEjecucionFormato;
typedef enum {
    SIM_EJECUCION_OK = 0, SIM_EJECUCION_ARGUMENTO, SIM_EJECUCION_CARGA,
    SIM_EJECUCION_MEMORIA, SIM_EJECUCION_PIPELINE, SIM_EJECUCION_METRICAS,
    SIM_EJECUCION_PENDIENTE
} SimEjecucionCodigo;
/* codigo describe la coordinación; pipeline distingue pausa y terminación.
 * pendiente permite reobservar por falta de memoria; fatal impide continuar
 * la época con un error M1 que no admite ese reintento. */
typedef struct {
    SimEjecucionCodigo codigo;
    SimPipelineEstado pipeline;
    SimMetricasEstado metricas;
    bool pendiente, fatal;
} SimEjecucionEstado;
/* Carga ruta como MEM o BALB mediante S1. FINITA drena al terminar el código;
 * FISICA conserva la búsqueda física de S6, incluidos sus huecos y aliasing.
 * salida debe comenzar en NULL para no reemplazar una sesión del cliente.
 * Solo se publica después de cargar, inicializar S6, configurar e iniciar M1.
 * La sesión adquiere esos recursos; un fallo libera lo adquirido y conserva
 * la salida. diagnosticos es prestado, admite NULL y recibe las causas de
 * carga/inicialización sin cerrarse ni conservarse dentro de la sesión. */
SimEjecucionCodigo sim_ejecucion_crear(const char *ruta, SimEjecucionFormato,
    SimModoBusqueda, SimEjecucion **salida, FILE *diagnosticos);
/* Intenta un flanco y observa con M1 todo avance aceptado, incluso el último
 * que devuelve TERMINADO. Un rechazo del pipeline no agrega ciclos. LIMITE
 * permite reanudar; TERMINADO se conserva hasta reset. */
SimEjecucionCodigo sim_ejecucion_avanzar(SimEjecucion *);
/* presupuesto limita intentos adicionales desde el estado actual. Se detiene
 * antes ante terminación/error; si lo agota publica LIMITE sin otro flanco.
 * Cero solo aplica esa pausa administrativa. FISICA también queda acotada. */
SimEjecucionCodigo sim_ejecucion_ejecutar(SimEjecucion *, uint64_t presupuesto);
/* Copia el estado de coordinación al destino, sin avanzar ni consultar de
 * nuevo M1. OK de una operación no implica que el pipeline haya terminado. */
SimEjecucionCodigo sim_ejecucion_estado(const SimEjecucion *, SimEjecucionEstado *);
/* Si S6 avanzó pero M1 devolvió MEMORIA, falta registrar ese mismo flanco:
 * avanzar, ejecutar y reset quedan bloqueados hasta resolver la observación.
 * Reintentar no ejecuta instrucciones ni repite escrituras; sin pendiente es
 * inocuo. Otra falla M1 se marca fatal: exige intentar reset o liberar, no
 * reobservar como si fuera falta de memoria. */
SimEjecucionCodigo sim_ejecucion_reintentar(SimEjecucion *);
/* Aplica reset S6 y comienza otra época M1 sin contar un ciclo de ejecución.
 * El reset simple conserva DMEM, incluido un store pendiente en EX que S6
 * pueda publicar al reiniciar. restaurar=true recarga después la DMEM de la
 * imagen, para repetir desde sus datos originales. Se reemplaza el historial:
 * copiar resultados previos antes; el snapshot por valor sigue siendo propio.
 * Una observación pendiente debe resolverse antes de permitir este reset. */
SimEjecucionCodigo sim_ejecucion_reiniciar(SimEjecucion *, bool restaurar);
/* Consultas delegadas a M1, bloqueadas si falta observar un flanco o la época
 * está en error fatal. resultados y trap copian valores al destino del cliente;
 * esos datos siguen siendo válidos tras reset o liberación de la sesión.
 * exportar exige una salida inicialmente NULL y entrega una cadena propia:
 * liberarla con sim_metricas_liberar_reporte. Se conserva el JSON v1 de M1 y
 * se comparan snapshots con sim_metricas_comparar, sin contadores alternativos. */
SimMetricasEstado sim_ejecucion_resultados(const SimEjecucion *, SimMetricasResumen *);
/*
 * Copia un trap de la sesión mediante M1, propagando argumentos inválidos u observaciones
 * pendientes.
 */
SimMetricasEstado sim_ejecucion_trap(const SimEjecucion *, size_t, SimMetricaTrap *);
/*
 * Exporta las métricas de la sesión en el formato solicitado; exige una salida inicialmente
 * NULL y deja su liberación al cliente.
 */
SimMetricasEstado sim_ejecucion_exportar(const SimEjecucion *, SimMetricasFormato, char **);
/* Copia los 65536 bytes de DMEM pública en un destino del cliente de ese
 * tamaño exacto. No presta acceso al almacenamiento interno ni a la bóveda.
 * Puede reflejar el último flanco real aunque su observación M1 esté pendiente;
 * leer memoria no publica por ello un resumen de métricas incompleto. */
SimEjecucionCodigo sim_ejecucion_memoria(const SimEjecucion *, uint8_t *, size_t);
/* Libera M1, simulador e imagen y deja NULL el puntero recibido; admite NULL
 * y repetición a través de ese puntero. Los demás alias quedan inválidos, no
 * se anulan automáticamente. Una sesión compartida requiere sincronización
 * externa; las cadenas exportadas siguen siendo responsabilidad del cliente. */
void sim_ejecucion_liberar(SimEjecucion **);
#endif
