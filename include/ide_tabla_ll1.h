/*
 * Ventana del IDE que muestra la tabla LL(1) de BAL (src/LL1/).
 *
 * Tiene tres pestañas, en el orden en que se construye la tabla:
 *   1. Tabla M[A, a]: una fila por no terminal y una columna por terminal.
 *      Cada celda llena tiene el número de la producción que se usa; una
 *      celda vacía es un error de sintaxis. Al hacer clic en una celda se
 *      ve la producción completa.
 *   2. Producciones: la gramática de BAL ya transformada para LL(1).
 *   3. PRIMERO y SIGUIENTE: los conjuntos con los que se llenó la tabla.
 */
#ifndef IDE_TABLA_LL1_H
#define IDE_TABLA_LL1_H

#include <gtk/gtk.h>

/* Abre la ventana de la tabla (o la trae al frente si ya estaba abierta). */
void ide_mostrar_tabla_ll1(GtkWindow *padre);

#endif
