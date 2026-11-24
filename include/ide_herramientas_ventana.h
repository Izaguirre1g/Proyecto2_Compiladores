/* Acciones GTK; las entradas se toman prestadas solo durante la llamada. */
#ifndef IDE_HERRAMIENTAS_VENTANA_H
#define IDE_HERRAMIENTAS_VENTANA_H
#include <gtk/gtk.h>
#include "ide_ensamblador.h"
#include "ide_optimizacion.h"
void ide_mostrar_lexico(GtkWindow *padre, const char *texto);
void ide_visualizar_json(GtkWindow *padre, const char *texto, gboolean spilling);
void ide_elegir_asm(GtkWindow *padre);
/* El botón conserva copias propias: el llamador puede liberar a inmediatamente. */
GtkWidget *ide_boton_exportar(GtkWindow *padre, const IdeEnsamblador *a);
/* Copia los resultados del nivel; devuelve un botón deshabilitado si no compiló. */
GtkWidget *ide_boton_exportar_nivel(GtkWindow *padre, const IdeOptNivel *nivel,
                                   const char *nombre, const char *etiqueta);
#endif
