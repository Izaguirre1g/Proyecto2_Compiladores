#include "memory.h"
#include <stdint.h>

MemTamResultado mem_tamano_escalar(SimTipo tipo, size_t *bytes)
{
    if (!bytes) return MEM_TAM_ARGUMENTO_INVALIDO;
    *bytes = 0;
    switch (tipo) {
        case SIM_TIPO_INT:     *bytes = 4; return MEM_TAM_OK;
        case SIM_TIPO_BOOLEAN: *bytes = 1; return MEM_TAM_OK;
        case SIM_TIPO_CHAR:    *bytes = 1; return MEM_TAM_OK;
        case SIM_TIPO_TEXT:
        case SIM_TIPO_VOID:
        case SIM_TIPO_ERROR:
            return MEM_TAM_INDEFINIDO;
    }
    return MEM_TAM_ARGUMENTO_INVALIDO;
}

MemTamResultado mem_tamano_simbolo(const SimSimbolo *s, size_t *bytes)
{
    if (!s || !bytes) return MEM_TAM_ARGUMENTO_INVALIDO;
    *bytes = 0;
    if (s->clase == SIM_FUNCION || s->clase == SIM_MODULO ||
        s->clase == SIM_ETIQUETA || s->clase == SIM_LISTA)
        return MEM_TAM_INDEFINIDO;
    if (s->clase != SIM_VARIABLE && s->clase != SIM_CONSTANTE &&
        s->clase != SIM_PARAMETRO)
        return MEM_TAM_ARGUMENTO_INVALIDO;

    /* El ABI de paso por referencia para arreglos no esta fijado. */
    if (s->es_parametro && s->dimensiones != 0)
        return MEM_TAM_INDEFINIDO;
    if (s->dimensiones != 0 && !s->longitudes)
        return MEM_TAM_ARGUMENTO_INVALIDO;
    if (s->dimensiones == 0 && s->longitudes)
        return MEM_TAM_ARGUMENTO_INVALIDO;

    size_t total = 0;
    MemTamResultado resultado = mem_tamano_escalar(s->tipo, &total);
    if (resultado != MEM_TAM_OK) return resultado;
    for (unsigned i = 0; i < s->dimensiones; ++i) {
        size_t dimension = s->longitudes[i];
        if (dimension == 0) return MEM_TAM_INDEFINIDO;
        if (total > SIZE_MAX / dimension) return MEM_TAM_DESBORDAMIENTO;
        total *= dimension;
    }
    *bytes = total;
    return MEM_TAM_OK;
}

/* 3A-C: asignacion global transaccional de la zona gp+offset. */
#include <limits.h>

static int es_global_dato(const SimTabla *tabla, const SimSimbolo *s)
{
    return s->ambito == tabla->global &&
           (s->clase == SIM_VARIABLE || s->clase == SIM_CONSTANTE ||
            s->clase == SIM_LISTA);
}

static size_t alineacion_global(const SimSimbolo *s)
{
    /* La ABI BAL propuesta alinea enteros a 4; bytes a 1. */
    return s->tipo == SIM_TIPO_INT ? 4u : 1u;
}

static int avanzar_global(size_t *cursor, size_t alineacion,
                          size_t tamano, size_t capacidad)
{
    size_t resto = *cursor % alineacion;
    size_t relleno = resto ? alineacion - resto : 0;
    if (relleno > capacidad - *cursor) return 0;
    size_t inicio = *cursor + relleno;
    if (tamano > capacidad - inicio) return 0;
    *cursor = inicio + tamano;
    return 1;
}

/* 3A-D: marco por funcion, sin modificar semantica ni backend. */
#include <string.h>

