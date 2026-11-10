/*
 * Ventana "IR y variables vivas" del IDE: el detalle de la eliminación de
 * código muerto, para seguir el algoritmo paso a paso (como balc --dce).
 *
 * Tres pestañas, en el orden del algoritmo:
 *   1. IR antes:        la IR de tres direcciones con sus bloques básicos.
 *   2. Variables vivas: use, def, IN y OUT de cada bloque (diapositiva 25).
 *   3. IR después:      la IR sin el código muerto, y qué se eliminó.
 */
#ifndef IDE_DCE_VENTANA_H
#define IDE_DCE_VENTANA_H

#include <gtk/gtk.h>

#include "ide_dce.h"

/* Abre la ventana con el resultado "dce" (si ya estaba abierta, la
 * reemplaza con el resultado nuevo). Copia los textos: "dce" se puede
 * liberar después. */
void ide_mostrar_detalle_dce(GtkWindow *padre, const IdeDce *dce);

#endif
