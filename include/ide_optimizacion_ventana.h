/*
 * Ventana "Niveles -O0 / -O1 / -O2" del IDE (botón "-O0/-O1/-O2").
 *
 * Compila el código del editor con los tres niveles de optimización de
 * balc, los ejecuta en el simulador VLIW y los compara (ide_optimizacion.c):
 *   - Arriba: si -O1 y -O2 dejan el mismo resultado de main que -O0 (regla
 *     de oro).
 *   - Pestaña "Comparación": ciclos, instrucciones, IPC, tamaño y tiempo de
 *     compilación de cada nivel en una gráfica (azul -O0, naranja -O1,
 *     aqua -O2).
 *   - Pestaña "Instrucciones": las instrucciones de la IR antes y después de
 *     cada optimización, y cuántas eliminó, reordenó y renombró cada una.
 *   - Pestaña "Qué hace cada nivel": lo que hizo el DCE de -O1 y cada pasada
 *     de -O2, en orden.
 *   - Pestaña "Resultado de main": el valor final de cada variable de main
 *     en los tres niveles.
 *   - Pestañas "Ensamblador", "Código de -O1" e "IR de -O2".
 */
#ifndef IDE_OPTIMIZACION_VENTANA_H
#define IDE_OPTIMIZACION_VENTANA_H

#include <gtk/gtk.h>

/* Abre la ventana para el código "texto" (no lo guarda: se puede liberar
 * después). "ruta" es la del archivo (o NULL) y "nombre" el que se muestra.
 * Si ya estaba abierta, la reemplaza. */
void ide_mostrar_optimizacion(GtkWindow *padre, const char *texto, const char *ruta,
                              const char *nombre);

/* Agrega una acción que obtiene el código vigente del editor al pulsarla. */
void ide_optimizacion_agregar_spilling(GCallback accion, gpointer datos);

#endif
