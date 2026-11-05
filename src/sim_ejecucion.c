/* Conserva recursos propios y entrega a M1 cada flanco aceptado de S6. */
#include "sim_ejecucion.h"
#include <stdlib.h>
#include <string.h>

struct SimEjecucion {
    /* S6 toma prestada la imagen; M1 toma prestado este simulador. La sesión
     * posee ambos y no se mueve mientras esas referencias estén vigentes. */
    SimImagen *imagen;
    SimPipeline sim;
    SimMetricas metricas;
    /* Registra el resultado de coordinar S6/M1, sin duplicar sus contadores.
     * Permite distinguir una CPU pausada de una observación que falta guardar. */
    SimEjecucionEstado estado;
};

void sim_ejecucion_liberar(SimEjecucion **salida)
{
    if (!salida || !*salida) return;
    SimEjecucion *s = *salida;
    /* M1 debe soltar su referencia antes de destruir el simulador embebido.
     * Conservamos imagen fuera de la sesión para liberarla después de free(s),
     * sin leer memoria ya liberada. NULL permite repetir con el mismo puntero;
     * otros alias externos a la sesión dejan de ser válidos. */
    SimImagen *imagen = s->imagen;
    sim_metricas_liberar(&s->metricas);
    free(s);
    *salida = NULL;
    sim_liberar_imagen(&imagen);
}

SimEjecucionCodigo sim_ejecucion_crear(const char *ruta, SimEjecucionFormato formato,
    SimModoBusqueda modo, SimEjecucion **salida, FILE *diag)
{
    /* Rechazar una salida ocupada protege una sesión previa del cliente.
     * Formato y modo se validan antes de adquirir recursos o abrir el archivo. */
    if (!ruta || !*ruta || !salida || *salida ||
        (formato != SIM_EJECUCION_MEM && formato != SIM_EJECUCION_BALB) ||
        (modo != SIM_BUSQUEDA_FINITA && modo != SIM_BUSQUEDA_FISICA))
        return SIM_EJECUCION_ARGUMENTO;
    /* La reserva única mantiene estable &s->sim cuando M1 lo referencia.
     * calloc también deja vacíos los recursos que podría alcanzar la limpieza
     * parcial. La imagen S1 conserva los datos originales; la DMEM de S6 es la
     * copia mutable que cambia al ejecutar y puede restaurarse desde la imagen. */
    SimEjecucion *s = calloc(1, sizeof *s);
    if (!s) {
        if (diag) fputs("ejecucion: no se pudo reservar la sesión\n", diag);
        return SIM_EJECUCION_MEMORIA;
    }
    /* S1 selecciona y valida el formato completo. No se interpreta BALB como
     * instrucciones crudas y la imagen solo se recibe si terminó la carga. */
    SimCargaEstado carga = (formato == SIM_EJECUCION_MEM ? sim_cargar_mem : sim_cargar_balb)
        (ruta, &s->imagen, diag);
    SimEjecucionCodigo codigo = SIM_EJECUCION_CARGA;
    if (carga != SIM_CARGA_OK) {
        if (carga == SIM_CARGA_MEMORIA) codigo = SIM_EJECUCION_MEMORIA;
        goto fallo;
    }
    /* Primero S6 necesita una imagen viva; luego se fija la búsqueda antes
     * de iniciar M1, que vincula imagen, modo e instancia durante la época. */
    codigo = SIM_EJECUCION_PIPELINE;
    if (sim_pipeline_iniciar(&s->sim, s->imagen) != SIM_PIPE_ACTIVO ||
        sim_pipeline_configurar_busqueda(&s->sim, modo) != SIM_PIPE_ACTIVO) goto fallo;
    codigo = SIM_EJECUCION_METRICAS;
    s->estado.metricas = sim_metricas_iniciar(&s->metricas, &s->sim);
    if (s->estado.metricas != SIM_METRICAS_OK) goto fallo;
    /* Hasta aquí la sesión era privada de esta función. Publicarla al final
     * impide que el cliente reciba componentes parcialmente inicializados. */
    *salida = s;
    return SIM_EJECUCION_OK;
/* La misma destrucción admite recursos todavía vacíos: cada error conserva
 * su código y libera lo adquirido sin tocar la salida del cliente. */
fallo:
    if (diag && codigo != SIM_EJECUCION_CARGA)
        fprintf(diag, "ejecucion: inicialización incompleta, código %d\n", (int)codigo);
    sim_ejecucion_liberar(&s);
    return codigo;
}

