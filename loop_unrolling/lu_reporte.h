#ifndef BAL_LU_REPORTE_H
#define BAL_LU_REPORTE_H
#include <stdio.h>
#include "ir.h"
#include "loop_unrolling.h"

/* Reporte "bonito" del loop unrolling para la terminal (balc --unroll):
 *   1. tabla de ciclos con la decision tomada en cada uno,
 *   2. metricas antes/despues (instrucciones, bloques, saltos, etiquetas),
 *   3. IR antes y despues lado a lado; '+' marca lo creado por el unrolling.
 * Usa colores ANSI solo si la salida es una terminal y NO_COLOR no existe. */

/* Una linea ya formateada de la IR. tipo: 'B' encabezado de bloque,
 * 'I' instruccion, 'N' instruccion creada por el unrolling, ' ' vacia. */
typedef struct { char *texto; char tipo; } LuLinea;

/* "Foto" de una IR: sus lineas formateadas y sus metricas. Se toma una
 * antes de desenrollar y otra despues, porque la IR se modifica. */
typedef struct {
    LuLinea *lineas;
    size_t cantidad;
    size_t instr, bloques, saltos, etiquetas;  /* Metricas */
    unsigned contador_t, contador_l;           /* Contadores $tN / $LN de la IR */
} LuVista;

/* Toma la foto de `ir`. Si `base` no es NULL, las instrucciones que usan
 * temporales o etiquetas mas nuevos que los de `base` se marcan como 'N'.
 * Devuelve 0 en exito y 2 si falta memoria. */
int lu_vista_crear(const IrPrograma *ir, const LuVista *base, LuVista *v);
void lu_vista_liberar(LuVista *v);

/* 1 si conviene usar colores al escribir en f. */
int lu_usar_color(FILE *f);

/* Imprime el reporte completo. `archivo` es el fuente (se muestra su nombre). */
void lu_reporte(FILE *f, const char *archivo, unsigned factor, const LuVista *antes,
                const LuVista *despues, const LuResumen *res, int color);

#endif