static int dentro_de_funcion(const SimAmbito *ambito,
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

static MemMarcoResultado calcular_marco(const SimTabla *tabla,
                                       const SimAmbito *ambito,
                                       size_t capacidad, size_t *total)
{
    size_t cursor = 4; /* salvaguarda de ra */
    if (capacidad < cursor) return MEM_MARCO_SIN_ESPACIO;
    /* Primero, slots de parametros de 4 bytes, en orden de declaracion. */
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden) {
        if (!dentro_de_funcion(s->ambito, ambito) || !es_parametro(s)) continue;
        if (s->ambito != ambito || (s->clase != SIM_PARAMETRO &&
            s->clase != SIM_VARIABLE) || s->dimensiones != 0 ||
            s->tipo == SIM_TIPO_TEXT || s->tipo == SIM_TIPO_VOID ||
            s->tipo == SIM_TIPO_ERROR)
            return MEM_MARCO_PARAMETROS_PENDIENTES;
        if (s->tipo != SIM_TIPO_INT && s->tipo != SIM_TIPO_BOOLEAN &&
            s->tipo != SIM_TIPO_CHAR)
            return MEM_MARCO_PARAMETROS_PENDIENTES;
        if (cursor > (size_t)LONG_MAX || 4u > (size_t)LONG_MAX - cursor)
            return MEM_MARCO_DESBORDAMIENTO;
        if (!avanzar_global(&cursor, 4, 4, capacidad))
            return MEM_MARCO_SIN_ESPACIO;
    }
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden) {
        if (!dentro_de_funcion(s->ambito, ambito) || es_parametro(s)) continue;
        if (s->clase != SIM_VARIABLE && s->clase != SIM_CONSTANTE &&
            s->clase != SIM_LISTA) continue;
        size_t bytes = 0;
        MemTamResultado r = mem_tamano_simbolo(s, &bytes);
        if (r == MEM_TAM_INDEFINIDO) return MEM_MARCO_TAMANO_INDEFINIDO;
        if (r == MEM_TAM_DESBORDAMIENTO) return MEM_MARCO_DESBORDAMIENTO;
        if (r != MEM_TAM_OK || bytes == 0) return MEM_MARCO_TABLA_INVALIDA;
        size_t align = alineacion_global(s);
        size_t pad = (align - cursor % align) % align;
        if (cursor > (size_t)LONG_MAX ||
            pad > (size_t)LONG_MAX - cursor ||
            bytes > (size_t)LONG_MAX - cursor - pad)
            return MEM_MARCO_DESBORDAMIENTO;
        if (!avanzar_global(&cursor, align, bytes, capacidad))
            return MEM_MARCO_SIN_ESPACIO;
    }
    size_t padding = (4 - cursor % 4) % 4;
    if (padding > capacidad - cursor) return MEM_MARCO_SIN_ESPACIO;
    if (padding > (size_t)LONG_MAX - cursor)
        return MEM_MARCO_DESBORDAMIENTO;
    *total = cursor + padding;
    return MEM_MARCO_OK;
}

static void escribir_marco(SimTabla *tabla, const SimAmbito *ambito);

MemMarcoResultado mem_asignar_marco(SimTabla *tabla, const char *funcion,
                                   size_t capacidad_bytes, MemMarco *marco)
{
    if (!tabla || !funcion || !*funcion || !marco ||
        !sim_verificar_pre_memoria(tabla, NULL))
        return MEM_MARCO_TABLA_INVALIDA;
    const SimSimbolo *sim_funcion = NULL;
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden)
        if (s->ambito == tabla->global && s->clase == SIM_FUNCION &&
            strcmp(s->nombre, funcion) == 0) {
            sim_funcion = s;
            break;
        }
    if (!sim_funcion) return MEM_MARCO_FUNCION_INVALIDA;
    const SimAmbito *ambito_funcion = NULL;
    for (const SimAmbito *a = tabla->global->siguiente_creacion;
         a; a = a->siguiente_creacion)
        if (a->padre == tabla->global && strcmp(a->nombre, funcion) == 0) {
            if (ambito_funcion) return MEM_MARCO_FUNCION_INVALIDA;
            ambito_funcion = a;
        }
    if (!ambito_funcion) return MEM_MARCO_FUNCION_INVALIDA;
    size_t total = 0;
    MemMarcoResultado r = calcular_marco(tabla, ambito_funcion,
                                        capacidad_bytes, &total);
    if (r != MEM_MARCO_OK) return r;
    escribir_marco(tabla, ambito_funcion);
    marco->offset_ra = 0;
    marco->bytes_marco = total;
    marco->bytes_locales = total - 4;
    return MEM_MARCO_OK;
}

int mem_offset_directo(long desplazamiento)
{
    return desplazamiento >= -1024 && desplazamiento <= 1023;
}

