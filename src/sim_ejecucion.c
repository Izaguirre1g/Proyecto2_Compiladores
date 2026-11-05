/* Conserva recursos propios y entrega a M1 cada flanco aceptado de S6. */
#include "sim_ejecucion.h"
#include <stdlib.h>
#include <string.h>

struct SimEjecucion {
    SimImagen *imagen;
    SimPipeline sim;
    SimMetricas metricas;
    SimEjecucionEstado estado;
};

void sim_ejecucion_liberar(SimEjecucion **salida)
{
    if (!salida || !*salida) return;
    SimEjecucion *s = *salida;
    /* Primero termina el préstamo M1; después muere el simulador y al final
     * su imagen. La referencia local evita acceder a la sesión ya liberada. */
    SimImagen *imagen = s->imagen;
    sim_metricas_liberar(&s->metricas);
    free(s);
    *salida = NULL;
    sim_liberar_imagen(&imagen);
}

SimEjecucionCodigo sim_ejecucion_crear(const char *ruta, SimEjecucionFormato formato,
    SimModoBusqueda modo, SimEjecucion **salida, FILE *diag)
{
    if (!ruta || !*ruta || !salida || *salida ||
        (formato != SIM_EJECUCION_MEM && formato != SIM_EJECUCION_BALB) ||
        (modo != SIM_BUSQUEDA_FINITA && modo != SIM_BUSQUEDA_FISICA))
        return SIM_EJECUCION_ARGUMENTO;
    /* La reserva nunca se mueve. Las dos memorias existentes (imagen S1 y
     * memoria mutable S6) son necesarias para restaurar condiciones iniciales. */
    SimEjecucion *s = calloc(1, sizeof *s);
    if (!s) {
        if (diag) fputs("ejecucion: no se pudo reservar la sesión\n", diag);
        return SIM_EJECUCION_MEMORIA;
    }
    SimCargaEstado carga = (formato == SIM_EJECUCION_MEM ? sim_cargar_mem : sim_cargar_balb)
        (ruta, &s->imagen, diag);
    SimEjecucionCodigo codigo = SIM_EJECUCION_CARGA;
    if (carga != SIM_CARGA_OK) {
        if (carga == SIM_CARGA_MEMORIA) codigo = SIM_EJECUCION_MEMORIA;
        goto fallo;
    }
    codigo = SIM_EJECUCION_PIPELINE;
    if (sim_pipeline_iniciar(&s->sim, s->imagen) != SIM_PIPE_ACTIVO ||
        sim_pipeline_configurar_busqueda(&s->sim, modo) != SIM_PIPE_ACTIVO) goto fallo;
    codigo = SIM_EJECUCION_METRICAS;
    s->estado.metricas = sim_metricas_iniciar(&s->metricas, &s->sim);
    if (s->estado.metricas != SIM_METRICAS_OK) goto fallo;
    *salida = s;
    return SIM_EJECUCION_OK;
fallo:
    if (diag && codigo != SIM_EJECUCION_CARGA)
        fprintf(diag, "ejecucion: inicialización incompleta, código %d\n", (int)codigo);
    sim_ejecucion_liberar(&s);
    return codigo;
}

/* M1 publica atómicamente una observación. Solo falta de memoria permite
 * repetirla; otra inconsistencia no debe permitir perder el flanco pendiente. */
static SimEjecucionCodigo observar(SimEjecucion *s)
{
    s->estado.metricas = sim_metricas_observar(&s->metricas, &s->sim);
    s->estado.pendiente = s->estado.metricas == SIM_METRICAS_MEMORIA;
    s->estado.fatal = s->estado.metricas != SIM_METRICAS_OK && !s->estado.pendiente;
    s->estado.codigo = s->estado.pendiente ? SIM_EJECUCION_PENDIENTE :
        s->estado.fatal ? SIM_EJECUCION_METRICAS : SIM_EJECUCION_OK;
    return s->estado.codigo;
}

