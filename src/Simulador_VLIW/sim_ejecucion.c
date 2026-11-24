// MAPA M2: crea y controla una sesion S1 + S6 + M1.
// Es la interfaz que consumen el IDE y los comparadores para ejecutar programas.

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

/* Libera M1, simulador e imagen y deja NULL el puntero recibido; admite NULL
 * y repetición a través de ese puntero. Los demás alias quedan inválidos, no
 * se anulan automáticamente. Una sesión compartida requiere sincronización
 * externa; las cadenas exportadas siguen siendo responsabilidad del cliente. */
// LIBERACION M2: destruye recolector, pipeline y la imagen cargada.
// La instancia es dueña de sus recursos y deja NULL el puntero del cliente.
void sim_ejecucion_liberar(SimEjecucion **salida)
{
    if (!salida || !*salida) return;
    SimEjecucion *s = *salida;
    /* M1 debe soltar su referencia antes de destruir el simulador embebido.
     * Conservamos imagen fuera de la sesión para liberarla después de free(s),
     * sin leer memoria ya liberada. NULL permite repetir con el mismo puntero;
     * otros alias externos a la sesión dejan de ser válidos. */
    // El orden de liberacion mantiene válida la imagen hasta después de destruir M1.
    SimImagen *imagen = s->imagen;
    sim_metricas_liberar(&s->metricas);
    free(s);
    *salida = NULL;
    sim_liberar_imagen(&imagen);
}

/* Carga ruta como MEM o BALB mediante S1. FINITA drena al terminar el código;
 * FISICA conserva la búsqueda física de S6, incluidos sus huecos y aliasing.
 * salida debe comenzar en NULL para no reemplazar una sesión del cliente.
 * Solo se publica después de cargar, inicializar S6, configurar e iniciar M1.
 * La sesión adquiere esos recursos; un fallo libera lo adquirido y conserva
 * la salida. diagnosticos es prestado, admite NULL y recibe las causas de
 * carga/inicialización sin cerrarse ni conservarse dentro de la sesión. */