static MemGlobalResultado validar_globales(
    const SimTabla *tabla, size_t gp_base, size_t memoria_bytes,
    size_t *total)
{
    if (!tabla || !total || gp_base > memoria_bytes)
        return MEM_GLOBAL_TABLA_INVALIDA;

    /* Offset relativo a gp: no modifica la tabla durante el preflight. */
    const size_t capacidad = memoria_bytes - gp_base;
    size_t cursor = 0;
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden) {
        if (!es_global_dato(tabla, s)) continue;
        size_t bytes = 0;
        MemTamResultado r = mem_tamano_simbolo(s, &bytes);
        if (r == MEM_TAM_INDEFINIDO) return MEM_GLOBAL_TAMANO_INDEFINIDO;
        if (r == MEM_TAM_DESBORDAMIENTO) return MEM_GLOBAL_DESBORDAMIENTO;
        if (r != MEM_TAM_OK || bytes == 0) return MEM_GLOBAL_TABLA_INVALIDA;
        size_t align = alineacion_global(s);
        size_t resto = cursor % align;
        size_t relleno = resto ? align - resto : 0;
        if (cursor > (size_t)LONG_MAX ||
            relleno > (size_t)LONG_MAX - cursor ||
            bytes > (size_t)LONG_MAX - (cursor + relleno))
            return MEM_GLOBAL_DESBORDAMIENTO;
        if (!avanzar_global(&cursor, align, bytes, capacidad))
            return MEM_GLOBAL_SIN_ESPACIO;
    }

    *total = cursor;
    return MEM_GLOBAL_OK;
}

static void escribir_globales(SimTabla *tabla)
{
    /* Segunda pasada: se conocen todas las longitudes y no hay fallos. */
    size_t cursor = 0;
    for (SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden) {
        if (!es_global_dato(tabla, s)) continue;
        size_t bytes = 0;
        (void)mem_tamano_simbolo(s, &bytes);
        size_t align = alineacion_global(s);
        size_t resto = cursor % align;
        if (resto) cursor += align - resto;
        sim_definir_ubicacion(s, SIM_ALMACEN_GLOBAL, (long)cursor, bytes);
        cursor += bytes;
    }
}


MemGlobalResultado mem_asignar_globales(
    SimTabla *tabla, size_t gp_base, size_t memoria_bytes,
    MemGlobalMapa *mapa)
{
    if (!tabla || !mapa || !sim_verificar_pre_memoria(tabla, NULL))
        return MEM_GLOBAL_TABLA_INVALIDA;
    size_t total = 0;
    MemGlobalResultado r = validar_globales(tabla, gp_base, memoria_bytes, &total);
    if (r != MEM_GLOBAL_OK) return r;
    escribir_globales(tabla);
    mapa->gp_base = gp_base;
    mapa->memoria_bytes = memoria_bytes;
    mapa->bytes_globales = total;
    return MEM_GLOBAL_OK;
}

/* Busca exactamente un ambito de funcion asociado al simbolo global. */
static const SimAmbito *ambito_de_funcion(const SimTabla *tabla,
                                          const char *nombre)
{
    const SimAmbito *encontrado = NULL;
    for (const SimAmbito *a = tabla->global->siguiente_creacion;
         a; a = a->siguiente_creacion) {
        if (a->padre == tabla->global && strcmp(a->nombre, nombre) == 0) {
            if (encontrado) return NULL;
            encontrado = a;
        }
    }
    return encontrado;
}

/* La fase de escritura usa exactamente el orden calculado en calcular_marco. */
static void escribir_marco(SimTabla *tabla, const SimAmbito *ambito)
{
    size_t cursor = 4;
    for (SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden) {
        if (!dentro_de_funcion(s->ambito, ambito) || !es_parametro(s)) continue;
        sim_definir_ubicacion(s, SIM_ALMACEN_PARAMETRO, (long)cursor, 4);
        cursor += 4;
    }
    for (SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden) {
        if (!dentro_de_funcion(s->ambito, ambito) || es_parametro(s)) continue;
        if (s->clase != SIM_VARIABLE && s->clase != SIM_CONSTANTE &&
            s->clase != SIM_LISTA) continue;
        size_t bytes = 0;
        (void)mem_tamano_simbolo(s, &bytes);
        size_t align = alineacion_global(s);
        cursor += (align - cursor % align) % align;
        sim_definir_ubicacion(s, SIM_ALMACEN_LOCAL, (long)cursor, bytes);
        cursor += bytes;
    }
}

