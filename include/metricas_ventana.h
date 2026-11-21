/*
 * Ventana "Métricas" del IDE (botón "Métricas"), en src/Metricas/.
 *
 * Ejecuta el código del editor en el simulador VLIW (metricas.c) y lo
 * muestra con gráficas:
 *   - Arriba: los números principales (ciclos, IPC, instrucciones
 *     ejecutadas y tamaño del código en bytes y bundles).
 *   - Pestaña "Ejecución": qué tanto se usó cada slot del bundle (ALU, LSU,
 *     BRU, CRIPTO) y cuántos bundles traían trabajo y cuántos eran solo NOP.
 *   - Pestaña "Antes y después del DCE": el mismo código con y sin el código
 *     muerto (el que da el botón Código muerto), métrica por métrica,
 *     incluidos bytes originales/optimizados, diferencia y variación.
 *   - Pestaña "Tabla": todos los números, para leerlos sin las gráficas.
 * Al pasar el mouse sobre una barra se ve su valor exacto.
 */
#ifndef METRICAS_VENTANA_H
#define METRICAS_VENTANA_H

#include <gtk/gtk.h>

/* Abre la ventana para el código "texto" (no lo guarda: se puede liberar
 * después). "ruta" es la del archivo (o NULL) y "nombre" el que se muestra.
 * Si ya estaba abierta, la reemplaza. */
void ide_mostrar_metricas(GtkWindow *padre, const char *texto, const char *ruta,
                          const char *nombre);

#endif
