#include "resolucion_referencias.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct RrEntrada {
    char *nombre;
    uint32_t direccion;
    RrClase clase;
};

static int clase_valida(RrClase clase)
{
    return clase == RR_FUNCION || clase == RR_ETIQUETA;
}

static int direccion_valida(uint32_t direccion)
{
    /* Memoria de instrucciones BAL de 64 KiB; solo inicios de bundle. */
    return direccion < 65536u && (direccion % 16u) == 0u;
}

void rr_inicializar(RrTabla *tabla)
{
    if (tabla) *tabla = (RrTabla){0};
}

void rr_liberar(RrTabla *tabla)
{
    if (!tabla) return;
    for (size_t i = 0; i < tabla->cantidad; ++i)
        free(tabla->entradas[i].nombre);
    free(tabla->entradas);
    *tabla = (RrTabla){0};
}

static const RrEntrada *encontrar(const RrTabla *tabla, const char *nombre,
                                  RrClase clase)
{
    if (!tabla || !nombre || !*nombre || !clase_valida(clase)) return NULL;
    for (size_t i = 0; i < tabla->cantidad; ++i)
        if (tabla->entradas[i].clase == clase &&
            strcmp(tabla->entradas[i].nombre, nombre) == 0)
            return &tabla->entradas[i];
    return NULL;
}

RrEstado rr_definir(RrTabla *tabla, const char *nombre, RrClase clase,
                    uint32_t direccion)
{
    if (!tabla || !nombre || !*nombre || !clase_valida(clase)) return RR_INVALIDO;
    if (!direccion_valida(direccion)) return RR_DESALINEADO;
    if (encontrar(tabla, nombre, clase)) return RR_DUPLICADO;
    size_t len = strlen(nombre);
    if (len == SIZE_MAX) return RR_SIN_MEMORIA;
    char *copia = malloc(len + 1);
    if (!copia) return RR_SIN_MEMORIA;
    memcpy(copia, nombre, len + 1);
    if (tabla->cantidad == tabla->capacidad) {
        if (tabla->capacidad > SIZE_MAX / 2) {
            free(copia);
            return RR_SIN_MEMORIA;
        }
        size_t nuevo = tabla->capacidad ? tabla->capacidad * 2 : 8;
        if (nuevo > SIZE_MAX / sizeof(*tabla->entradas)) {
            free(copia);
            return RR_SIN_MEMORIA;
        }
        RrEntrada *p = realloc(tabla->entradas, nuevo * sizeof(*p));
        if (!p) {
            free(copia);
            return RR_SIN_MEMORIA;
        }
        tabla->entradas = p;
        tabla->capacidad = nuevo;
    }
    tabla->entradas[tabla->cantidad++] = (RrEntrada){copia, direccion, clase};
    return RR_OK;
}

RrEstado rr_buscar(const RrTabla *tabla, const char *nombre, RrClase clase,
                   uint32_t *direccion)
{
    if (!direccion || !tabla || !nombre || !*nombre || !clase_valida(clase))
        return RR_INVALIDO;
    const RrEntrada *e = encontrar(tabla, nombre, clase);
    if (!e) return RR_NO_ENCONTRADO;
    *direccion = e->direccion;
    return RR_OK;
}

RrEstado rr_desplazamiento(const RrTabla *tabla, const char *nombre,
                          RrClase clase, uint32_t pc, RrSalto tipo,
                          int32_t *desplazamiento)
{
    if (!desplazamiento || !direccion_valida(pc) ||
        (tipo != RR_SYE && tipo != RR_CONDICIONAL)) return RR_INVALIDO;
    uint32_t destino = 0;
    RrEstado estado = rr_buscar(tabla, nombre, clase, &destino);
    if (estado != RR_OK) return estado;
    int64_t delta = (int64_t)destino - (int64_t)pc;
    int64_t minimo = tipo == RR_SYE ? -32768 : -1024;
    int64_t maximo = tipo == RR_SYE ? 32767 : 1023;
    if (delta < minimo || delta > maximo || (delta % 16) != 0)
        return RR_FUERA_DE_RANGO;
    *desplazamiento = (int32_t)delta;
    return RR_OK;
}

int rr_resolver_funcion(void *contexto, const AstNodo *id, unsigned *direccion)
{
    if (!id || id->tipo != AST_IDENTIFICADOR || !id->texto || !direccion)
        return 0;
    uint32_t d = 0;
    if (rr_buscar(contexto, id->texto, RR_FUNCION, &d) != RR_OK) return 0;
    *direccion = d;
    return 1;
}

const char *rr_nombre_estado(RrEstado estado)
{
    switch (estado) {
        case RR_OK: return "ok";
        case RR_INVALIDO: return "argumento invalido";
        case RR_DUPLICADO: return "simbolo duplicado";
        case RR_NO_ENCONTRADO: return "simbolo no encontrado";
        case RR_DESALINEADO: return "direccion invalida o no alineada";
        case RR_FUERA_DE_RANGO: return "salto fuera de rango";
        case RR_SIN_MEMORIA: return "sin memoria";
    }
    return "estado desconocido";
}
