/*
 * Ventana "IR y variables vivas" (ver include/ide_dce_ventana.h).
 * Solo muestra los textos que ya preparó ide_dce.c; no calcula nada.
 */
#include "ide_dce_ventana.h"

/* La ventana abierta (solo hay una): NULL si está cerrada. */
static GtkWidget *ventana_dce = NULL;

/* Al cerrarla se olvida, para que el botón la vuelva a crear. */
static void al_destruir_detalle(GtkWidget *widget, gpointer datos)
{
    (void)widget;
    (void)datos;
    ventana_dce = NULL;
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

/* El texto de la pestaña "IR después": primero qué se eliminó y qué se
 * conservó (como dce_imprimir_resultado), y después la IR. */
static gchar *texto_despues(const IdeDce *d)
{
    const DceResultado *r = &d->resultado;
    GString *t = g_string_new(NULL);
    g_string_append_printf(t, "Codigo muerto eliminado: %zu de %zu instrucciones "
                              "(quedan %zu), en %zu pasada%s\n",
                           r->cantidad_eliminadas, r->instrucciones_antes,
                           r->instrucciones_despues, r->pasadas, r->pasadas == 1 ? "" : "s");
    for (size_t i = 0; i < r->cantidad_eliminadas; ++i) {
        g_string_append_printf(t, "  - linea %zu: %s\n", r->eliminadas[i].linea,
                               r->eliminadas[i].texto);
    }
    if (r->llamadas_conservadas > 0) {
        g_string_append_printf(t, "Llamadas conservadas aunque su resultado no se usa "
                                  "(pueden tener efectos): %zu\n", r->llamadas_conservadas);
    }
    if (r->riesgosas_conservadas > 0) {
        g_string_append_printf(t, "Operaciones conservadas porque pueden fallar "
                                  "(division o potencia): %zu\n", r->riesgosas_conservadas);
    }
    g_string_append(t, "\n");
    if (d->ir_despues != NULL) g_string_append(t, d->ir_despues);
    return g_string_free(t, FALSE);
}

void ide_mostrar_detalle_dce(GtkWindow *padre, const IdeDce *d)
{
    /* Si ya estaba abierta, se cierra: el resultado nuevo la reemplaza. */
    if (ventana_dce != NULL) gtk_widget_destroy(ventana_dce);

    ventana_dce = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ventana_dce), "IR y variables vivas");
    gtk_window_set_transient_for(GTK_WINDOW(ventana_dce), padre);
    gtk_window_set_default_size(GTK_WINDOW(ventana_dce), 760, 620);
    g_signal_connect(ventana_dce, "destroy", G_CALLBACK(al_destruir_detalle), NULL);

    /* Arriba, cómo leer cada pestaña. */
    GtkWidget *ayuda = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ayuda),
        "Una variable está <b>viva</b> si su valor se lee más adelante en algún camino:  "
        "<tt>IN[B] = use ∪ (OUT[B] − def)</tt>,  <tt>OUT[B] = ∪ IN[sucesores]</tt>.\n"
        "Una asignación a una variable que no está viva es <b>código muerto</b>, salvo que "
        "tenga efectos (llamadas, escrituras) o pueda fallar (división, potencia).");
    gtk_label_set_xalign(GTK_LABEL(ayuda), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(ayuda), TRUE);

    gchar *despues = texto_despues(d);
    GtkWidget *pestanas = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_de_texto(d->ir_antes),
                             gtk_label_new("1. IR antes"));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_de_texto(d->vivas),
                             gtk_label_new("2. Variables vivas"));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_de_texto(despues),
                             gtk_label_new("3. IR después del DCE"));
    g_free(despues);

    GtkWidget *columna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(columna), 8);
    gtk_box_pack_start(GTK_BOX(columna), ayuda, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(columna), pestanas, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(ventana_dce), columna);
    gtk_widget_show_all(ventana_dce);
}
