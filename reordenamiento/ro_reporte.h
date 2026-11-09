#ifndef BAL_RO_REPORTE_H
#define BAL_RO_REPORTE_H
#include <stdio.h>
#include "ir.h"
#include "reordenamiento.h"

/* Reporte visual del reordenamiento (balc --reorder):
 *   1. tabla de bloques reordenados (movidas, ciclos antes/despues),
 *   2. metricas y dependencias respetadas (RAW, WAR, WAW, ...),
 *   3. IR antes y despues lado a lado; '*' marca lo que cambio de lugar
 *      (el numero de cada instruccion es su posicion ORIGINAL).
 * `ir` es la IR ya reordenada; el "antes" se reconstruye con res->origen. */
void ro_reporte(FILE *f, const char *archivo, const IrPrograma *ir,
                const RoResumen *res, unsigned latencia, int color);

/* 1 si conviene usar colores al escribir en f (terminal y sin NO_COLOR). */
int ro_usar_color(FILE *f);

#endif