MemProyectoResultado mem_asignar_proyecto(
    SimTabla *tabla, size_t gp_base, size_t memoria_bytes,
    size_t capacidad_marco, MemProyectoMapa *mapa)
{
    if (!tabla || !mapa || !tabla->global ||
        !sim_verificar_pre_memoria(tabla, NULL))
        return MEM_PROYECTO_TABLA_INVALIDA;

    /* PREFLIGHT: sin escrituras de ubicacion, incluido el mapa de salida. */
    size_t global_total = 0;
    MemGlobalResultado rg = validar_globales(tabla, gp_base, memoria_bytes,
                                              &global_total);
    if (rg != MEM_GLOBAL_OK) {
        switch (rg) {
            case MEM_GLOBAL_TAMANO_INDEFINIDO: return MEM_PROYECTO_TAMANO_INDEFINIDO;
            case MEM_GLOBAL_DESBORDAMIENTO: return MEM_PROYECTO_DESBORDAMIENTO;
            case MEM_GLOBAL_SIN_ESPACIO: return MEM_PROYECTO_SIN_ESPACIO;
            default: return MEM_PROYECTO_TABLA_INVALIDA;
        }
    }

    size_t funciones = 0, marco_maximo = 0;
    for (const SimSimbolo *f = tabla->primero; f; f = f->siguiente_orden) {
        if (f->ambito != tabla->global || f->clase != SIM_FUNCION) continue;
        const SimAmbito *a = ambito_de_funcion(tabla, f->nombre);
        if (!a) return MEM_PROYECTO_FUNCION_INVALIDA;
        size_t total = 0;
        MemMarcoResultado rm = calcular_marco(tabla, a, capacidad_marco, &total);
        switch (rm) {
            case MEM_MARCO_OK: break;
            case MEM_MARCO_PARAMETROS_PENDIENTES: return MEM_PROYECTO_PARAMETROS_PENDIENTES;
            case MEM_MARCO_TAMANO_INDEFINIDO: return MEM_PROYECTO_TAMANO_INDEFINIDO;
            case MEM_MARCO_DESBORDAMIENTO: return MEM_PROYECTO_DESBORDAMIENTO;
            case MEM_MARCO_SIN_ESPACIO: return MEM_PROYECTO_SIN_ESPACIO;
            default: return MEM_PROYECTO_TABLA_INVALIDA;
        }
        if (total > marco_maximo) marco_maximo = total;
        ++funciones;
    }

    /* COMMIT: las mismas operaciones que ya pasaron la validacion. */
    escribir_globales(tabla);
    for (const SimSimbolo *f = tabla->primero; f; f = f->siguiente_orden) {
        if (f->ambito != tabla->global || f->clase != SIM_FUNCION) continue;
        escribir_marco(tabla, ambito_de_funcion(tabla, f->nombre));
    }
    mapa->globales.gp_base = gp_base;
    mapa->globales.memoria_bytes = memoria_bytes;
    mapa->globales.bytes_globales = global_total;
    mapa->funciones = funciones;
    mapa->marco_maximo_bytes = marco_maximo;
    return MEM_PROYECTO_OK;
}

/* x8..x11 son caller-saved. La ubicacion de entrada por pila depende
 * del marco calculado y de la convencion de reserva del llamador. */
MemAbiResultado mem_abi_argumento(size_t indice, size_t bytes_marco,
                                 MemAbiArgumento *salida)
{
    if (!salida || bytes_marco < 4 || bytes_marco % 4 != 0)
        return MEM_ABI_ARGUMENTO_INVALIDO;
    MemAbiArgumento temporal = {0, 0, 0};
    if (indice < 4) {
        temporal.en_registro = 1;
        temporal.registro = (unsigned)(8 + indice);
    } else {
        size_t posicion = indice - 4;
        if (posicion > (SIZE_MAX - bytes_marco) / 4)
            return MEM_ABI_DESBORDAMIENTO;
        temporal.offset_entrada = bytes_marco + 4 * posicion;
        if (temporal.offset_entrada > (size_t)LONG_MAX)
            return MEM_ABI_DESBORDAMIENTO;
    }
    *salida = temporal;
    return MEM_ABI_OK;
}
