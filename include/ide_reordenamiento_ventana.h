/*
 * Ventana "Reordenamiento" del IDE (botón "Reordenamiento").
 *
 *   - Arriba: la latencia (ciclos entre escribir un valor y poder leerlo),
 *     la opción de desenrollar los ciclos primero y las métricas: ciclos
 *     estimados antes y después, instrucciones movidas y dependencias.
 *   - Pestaña "Bloques": cada bloque básico con sus instrucciones movidas,
 *     sus ciclos antes y después y sus dependencias (RAW, WAR, WAW...).
 *   - Pestaña "IR antes y después": las dos lado a lado; lo que cambió de
 *     lugar va en verde con "*", como en balc --reorder.
 *   - Pestaña "Dependencias": el grafo de cada bloque que cambió (quién
 *     debe ir antes de quién y por qué), con la ruta crítica en negrita.
 * Al cambiar la latencia o la opción de desenrollar, se vuelve a calcular.
 */
#ifndef IDE_REORDENAMIENTO_VENTANA_H
#define IDE_REORDENAMIENTO_VENTANA_H

#include <gtk/gtk.h>

/* Abre la ventana para el código "texto" (copia el texto: se puede liberar
 * después). "ruta" es la del archivo (o NULL) y "nombre" el que se muestra.
 * Si ya estaba abierta, la reemplaza. */
void ide_mostrar_reordenamiento(GtkWindow *padre, const char *texto, const char *ruta,
                                const char *nombre);

#endif
