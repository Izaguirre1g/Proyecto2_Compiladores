/*
 * Ventana "Loop unrolling" del IDE (botón "Loop unrolling").
 *
 *   - Arriba: el factor k (se puede cambiar: se vuelve a calcular) y las
 *     métricas antes y después (instrucciones, bloques, saltos, etiquetas).
 *   - Pestaña "Ciclos": cada ciclo con su línea, condición, paso, vueltas y
 *     la decisión (TOTAL, PARCIAL o sin cambios, con el motivo).
 *   - Pestaña "IR antes y después": las dos lado a lado; las instrucciones
 *     que creó el unrolling van en verde con "+", como en balc --unroll.
 */
#ifndef IDE_UNROLLING_VENTANA_H
#define IDE_UNROLLING_VENTANA_H

#include <gtk/gtk.h>

/* Abre la ventana para el código "texto" (copia el texto: se puede liberar
 * después). "ruta" es la del archivo (o NULL) y "nombre" el que se muestra.
 * Si ya estaba abierta, la reemplaza. */
void ide_mostrar_unrolling(GtkWindow *padre, const char *texto, const char *ruta,
                           const char *nombre);

#endif