/* S6 ya publicó el flanco: M1 solo registra sus eventos, no ejecuta la CPU.
 * Si su reserva de historial falla, la observación queda sin publicar y puede
 * repetirse completa. Otro error no ofrece esa garantía de recuperación. */
static SimEjecucionCodigo observar(SimEjecucion *s)
{
    s->estado.metricas = sim_metricas_observar(&s->metricas, &s->sim);
    /* pendiente conserva la obligación de registrar ese flanco antes de otro
     * avance o reset. fatal indica que repetir observar no es una recuperación
     * admitida; la época tampoco puede presentarse como un resultado completo. */
    s->estado.pendiente = s->estado.metricas == SIM_METRICAS_MEMORIA;
    s->estado.fatal = s->estado.metricas != SIM_METRICAS_OK && !s->estado.pendiente;
    /* El retorno separa falta recuperable de observación, falla M1 y éxito;
     * el estado del pipeline sigue describiendo lo que S6 ya ejecutó. */
    s->estado.codigo = s->estado.pendiente ? SIM_EJECUCION_PENDIENTE :
        s->estado.fatal ? SIM_EJECUCION_METRICAS : SIM_EJECUCION_OK;
    return s->estado.codigo;
}

SimEjecucionCodigo sim_ejecucion_avanzar(SimEjecucion *s)
{
    if (!s) return SIM_EJECUCION_ARGUMENTO;
    /* Avanzar aquí perdería el único evento aún no observado. Un error fatal
     * también bloquea la época, aunque el pipeline por sí solo pudiera seguir. */
    if (s->estado.pendiente) return SIM_EJECUCION_PENDIENTE;
    if (s->estado.fatal) return SIM_EJECUCION_METRICAS;
    /* S6 numera el próximo flanco con ciclos+1. Si un pipeline reanudable ya
     * alcanzó UINT64_MAX, no se permite la vuelta a cero que M1 interpretaría
     * como otra época. TERMINADO y errores conservan sus respuestas estables. */
    if (s->sim.ciclos == UINT64_MAX &&
        (s->sim.estado == SIM_PIPE_ACTIVO || s->sim.estado == SIM_PIPE_LIMITE)) {
        s->estado.metricas = SIM_METRICAS_DESBORDAMIENTO;
        s->estado.fatal = true;
        s->estado.codigo = SIM_EJECUCION_METRICAS;
        return s->estado.codigo;
    }
    uint64_t previo = s->sim.ciclos;
    s->estado.pipeline = sim_avanzar_ciclo(&s->sim);
    /* previo distingue un flanco aceptado de un intento rechazado por S6.
     * TERMINADO puede acompañar el último avance válido: filtrar solo ACTIVO
     * omitiría ese ciclo. Si observar falla, se sale sin ejecutar otro flanco. */
    if (s->sim.ciclos != previo && observar(s) != SIM_EJECUCION_OK)
        return s->estado.codigo;
    /* Un error administrativo de S6 informa el rechazo del intento, sin
     * inventar un trap ni sumar tiempo. OK también admite pausa o terminación. */
    s->estado.codigo = s->estado.pipeline > SIM_PIPE_LIMITE ?
        SIM_EJECUCION_PIPELINE : SIM_EJECUCION_OK;
    return s->estado.codigo;
}

SimEjecucionCodigo sim_ejecucion_ejecutar(SimEjecucion *s, uint64_t presupuesto)
{
    if (!s) return SIM_EJECUCION_ARGUMENTO;
    if (s->estado.pendiente) return SIM_EJECUCION_PENDIENTE;
    if (s->estado.fatal) return SIM_EJECUCION_METRICAS;
    /* El presupuesto cuenta intentos adicionales desde este estado, no un
     * ciclo final absoluto. Usar un índice local evita desbordar la suma
     * ciclos+presupuesto y permite observar cada avance antes del siguiente. */
    for (uint64_t n = 0; n < presupuesto; ++n) {
        SimEjecucionCodigo c = sim_ejecucion_avanzar(s);
        if (c != SIM_EJECUCION_OK || s->sim.estado == SIM_PIPE_TERMINADO) return c;
    }
    /* Agotar el recorrido solo debe publicar LIMITE. El presupuesto cero
     * del ejecutor S6 no vuelve a ejecutar instrucciones ni produce eventos;
     * también conserva TERMINADO o errores previos en la llamada de cero. */
    s->estado.pipeline = sim_pipeline_ejecutar(&s->sim, 0);
    s->estado.codigo = s->estado.pipeline > SIM_PIPE_LIMITE ?
        SIM_EJECUCION_PIPELINE : SIM_EJECUCION_OK;
    return s->estado.codigo;
}

