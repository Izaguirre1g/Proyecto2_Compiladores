/*
 * Ventana del botón "Tabla de símbolos" (ver include/ide_simbolos.h).
 *
 * Tiene tres pestañas:
 *   1. Símbolos: la misma estructura del archivo .simbolos.md de "balc -m":
 *      una fila por símbolo (en el orden en que se declararon) con Nombre,
 *      Clase, Tipo, Ambito, Nivel, Almacen, Bytes, Desplazamiento y
 *      Direccion de codigo; debajo, la sección Memoria (funciones, marco
 *      máximo, código desde, GP, memoria global y SP).
 *   2. Ámbitos: cada ámbito (global, cada función y cada bloque { }), su
 *      nivel, su ámbito padre y cuántos símbolos tiene en su tabla hash.
 *   3. sim_imprimir: el listado tal como lo escribe symbol_table.c.
 */
#ifndef IDE_SIMBOLOS_VENTANA_H
#define IDE_SIMBOLOS_VENTANA_H

#include <gtk/gtk.h>

#include "ide_simbolos.h"

/* Abre la ventana con la tabla de "texto" (si ya estaba abierta, la
 * reemplaza con el resultado nuevo). "nombre" es el nombre del archivo para
 * el título. */
void ide_mostrar_simbolos(GtkWindow *padre, const char *texto, const char *ruta,
                          const char *nombre);

#endif
