/*
 * Ventana "Ensamblador" del IDE: el ensamblador VLIW del código del editor.
 *
 * Dos pestañas:
 *   1. Ensamblador: el mismo texto que genera "balc -s" (.asm). Cada línea
 *      es un bundle con sus 4 slots: ALU | LSU | BRU | CRIPTO.
 *   2. Binario: cada bundle con su dirección (PC) y su codificación en
 *      hexadecimal y por campos, como "balc -p".
 */
#ifndef IDE_ENSAMBLADOR_VENTANA_H
#define IDE_ENSAMBLADOR_VENTANA_H

#include <gtk/gtk.h>

#include "ide_ensamblador.h"

/* Abre la ventana con el resultado "asm_ide" del archivo "nombre" (si ya
 * estaba abierta, la reemplaza). Copia los textos: "asm_ide" se puede
 * liberar después. */
void ide_mostrar_ensamblador(GtkWindow *padre, const IdeEnsamblador *asm_ide, const char *nombre);

/* TRUE si la ventana está abierta (para actualizarla cuando cambia el código). */
gboolean ide_ensamblador_abierta(void);

#endif