SimEjecucionCodigo sim_ejecucion_reintentar(SimEjecucion *s)
{
    if (!s) return SIM_EJECUCION_ARGUMENTO;
    if (s->estado.fatal) return SIM_EJECUCION_METRICAS;
    /* Se recupera el registro M1 del flanco ya ejecutado, no su efecto en CPU.
     * Si vuelve a faltar memoria se conserva pendiente; sin pendiente no hay
     * trabajo que repetir. Un error fatal se rechazó antes de llegar aquí. */
    return s->estado.pendiente ? observar(s) : SIM_EJECUCION_OK;
}

SimEjecucionCodigo sim_ejecucion_reiniciar(SimEjecucion *s, bool restaurar)
{
    if (!s) return SIM_EJECUCION_ARGUMENTO;
    if (s->estado.pendiente) return SIM_EJECUCION_PENDIENTE;
    /* Reiniciar descarta el pipeline parcial o de excepción según S6, pero
     * puede publicar stores ya presentes en EX. No equivale a recargar DMEM. */
    s->estado.pipeline = sim_pipeline_reiniciar(&s->sim);
    if (s->estado.pipeline != SIM_PIPE_ACTIVO) return SIM_EJECUCION_PIPELINE;
    /* Restaurar después del reset elimina también esas escrituras residuales.
     * M1 debe empezar después de ambas operaciones: ni reset ni recarga son
     * ciclos/accesos de ejecución de la nueva época. Reemplaza su historial. */
    if (restaurar) s->estado.pipeline = sim_pipeline_restaurar_memoria(&s->sim);
    s->estado.metricas = sim_metricas_iniciar(&s->metricas, &s->sim);
    s->estado.fatal = s->estado.metricas != SIM_METRICAS_OK;
    s->estado.codigo = s->estado.fatal ? SIM_EJECUCION_METRICAS : SIM_EJECUCION_OK;
    return s->estado.codigo;
}

SimEjecucionCodigo sim_ejecucion_estado(const SimEjecucion *s, SimEjecucionEstado *out)
{
    if (!s || !out) return SIM_EJECUCION_ARGUMENTO;
    /* Entregar una copia evita prestar el estado de control interno; consultar
     * no reintenta observaciones ni cambia el reloj del procesador. */
    *out = s->estado;
    return SIM_EJECUCION_OK;
}

/* Resultados, historial y reportes comparten esta barrera: mientras falta
 * observar un flanco no deben parecer completos. Un fatal conserva la causa
 * M1; las tres consultas siguientes delegan sin reconstruir métricas. */
static SimMetricasEstado disponible(const SimEjecucion *s)
{
    if (!s) return SIM_METRICAS_ARGUMENTO;
    if (s->estado.pendiente) return SIM_METRICAS_SECUENCIA;
    return s->estado.fatal ? s->estado.metricas : SIM_METRICAS_OK;
}

SimMetricasEstado sim_ejecucion_resultados(const SimEjecucion *s, SimMetricasResumen *r)
{
    SimMetricasEstado e = disponible(s);
    return e == SIM_METRICAS_OK ? sim_metricas_resultados(&s->metricas, r) : e;
}

SimMetricasEstado sim_ejecucion_trap(const SimEjecucion *s, size_t i, SimMetricaTrap *t)
{
    SimMetricasEstado e = disponible(s);
    return e == SIM_METRICAS_OK ? sim_metricas_trap(&s->metricas, i, t) : e;
}

SimMetricasEstado sim_ejecucion_exportar(const SimEjecucion *s, SimMetricasFormato f, char **r)
{
    SimMetricasEstado e = disponible(s);
    return e == SIM_METRICAS_OK ? sim_metricas_exportar(&s->metricas, f, r) : e;
}

SimEjecucionCodigo sim_ejecucion_memoria(const SimEjecucion *s, uint8_t *bytes, size_t n)
{
    if (!s || !bytes || n != SIM_CARGADOR_DMEM_BYTES) return SIM_EJECUCION_ARGUMENTO;
    /* S6 ya publicó esta memoria aunque M1 siga pendiente. La copia permite
     * inspeccionar ese estado real sin prestar bytes internos ni claves de la
     * bóveda, y no obliga a guardar otra DMEM permanente en la sesión. */
    memcpy(bytes, s->sim.memoria.bytes, n);
    return SIM_EJECUCION_OK;
}
