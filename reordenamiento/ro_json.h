#ifndef BAL_RO_JSON_H
#define BAL_RO_JSON_H
#include <stdio.h>
#include "ir.h"

/* Exporta el grafo de dependencias de cada bloque basico como JSON
 * (balc --reorder-json), para verlo en reordenamiento/visualizar_grafo.html.
 * `ir` es la IR ANTES de reordenar (con sus bloques basicos). Incluye los
 * nodos, las aristas con su tipo y variable, la ruta critica, el orden que
 * elige el reordenamiento y el ciclo de cada instruccion antes y despues.
 * `factor_unroll` es 0 si no se desenrollo antes. Formato en el README.
 * Devuelve 0 en exito o 2 si falta memoria. */
int ro_exportar_json(FILE *f, const char *archivo, const IrPrograma *ir,
                     unsigned latencia, unsigned factor_unroll);

#endif
