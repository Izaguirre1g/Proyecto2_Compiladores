/*
 * Ventana "Ensamblador" (ver include/ide_ensamblador_ventana.h).
 * Solo muestra los textos que ya preparó ide_ensamblador.c; no genera nada.
 */
#include "ide_ensamblador_ventana.h"

/* La ventana abierta (solo hay una): NULL si está cerrada. */
static GtkWidget *ventana_asm = NULL;

/* Al cerrarla se olvida, para que el botón la vuelva a crear. */
static void al_destruir_ensamblador(GtkWidget *widget, gpointer datos)
{
    (void)widget;
    (void)datos;
    ventana_asm = NULL;
}

/* Una pestaña con un texto de solo lectura en letra monoespaciada. */
static GtkWidget *pestana_de_texto(const char *texto)
{
    GtkWidget *vista = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(vista), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(vista), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(vista), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(vista), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(vista), 6);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(vista)),
                             texto != NULL ? texto : "", -1);

    GtkWidget *recuadro = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(recuadro), vista);
    return recuadro;
}

gboolean ide_ensamblador_abierta(void)
{
    return ventana_asm != NULL;
}

void ide_mostrar_ensamblador(GtkWindow *padre, const IdeEnsamblador *a, const char *nombre)
{
    /* Si ya estaba abierta, se cierra: el resultado nuevo la reemplaza. */
    if (ventana_asm != NULL) gtk_widget_destroy(ventana_asm);

    gchar *base = g_path_get_basename(nombre);
    gchar *titulo = g_strdup_printf("Ensamblador — %s", base);
    ventana_asm = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ventana_asm), titulo);
    gtk_window_set_transient_for(GTK_WINDOW(ventana_asm), padre);
    gtk_window_set_default_size(GTK_WINDOW(ventana_asm), 820, 640);
    g_signal_connect(ventana_asm, "destroy", G_CALLBACK(al_destruir_ensamblador), NULL);
    g_free(titulo);
    g_free(base);

    /* Arriba, el resumen y cómo leer cada línea. */
    gchar *resumen = g_markup_printf_escaped(
        "<b>%zu bundles</b> · %zu funci%s · %zu saltos (%zu reescrito%s por alcance)\n"
        "Es el mismo ensamblador que genera <tt>balc -s</tt>. Cada línea es un bundle "
        "VLIW con 4 slots: <tt>ALU | LSU | BRU | CRIPTO</tt>; <tt>nop</tt> es un slot vacío "
        "(una línea con solo <tt>nop</tt> es un bundle vacío que espera un resultado).\n"
        "Primero va <tt># __arranque:</tt> (inicializa gp y sp y llama a main); el código de "
        "cada función empieza en <tt># __fn_nombre:</tt>, por ejemplo <tt># __fn_main:</tt>.",
        a->bundles, a->funciones, a->funciones == 1 ? "ón" : "ones",
        a->saltos, a->reescritos, a->reescritos == 1 ? "" : "s");
    GtkWidget *encabezado = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(encabezado), resumen);
    gtk_label_set_xalign(GTK_LABEL(encabezado), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(encabezado), TRUE);
    g_free(resumen);

    GtkWidget *pestanas = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_de_texto(a->ensamblador),
                             gtk_label_new("Ensamblador (.asm)"));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_de_texto(a->binario),
                             gtk_label_new("Binario (bundles codificados)"));

    GtkWidget *columna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(columna), 8);
    gtk_box_pack_start(GTK_BOX(columna), encabezado, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(columna), pestanas, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(ventana_asm), columna);
    gtk_widget_show_all(ventana_asm);
}
