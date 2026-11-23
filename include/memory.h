#ifndef MEMORY_H
#define MEMORY_H

#include "symbol_table.h"
#include <stddef.h>

/* Etapa 3A-B: consulta PURA; no escribe en el simbolo ni asigna offsets.
 * Los tamanos son bytes del ISA (palabra 32 bits, memoria byte-addressable).
 * TEXT y LISTA necesitan una representacion ABI aun por definir.
 */
typedef enum {
    MEM_TAM_OK = 0,
    MEM_TAM_INDEFINIDO,
    MEM_TAM_DESBORDAMIENTO,
    MEM_TAM_ARGUMENTO_INVALIDO
} MemTamResultado;

/* Tamano del escalar; no sirve para arreglos ni descriptores compuestos. */
MemTamResultado mem_tamano_escalar(SimTipo tipo, size_t *bytes);

/* Tamano de almacenamiento de un simbolo. No incluye padding/alineacion.
 * Funciones, modulos, etiquetas, listas, referencias de arreglo en parametros,
 * arreglos de dimension dinamica y texto quedan INDEFINIDOS.
 */
MemTamResultado mem_tamano_simbolo(const SimSimbolo *simbolo, size_t *bytes);

/* 3A-C: distribucion GLOBAL posterior a semantica.
 * gp_base y memoria_bytes son direcciones/capacidad en bytes; NO son
 * constantes impuestas por el ISA. Esta fase no reserva memoria real.
 * En caso de error no modifica NINGUN simbolo.
 */
typedef enum {
    MEM_GLOBAL_OK = 0,
    MEM_GLOBAL_TABLA_INVALIDA,
    MEM_GLOBAL_TAMANO_INDEFINIDO,
    MEM_GLOBAL_DESBORDAMIENTO,
    MEM_GLOBAL_SIN_ESPACIO
} MemGlobalResultado;

typedef struct {
    size_t gp_base;
    size_t memoria_bytes;
    size_t bytes_globales; /* incluye padding */
} MemGlobalMapa;

MemGlobalResultado mem_asignar_globales(
    SimTabla *tabla, size_t gp_base, size_t memoria_bytes,
    MemGlobalMapa *mapa);

/* Si vale 0, un acceso a ese desplazamiento necesita construir
 * una direccion auxiliar; el simbolo sigue siendo valido. */
int mem_offset_directo(long desplazamiento);


/* 3A-D: marco local independiente, calculado antes de asignar globales.
 * Offsets no negativos relativos a sp DESPUES del prologo.
 * [sp+0..sp+3] se reserva para ra; variables desde sp+4.
 * ABI escalar 3A-E2: parametros de 4 bytes desde sp+4, antes de locales.
 * Parametros compuestos siguen pendientes.
 * Sin reutilizacion de slots entre bloques: diseño conservador.
 */
typedef enum {
    MEM_MARCO_OK = 0,
    MEM_MARCO_TABLA_INVALIDA,
    MEM_MARCO_FUNCION_INVALIDA,
    MEM_MARCO_TAMANO_INDEFINIDO,
    MEM_MARCO_DESBORDAMIENTO,
    MEM_MARCO_SIN_ESPACIO,
    MEM_MARCO_PARAMETROS_PENDIENTES
} MemMarcoResultado;

typedef struct {
    size_t bytes_marco;   /* incluye ra (4 bytes) y padding final */
    size_t bytes_locales; /* region de variables y padding, sin ra */
    size_t offset_ra;     /* siempre 0 respecto de sp tras prologo */
} MemMarco;

MemMarcoResultado mem_asignar_marco(
    SimTabla *tabla, const char *funcion, size_t capacidad_bytes,
    MemMarco *marco);

/* 3A-E1: asignacion coordinada sobre UNA tabla. Operacion transaccional:
 * preflight de globales y TODAS las funciones antes de escribir offsets.
 * No ejecuta codigo ni reserva RAM real; aplica ABI escalar 3A-E2.
 * capacidad_marco es un limite por funcion, no una particion de memoria.
 */
typedef enum {
    MEM_PROYECTO_OK = 0,
    MEM_PROYECTO_TABLA_INVALIDA,
    MEM_PROYECTO_FUNCION_INVALIDA,
    MEM_PROYECTO_TAMANO_INDEFINIDO,
    MEM_PROYECTO_DESBORDAMIENTO,
    MEM_PROYECTO_SIN_ESPACIO,
    MEM_PROYECTO_PARAMETROS_PENDIENTES
} MemProyectoResultado;

typedef struct {
    MemGlobalMapa globales;
    size_t funciones;
    size_t marco_maximo_bytes;
} MemProyectoMapa;

MemProyectoResultado mem_asignar_proyecto(
    SimTabla *tabla, size_t gp_base, size_t memoria_bytes,
    size_t capacidad_marco, MemProyectoMapa *mapa);

/* Extensión optativa O2, pendiente de revisión de Dylan: argumentos de
 * arreglo unidimensional se pasan por dirección de 32 bits en los mismos
 * slots/registros que los escalares. El puntero se presta durante la llamada;
 * no transporta longitud, no copia elementos ni comprueba límites dinámicos.
 * Conserva el preflight transaccional y rechaza listas/texto/multidimensión. */
MemProyectoResultado mem_asignar_proyecto_referencias(
    SimTabla *tabla, size_t gp_base, size_t memoria_bytes,
    size_t capacidad_marco, MemProyectoMapa *mapa);

/* 3A-E2: ABI BAL escalar, propuesta local (no propiedad del ISA).
 * Argumentos 0..3 en x8..x11; restantes en slots del llamador.
 * Todos tienen home slot de 4 bytes, incluso boolean y char.
 * offset_entrada: para argumentos 4+, relativo al sp DESPUES del prologo;
 * equivale a bytes_marco + 4*(indice-4). No emite instrucciones.
 */
typedef enum {
    MEM_ABI_OK = 0,
    MEM_ABI_ARGUMENTO_INVALIDO,
    MEM_ABI_DESBORDAMIENTO
} MemAbiResultado;

typedef struct {
    int en_registro;
    unsigned registro; /* x8..x11; cero si llega por pila */
    size_t offset_entrada; /* valido solo si en_registro == 0 */
} MemAbiArgumento;

MemAbiResultado mem_abi_argumento(size_t indice, size_t bytes_marco,
                                 MemAbiArgumento *salida);

#endif
