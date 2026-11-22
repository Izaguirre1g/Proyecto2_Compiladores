/*
 * Ventana "-O0 / -O2" del IDE (botón "-O0 / -O2").
 *
 * Compila el código del editor con los dos niveles de optimización de balc,
 * los ejecuta en el simulador VLIW y los compara (ide_optimizacion.c):
 *   - Arriba: si -O2 deja el mismo resultado de main que -O0 (regla de oro).
 *   - Pestaña "Comparación": ciclos, instrucciones, IPC, tamaño y tiempo de
 *     compilación de cada nivel, en una gráfica (azul -O0, naranja -O2).
 *   - Pestaña "Pasadas de -O2": qué hizo cada optimización y en qué orden.
 *   - Pestaña "Resultado de main": el valor final de cada variable de main
 *     en los dos niveles.
 *   - Pestañas "Ensamblador" e "IR de -O2": el código de cada nivel y la IR
 *     antes de optimizar, optimizada y con spilling.
 */
#ifndef IDE_OPTIMIZACION_VENTANA_H
#define IDE_OPTIMIZACION_VENTANA_H

#include <gtk/gtk.h>

/* Abre la ventana para el código "texto" (no lo guarda: se puede liberar
 * después). "ruta" es la del archivo (o NULL) y "nombre" el que se muestra.
 * Si ya estaba abierta, la reemplaza. */
void ide_mostrar_optimizacion(GtkWindow *padre, const char *texto, const char *ruta,
                              const char *nombre);

#endif
