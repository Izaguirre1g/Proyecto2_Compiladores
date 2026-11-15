#include "ir_spilling_memoria.h"
#include "memory.h"
#include <stdint.h>
#include <string.h>

/* Mismo layout escalar 3A-E2 que memory.c: RA, parametros, locales.
 * La capacidad admitida por funciones_marco.c es 65536 bytes. */
enum { SPILL_MEM_TOPE_MARCO = 65536 };

static int pertenece_a_funcion(const SimAmbito *ambito,
                              const SimAmbito *funcion)
{
    for (const SimAmbito *a = ambito; a; a = a->padre)
        if (a == funcion) return 1;
    return 0;
}

static int es_parametro(const SimSimbolo *s)
{
    return s->es_parametro || s->clase == SIM_PARAMETRO;
}

static int es_dato(const SimSimbolo *s)
{
    return s->clase == SIM_VARIABLE || s->clase == SIM_CONSTANTE ||
           s->clase == SIM_LISTA;
}

/* Calcula un intervalo y verifica la ubicacion ya asignada. Exigir el
 * offset exacto (no solo ausencia de solapamiento) detecta offsets falsos,
 * parametros reordenados y huecos no previstos por la ABI. */
static int verificar_dato(const SimSimbolo *s, size_t *cursor,
                          size_t bytes, size_t alineacion, SimAlmacen almacen)
{
    if (!s || !cursor || !alineacion || !bytes ||
        *cursor > SPILL_MEM_TOPE_MARCO ||
        s->almacen != almacen || s->desplazamiento < 0 ||
        s->bytes != bytes) return 0;
    size_t pad = (alineacion - (*cursor % alineacion)) % alineacion;
    if (pad > SPILL_MEM_TOPE_MARCO - *cursor) return 0;
    size_t inicio = *cursor + pad;
    if (bytes > SPILL_MEM_TOPE_MARCO - inicio ||
        (size_t)s->desplazamiento != inicio) return 0;
    *cursor = inicio + bytes;
    return 1;
}

int ir_spilling_backend_desde_tabla(const SimTabla *tabla,
                                    const char *nombre_funcion,
                                    size_t ranuras,
                                    IrSpillingBackend *salida)
{
    if (!tabla || !tabla->global || !nombre_funcion ||
        !*nombre_funcion || !salida) return 2;

    /* El nombre no basta: exigimos simbolo global de funcion unico y
     * exactamente un ambito propio. Las funciones anidadas no son ABI. */
    const SimSimbolo *funcion_simbolo = NULL;
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden) {
        if (s->ambito == tabla->global && s->clase == SIM_FUNCION &&
            s->nombre && strcmp(s->nombre, nombre_funcion) == 0) {
            if (funcion_simbolo) return 2;
            funcion_simbolo = s;
        }
    }
    if (!funcion_simbolo) return 2;
    const SimAmbito *ambito_funcion = NULL;
    for (const SimAmbito *a = tabla->global->siguiente_creacion;
         a; a = a->siguiente_creacion) {
        if (a->padre == tabla->global && a->nombre &&
            strcmp(a->nombre, nombre_funcion) == 0) {
            if (ambito_funcion) return 2;
            ambito_funcion = a;
        }
    }
    if (!ambito_funcion) return 2;

    size_t cursor = 4; /* [sp+0..3] conserva RA */
    size_t parametros = 0;
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden) {
        if (!pertenece_a_funcion(s->ambito, ambito_funcion) ||
            !es_parametro(s)) continue;
        if (s->ambito != ambito_funcion ||
            (s->clase != SIM_PARAMETRO && s->clase != SIM_VARIABLE) ||
            s->dimensiones != 0 ||
            (s->tipo != SIM_TIPO_INT && s->tipo != SIM_TIPO_BOOLEAN &&
             s->tipo != SIM_TIPO_CHAR) ||
            !verificar_dato(s, &cursor, 4, 4, SIM_ALMACEN_PARAMETRO)) return 2;
        ++parametros;
    }
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden) {
        if (!pertenece_a_funcion(s->ambito, ambito_funcion) ||
            es_parametro(s)) continue;
        if (!es_dato(s)) {
            /* Etiquetas y otros no ocupan memoria de datos. */
            if (s->almacen == SIM_ALMACEN_LOCAL ||
                s->almacen == SIM_ALMACEN_PARAMETRO) return 2;
            continue;
        }
        size_t bytes = 0;
        if (mem_tamano_simbolo(s, &bytes) != MEM_TAM_OK) return 2;
        size_t alineacion = s->tipo == SIM_TIPO_INT ? 4u : 1u;
        if (!verificar_dato(s, &cursor, bytes, alineacion,
                            SIM_ALMACEN_LOCAL)) return 2;
    }
    size_t padding = (4 - cursor % 4) % 4;
    if (padding > SPILL_MEM_TOPE_MARCO - cursor) return 2;
    cursor += padding;

    /* Prepara el marco final en una variable temporal: en fallo ni la tabla
     * ni el resultado del llamador se modifican. */
    IrSpillingBackend nuevo = {0};
    if (ir_spilling_backend_preparar(cursor, ranuras, parametros, &nuevo))
        return 2;
    *salida = nuevo;
    return 0;
}