// ENTRADA M2: abre .mem/BALB, inicializa el pipeline y vincula las metricas.
// La sesion solo se entrega si todos los componentes quedaron preparados.
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
    // Se elige el cargador según si la entrada es MEM o BALB.
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
    // Despues de cargar, S6 prepara el pipeline y el modo de busqueda.
    if (sim_pipeline_iniciar(&s->sim, s->imagen) != SIM_PIPE_ACTIVO ||
        sim_pipeline_configurar_busqueda(&s->sim, modo) != SIM_PIPE_ACTIVO) goto fallo;
    codigo = SIM_EJECUCION_METRICAS;
    // M1 comienza con un resumen de ciclo cero, enlazado al mismo pipeline.
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
// M2: intenta registrar en M1 el último flanco que ejecuto S6.
// Si M1 falla por memoria, conserva ese flanco pendiente para reintentar sin volver a ejecutar CPU.
static SimEjecucionCodigo observar(SimEjecucion *s)
{
    // La recoleccion ocurre después del flanco; nunca antes ni una sola vez por lote.
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

/* Intenta un flanco y observa con M1 todo avance aceptado, incluso el último
 * que devuelve TERMINADO. Un rechazo del pipeline no agrega ciclos. LIMITE
 * permite reanudar; TERMINADO se conserva hasta reset. */
// M2: avanza UN flanco de S6 y luego lo registra mediante M1.
// Un flanco rechazado no incrementa los contadores dinamicos.
SimEjecucionCodigo sim_ejecucion_avanzar(SimEjecucion *s)
{
    if (!s) return SIM_EJECUCION_ARGUMENTO;
    /* Avanzar aquí perdería el único evento aún no observado. Un error fatal
     * también bloquea la época, aunque el pipeline por sí solo pudiera seguir. */
    // Si hay un flanco pendiente de medicion no se permite ejecutar otro flanco.
    if (s->estado.pendiente) return SIM_EJECUCION_PENDIENTE;
    // Un error no recuperable bloquea la publicación de metricas de esa época.
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
    // S6 ejecuta exactamente un flanco; M2 comprobara si debe registrarlo.
    s->estado.pipeline = sim_avanzar_ciclo(&s->sim);
    /* previo distingue un flanco aceptado de un intento rechazado por S6.
     * TERMINADO puede acompañar el último avance válido: filtrar solo ACTIVO
     * omitiría ese ciclo. Si observar falla, se sale sin ejecutar otro flanco. */
    // Comparar ciclos detecta si S6 acepto efectivamente un flanco.
    if (s->sim.ciclos != previo && observar(s) != SIM_EJECUCION_OK)
        return s->estado.codigo;
    /* Un error administrativo de S6 informa el rechazo del intento, sin
     * inventar un trap ni sumar tiempo. OK también admite pausa o terminación. */
    s->estado.codigo = s->estado.pipeline > SIM_PIPE_LIMITE ?
        SIM_EJECUCION_PIPELINE : SIM_EJECUCION_OK;
    return s->estado.codigo;
}

/* presupuesto limita intentos adicionales desde el estado actual. Se detiene
 * antes ante terminación/error; si lo agota publica LIMITE sin otro flanco.
 * Cero solo aplica esa pausa administrativa. FISICA también queda acotada. */
// M2: ejecuta un número limitado de flancos adicionales.
// El presupuesto es relativo y llegar al limite no significa que el programa termino.
SimEjecucionCodigo sim_ejecucion_ejecutar(SimEjecucion *s, uint64_t presupuesto)
{
    if (!s) return SIM_EJECUCION_ARGUMENTO;
    if (s->estado.pendiente) return SIM_EJECUCION_PENDIENTE;
    if (s->estado.fatal) return SIM_EJECUCION_METRICAS;
    /* El presupuesto cuenta intentos adicionales desde este estado, no un
     * ciclo final absoluto. Usar un índice local evita desbordar la suma
     * ciclos+presupuesto y permite observar cada avance antes del siguiente. */
    // M2 llama a su API de un ciclo para observar todas las transiciones, sin saltarse eventos.
    for (uint64_t n = 0; n < presupuesto; ++n) {
        SimEjecucionCodigo c = sim_ejecucion_avanzar(s);
        if (c != SIM_EJECUCION_OK || s->sim.estado == SIM_PIPE_TERMINADO) return c;
    }
    /* Agotar el recorrido solo debe publicar LIMITE. El presupuesto cero
     * del ejecutor S6 no vuelve a ejecutar instrucciones ni produce eventos;
     * también conserva TERMINADO o errores previos en la llamada de cero. */
    // Un presupuesto agotado publica LIMITE, no TERMINADO, y permite continuar.
    s->estado.pipeline = sim_pipeline_ejecutar(&s->sim, 0);
    s->estado.codigo = s->estado.pipeline > SIM_PIPE_LIMITE ?
        SIM_EJECUCION_PIPELINE : SIM_EJECUCION_OK;
    return s->estado.codigo;
}

/* Si S6 avanzó pero M1 devolvió MEMORIA, falta registrar ese mismo flanco:
 * avanzar, ejecutar y reset quedan bloqueados hasta resolver la observación.
 * Reintentar no ejecuta instrucciones ni repite escrituras; sin pendiente es
 * inocuo. Otra falla M1 se marca fatal: exige intentar reset o liberar, no
 * reobservar como si fuera falta de memoria. */
// M2: reintenta solamente la observacion pendiente de M1.
// No repite las escrituras, saltos ni demas efectos del flanco ya ejecutado.
// Solo la observacion de metricas vuelve a intentarse; nunca las instrucciones CPU.
SimEjecucionCodigo sim_ejecucion_reintentar(SimEjecucion *s)
{
    if (!s) return SIM_EJECUCION_ARGUMENTO;
    if (s->estado.fatal) return SIM_EJECUCION_METRICAS;
    /* Se recupera el registro M1 del flanco ya ejecutado, no su efecto en CPU.
     * Si vuelve a faltar memoria se conserva pendiente; sin pendiente no hay
     * trabajo que repetir. Un error fatal se rechazó antes de llegar aquí. */
    return s->estado.pendiente ? observar(s) : SIM_EJECUCION_OK;
}

/* Aplica reset S6 y comienza otra época M1 sin contar un ciclo de ejecución.
 * El reset simple conserva DMEM, incluido un store pendiente en EX que S6
 * pueda publicar al reiniciar. restaurar=true recarga después la DMEM de la
 * imagen, para repetir desde sus datos originales. Se reemplaza el historial:
 * copiar resultados previos antes; el snapshot por valor sigue siendo propio.
 * Una observación pendiente debe resolverse antes de permitir este reset. */
// M2: reinicia la sesion y comienza una nueva época de medicion.
// Permite elegir si se conserva DMEM o se restaura desde la imagen original.
// Cada reset abre una época nueva con contadores independientes.
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
    // En reset completo se restablece la DMEM original para repetir la prueba.
    if (restaurar) s->estado.pipeline = sim_pipeline_restaurar_memoria(&s->sim);
    s->estado.metricas = sim_metricas_iniciar(&s->metricas, &s->sim);
    s->estado.fatal = s->estado.metricas != SIM_METRICAS_OK;
    s->estado.codigo = s->estado.fatal ? SIM_EJECUCION_METRICAS : SIM_EJECUCION_OK;
    return s->estado.codigo;
}