SimEjecucionCodigo sim_ejecucion_avanzar(SimEjecucion *s)
{
    if (!s) return SIM_EJECUCION_ARGUMENTO;
    if (s->estado.pendiente) return SIM_EJECUCION_PENDIENTE;
    if (s->estado.fatal) return SIM_EJECUCION_METRICAS;
    /* S6 usa aritmética unsigned al numerar el siguiente flanco. Evitar su
     * vuelta a cero conserva el enlace secuencial sin modificar el pipeline. */
    if (s->sim.ciclos == UINT64_MAX &&
        (s->sim.estado == SIM_PIPE_ACTIVO || s->sim.estado == SIM_PIPE_LIMITE)) {
        s->estado.metricas = SIM_METRICAS_DESBORDAMIENTO;
        s->estado.fatal = true;
        s->estado.codigo = SIM_EJECUCION_METRICAS;
        return s->estado.codigo;
    }
    uint64_t previo = s->sim.ciclos;
    s->estado.pipeline = sim_avanzar_ciclo(&s->sim);
    /* El último flanco puede devolver TERMINADO. Solo el cambio de ciclos
     * determina si hubo algo nuevo para observar, también antes de un error. */
    if (s->sim.ciclos != previo && observar(s) != SIM_EJECUCION_OK)
        return s->estado.codigo;
    s->estado.codigo = s->estado.pipeline > SIM_PIPE_LIMITE ?
        SIM_EJECUCION_PIPELINE : SIM_EJECUCION_OK;
    return s->estado.codigo;
}

SimEjecucionCodigo sim_ejecucion_ejecutar(SimEjecucion *s, uint64_t presupuesto)
{
    if (!s) return SIM_EJECUCION_ARGUMENTO;
    if (s->estado.pendiente) return SIM_EJECUCION_PENDIENTE;
    if (s->estado.fatal) return SIM_EJECUCION_METRICAS;
    /* No se calcula ciclos+presupuesto: incluso UINT64_MAX no desborda una
     * fecha límite. Cada intento sigue sujeto a las validaciones de S6. */
    for (uint64_t n = 0; n < presupuesto; ++n) {
        SimEjecucionCodigo c = sim_ejecucion_avanzar(s);
        if (c != SIM_EJECUCION_OK || s->sim.estado == SIM_PIPE_TERMINADO) return c;
    }
    /* Único uso del ejecutor S6: pausa administrativa sin flanco ni eventos. */
    s->estado.pipeline = sim_pipeline_ejecutar(&s->sim, 0);
    s->estado.codigo = s->estado.pipeline > SIM_PIPE_LIMITE ?
        SIM_EJECUCION_PIPELINE : SIM_EJECUCION_OK;
    return s->estado.codigo;
}

SimEjecucionCodigo sim_ejecucion_reintentar(SimEjecucion *s)
{
    if (!s) return SIM_EJECUCION_ARGUMENTO;
    if (s->estado.fatal) return SIM_EJECUCION_METRICAS;
    return s->estado.pendiente ? observar(s) : SIM_EJECUCION_OK;
}

SimEjecucionCodigo sim_ejecucion_reiniciar(SimEjecucion *s, bool restaurar)
{
    if (!s) return SIM_EJECUCION_ARGUMENTO;
    if (s->estado.pendiente) return SIM_EJECUCION_PENDIENTE;
    s->estado.pipeline = sim_pipeline_reiniciar(&s->sim);
    if (s->estado.pipeline != SIM_PIPE_ACTIVO) return SIM_EJECUCION_PIPELINE;
    if (restaurar) s->estado.pipeline = sim_pipeline_restaurar_memoria(&s->sim);
    s->estado.metricas = sim_metricas_iniciar(&s->metricas, &s->sim);
    s->estado.fatal = s->estado.metricas != SIM_METRICAS_OK;
    s->estado.codigo = s->estado.fatal ? SIM_EJECUCION_METRICAS : SIM_EJECUCION_OK;
    return s->estado.codigo;
}

SimEjecucionCodigo sim_ejecucion_estado(const SimEjecucion *s, SimEjecucionEstado *out)
{
    if (!s || !out) return SIM_EJECUCION_ARGUMENTO;
    *out = s->estado;
    return SIM_EJECUCION_OK;
}

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
    memcpy(bytes, s->sim.memoria.bytes, n);
    return SIM_EJECUCION_OK;
}
