/* Comparación interna compartida por el recolector y la tabla GTK.
 * No añade una API de selección de observables ni cambia la política DCE.
 * Los niveles y sus memorias son prestados durante la llamada. */
#ifndef IDE_OPTIMIZACION_INTERNO_H
#define IDE_OPTIMIZACION_INTERNO_H
#include "ide_optimizacion.h"

/* Compara dos objetos ya identificados por su posición en cada nivel.
 * 1: mismos tipo, tamaño y bytes; 0: diferencia; -1: ejecución/datos no
 * verificables. Comprueba rangos y overflow; no reserva ni libera recursos. */
int ide_opt_comparar_variable(const IdeOptNivel *a, size_t ia,
                             const IdeOptNivel *b, size_t ib);
#endif