/* Copia el estado de coordinación al destino, sin avanzar ni consultar de
 * nuevo M1. OK de una operación no implica que el pipeline haya terminado. */
// CONSULTA M2: entrega el estado combinado del pipeline y del recolector.
// Permite distinguir ACTIVO, TERMINADO, LIMITE, pendiente y error fatal.
SimEjecucionCodigo sim_ejecucion_estado(const SimEjecucion *s, SimEjecucionEstado *out)
{
    if (!s || !out) return SIM_EJECUCION_ARGUMENTO;
    /* Entregar una copia evita prestar el estado de control interno; consultar
     * no reintenta observaciones ni cambia el reloj del procesador. */
    // Se entrega una copia del estado coordinado, no acceso modificable al objeto.
    *out = s->estado;
    return SIM_EJECUCION_OK;
}

/* Resultados, historial y reportes comparten esta barrera: mientras falta
 * observar un flanco no deben parecer completos. Un fatal conserva la causa
 * M1; las tres consultas siguientes delegan sin reconstruir métricas. */
// M2: comprueba si pueden consultarse metricas de esta sesion.
// Una observacion pendiente impide publicar cifras incompletas.
static SimMetricasEstado disponible(const SimEjecucion *s)
{
    if (!s) return SIM_METRICAS_ARGUMENTO;
    if (s->estado.pendiente) return SIM_METRICAS_SECUENCIA;
    return s->estado.fatal ? s->estado.metricas : SIM_METRICAS_OK;
}

/* Consultas delegadas a M1, bloqueadas si falta observar un flanco o la época
 * está en error fatal. resultados y trap copian valores al destino del cliente;
 * esos datos siguen siendo válidos tras reset o liberación de la sesión.
 * exportar exige una salida inicialmente NULL y entrega una cadena propia:
 * liberarla con sim_metricas_liberar_reporte. Se conserva el JSON v1 de M1 y
 * se comparan snapshots con sim_metricas_comparar, sin contadores alternativos. */
// CONSULTA M2: delega en M1 la obtencion del resumen de ejecución.
// Este resumen alimenta las metricas que después presenta el IDE.
SimMetricasEstado sim_ejecucion_resultados(const SimEjecucion *s, SimMetricasResumen *r)
{
    SimMetricasEstado e = disponible(s);
    // La API entrega una copia de metricas; nunca un puntero al recolector.
    return e == SIM_METRICAS_OK ? sim_metricas_resultados(&s->metricas, r) : e;
}

/* Consulta el trap i de la sesión mediante M1. Devuelve el error pendiente
 * de observación o de argumentos sin simular otro flanco ni reconstruir métricas. */
// CONSULTA M2: delega la lectura del historial de excepciones a M1.
// El resultado se entrega por valor sin exponer recursos internos.
SimMetricasEstado sim_ejecucion_trap(const SimEjecucion *s, size_t i, SimMetricaTrap *t)
{
    SimMetricasEstado e = disponible(s);
    return e == SIM_METRICAS_OK ? sim_metricas_trap(&s->metricas, i, t) : e;
}

/* Solicita a M1 un reporte en el formato f y salida inicialmente NULL.
 * Una observación pendiente o fatal impide exportarlo; la sesión no avanza. */
// CONSULTA M2: solicita a M1 un informe de texto o JSON.
// El formato no altera el pipeline ni la época de medicion.
SimMetricasEstado sim_ejecucion_exportar(const SimEjecucion *s, SimMetricasFormato f, char **r)
{
    SimMetricasEstado e = disponible(s);
    return e == SIM_METRICAS_OK ? sim_metricas_exportar(&s->metricas, f, r) : e;
}

/* Copia los 65536 bytes de DMEM pública en un destino del cliente de ese
 * tamaño exacto. No presta acceso al almacenamiento interno ni a la bóveda.
 * Puede reflejar el último flanco real aunque su observación M1 esté pendiente;
 * leer memoria no publica por ello un resumen de métricas incompleto. */
// CONSULTA M2: copia los 64 KiB de DMEM para comprobar resultados de programas.
// No entrega un puntero mutable a la memoria interna del simulador.
SimEjecucionCodigo sim_ejecucion_memoria(const SimEjecucion *s, uint8_t *bytes, size_t n)
{
    if (!s || !bytes || n != SIM_CARGADOR_DMEM_BYTES) return SIM_EJECUCION_ARGUMENTO;
    /* S6 ya publicó esta memoria aunque M1 siga pendiente. La copia permite
     * inspeccionar ese estado real sin prestar bytes internos ni claves de la
     * bóveda, y no obliga a guardar otra DMEM permanente en la sesión. */
    memcpy(bytes, s->sim.memoria.bytes, n);
    return SIM_EJECUCION_OK;
}
