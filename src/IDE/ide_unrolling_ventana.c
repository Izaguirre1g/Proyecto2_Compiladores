/*
 * Ventana "Loop unrolling" (ver include/ide_unrolling_ventana.h).
 * Solo muestra lo que calcula ide_unrolling.c con el módulo de Meibel.
 */
#include "ide_unrolling_ventana.h"

#include <string.h>

#include "ide_unrolling.h"

/* La ventana abierta (solo hay una) y lo que necesita para recalcular. */
static GtkWidget *ventana_lu = NULL;
static GtkWidget *contenido_lu = NULL;   /* Lo de abajo del factor: se rehace al cambiarlo. */
static gchar *texto_lu = NULL;           /* Copia del código del editor. */
static gchar *ruta_lu = NULL;

static void al_destruir_unrolling(GtkWidget *widget, gpointer datos)
{
    (void)widget;
    (void)datos;
    ventana_lu = NULL;
    contenido_lu = NULL;
    g_free(texto_lu);
    g_free(ruta_lu);
    texto_lu = NULL;
    ruta_lu = NULL;
}

/* ------------------------------------------------------------------ */
/* Pestaña "Ciclos"                                                    */
/* ------------------------------------------------------------------ */

static void agregar_columna(GtkTreeView *vista, const char *titulo, int indice)
{
    GtkCellRenderer *celda = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(titulo, celda, "text",
                                                                      indice, NULL);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_append_column(vista, col);
}

static GtkWidget *pestana_ciclos(const IdeUnrolling *u)
{
    GtkListStore *modelo = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                              G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    for (size_t i = 0; i < u->resumen.cantidad_detalle; ++i) {
        const LuDetalle *d = &u->resumen.detalle[i];
        char linea[24], condicion[120], paso[24], vueltas[32], decision[200];
        snprintf(linea, sizeof linea, "%zu", d->linea);
        if (d->iter[0] != '\0') {
            snprintf(condicion, sizeof condicion, "%s %s %s", d->iter, d->relacion, d->limite);
            snprintf(paso, sizeof paso, "%+lld", d->paso);
        } else {
            snprintf(condicion, sizeof condicion, "—");
            snprintf(paso, sizeof paso, "—");
        }
        if (d->vueltas_conocidas) snprintf(vueltas, sizeof vueltas, "%llu", d->vueltas);
        else snprintf(vueltas, sizeof vueltas, "%s", d->decision == LU_PARCIAL_VAR ? "?" : "—");
        ide_unrolling_decision(d, decision, sizeof decision);
        gtk_list_store_insert_with_values(modelo, NULL, -1, 0, d->etiqueta, 1, linea, 2, condicion,
                                          3, paso, 4, vueltas, 5, decision, -1);
    }
    GtkWidget *vista = gtk_tree_view_new_with_model(GTK_TREE_MODEL(modelo));
    g_object_unref(modelo);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(vista), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    const char *titulos[] = {"Ciclo", "Línea", "Condición", "Paso", "Vueltas", "Decisión"};
    for (int i = 0; i < 6; ++i) agregar_columna(GTK_TREE_VIEW(vista), titulos[i], i);

    GtkWidget *recuadro = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(recuadro), vista);
    if (u->resumen.cantidad_detalle == 0) {
        GtkWidget *vacio = gtk_label_new("Este código no tiene ciclos (whale o cycle).");
        gtk_widget_destroy(recuadro);
        return vacio;
    }
    return recuadro;
}

/* ------------------------------------------------------------------ */
/* Pestaña "IR antes y después"                                        */
/* ------------------------------------------------------------------ */

/* Una vista de texto con la foto de la IR: encabezados de bloque en
 * negrita y las instrucciones nuevas ('N') en verde. */
static GtkWidget *vista_ir(const LuVista *v, const char *titulo)
{
    GtkWidget *texto = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(texto), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(texto), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(texto), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(texto), 6);
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(texto));
    GdkRGBA verde = {0.18, 0.62, 0.25, 0.22};
    GtkTextTag *nueva = gtk_text_buffer_create_tag(b, NULL, "background-rgba", &verde, NULL);
    GtkTextTag *bloque = gtk_text_buffer_create_tag(b, NULL, "weight", PANGO_WEIGHT_BOLD, NULL);

    GtkTextIter fin;
    for (size_t i = 0; i < v->cantidad; ++i) {
        const LuLinea *l = &v->lineas[i];
        gtk_text_buffer_get_end_iter(b, &fin);
        GtkTextTag *marca = l->tipo == 'N' ? nueva : l->tipo == 'B' ? bloque : NULL;
        if (marca) gtk_text_buffer_insert_with_tags(b, &fin, l->texto, -1, marca, NULL);
        else gtk_text_buffer_insert(b, &fin, l->texto, -1);
        gtk_text_buffer_get_end_iter(b, &fin);
        gtk_text_buffer_insert(b, &fin, "\n", -1);
    }

    GtkWidget *recuadro = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(recuadro), texto);
    GtkWidget *etiqueta = gtk_label_new(NULL);
    gchar *marcado = g_markup_printf_escaped("<b>%s</b>", titulo);
    gtk_label_set_markup(GTK_LABEL(etiqueta), marcado);
    g_free(marcado);
    gtk_label_set_xalign(GTK_LABEL(etiqueta), 0.0f);

    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(caja), etiqueta, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caja), recuadro, TRUE, TRUE, 0);
    return caja;
}

static GtkWidget *pestana_ir(const IdeUnrolling *u)
{
    GtkWidget *division = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(division), vista_ir(&u->antes, "IR antes"), TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(division),
                    vista_ir(&u->despues, "IR después  (en verde y con +: lo que creó el unrolling)"),
                    TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(division), 430);
    return division;
}

/* ------------------------------------------------------------------ */
/* Contenido (se rehace al cambiar el factor)                          */
/* ------------------------------------------------------------------ */

/* Una fila de métricas: "Instrucciones IR  13 → 24 (+11)". */
static void metrica(GString *t, const char *nombre, size_t antes, size_t despues)
{
    long long dif = (long long)despues - (long long)antes;
    g_string_append_printf(t, "%s: <b>%zu → %zu</b>", nombre, antes, despues);
    if (dif != 0) g_string_append_printf(t, " (%+lld)", dif);
}

static GtkWidget *armar_contenido(unsigned factor)
{
    IdeUnrolling u;
    int estado = ide_unrolling_analizar(texto_lu, strlen(texto_lu), ruta_lu, factor, &u);

    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *resumen = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(resumen), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(resumen), TRUE);
    gtk_box_pack_start(GTK_BOX(caja), resumen, FALSE, FALSE, 0);

    if (estado != 0) {
        gtk_label_set_text(GTK_LABEL(resumen), "No se pudo analizar (falta de memoria o no se "
                                               "pudo escribir el archivo temporal).");
        return caja;
    }
    if (u.estado == IDE_LU_CON_ERRORES) {
        gtk_label_set_text(GTK_LABEL(resumen), "Este archivo tiene errores: corrígelos "
                                               "(pestaña Errores) para hacer el loop unrolling.");
        ide_unrolling_liberar(&u);
        return caja;
    }
    if (u.estado == IDE_LU_SIN_IR) {
        gchar *m = g_strdup_printf("La IR todavía no soporta este programa (%s).", u.motivo);
        gtk_label_set_text(GTK_LABEL(resumen), m);
        g_free(m);
        ide_unrolling_liberar(&u);
        return caja;
    }

    GString *t = g_string_new(NULL);
    g_string_append_printf(t, "Ciclos: <b>%zu</b> · desenrollados por completo: <b>%zu</b> · "
                              "parcialmente: <b>%zu</b> · sin cambios: <b>%zu</b>\n",
                           u.resumen.ciclos, u.resumen.totales, u.resumen.parciales,
                           u.resumen.rechazados);
    metrica(t, "Instrucciones IR", u.antes.instr, u.despues.instr);
    g_string_append(t, "  ·  ");
    metrica(t, "Bloques", u.antes.bloques, u.despues.bloques);
    g_string_append(t, "  ·  ");
    metrica(t, "Saltos", u.antes.saltos, u.despues.saltos);
    g_string_append(t, "  ·  ");
    metrica(t, "Etiquetas", u.antes.etiquetas, u.despues.etiquetas);
    gtk_label_set_markup(GTK_LABEL(resumen), t->str);
    g_string_free(t, TRUE);

    GtkWidget *pestanas = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_ciclos(&u), gtk_label_new("Ciclos"));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_ir(&u),
                             gtk_label_new("IR antes y después"));
    gtk_box_pack_start(GTK_BOX(caja), pestanas, TRUE, TRUE, 0);
    ide_unrolling_liberar(&u);
    return caja;
}

/* Al cambiar el factor k: recalcular y rehacer el contenido. */
static void al_cambiar_factor(GtkSpinButton *spin, gpointer datos)
{
    GtkBox *columna = datos;
    if (contenido_lu != NULL) gtk_widget_destroy(contenido_lu);
    contenido_lu = armar_contenido((unsigned)gtk_spin_button_get_value_as_int(spin));
    gtk_box_pack_start(columna, contenido_lu, TRUE, TRUE, 0);
    gtk_widget_show_all(contenido_lu);
}

void ide_mostrar_unrolling(GtkWindow *padre, const char *texto, const char *ruta,
                           const char *nombre)
{
    if (ventana_lu != NULL) gtk_widget_destroy(ventana_lu);
    texto_lu = g_strdup(texto);
    ruta_lu = ruta ? g_strdup(ruta) : NULL;

    gchar *base = g_path_get_basename(nombre);
    gchar *titulo = g_strdup_printf("Loop unrolling — %s", base);
    ventana_lu = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ventana_lu), titulo);
    gtk_window_set_transient_for(GTK_WINDOW(ventana_lu), padre);
    gtk_window_set_default_size(GTK_WINDOW(ventana_lu), 900, 640);
    g_signal_connect(ventana_lu, "destroy", G_CALLBACK(al_destruir_unrolling), NULL);
    g_free(titulo);
    g_free(base);

    /* Arriba: qué es y el factor k. */
    GtkWidget *ayuda = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ayuda),
        "El <b>loop unrolling</b> copia el cuerpo de un ciclo varias veces para hacer menos "
        "saltos. Si el número de vueltas N se conoce y es pequeño, el ciclo se desenrolla por "
        "<b>completo</b>; si no, se desenrolla <b>parcialmente</b> con factor k (k copias por "
        "vuelta y lo que sobra aparte). Módulo de Meibel: <tt>loop_unrolling/</tt>.");
    gtk_label_set_xalign(GTK_LABEL(ayuda), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(ayuda), TRUE);

    LuOpciones defecto;
    lu_opciones_defecto(&defecto);
    GtkWidget *factor = gtk_spin_button_new_with_range(2, 16, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(factor), defecto.factor);
    GtkWidget *fila = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(fila), gtk_label_new("Factor k del desenrollado parcial:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fila), factor, FALSE, FALSE, 0);

    GtkWidget *columna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(columna), 8);
    gtk_box_pack_start(GTK_BOX(columna), ayuda, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(columna), fila, FALSE, FALSE, 0);
    contenido_lu = armar_contenido(defecto.factor);
    gtk_box_pack_start(GTK_BOX(columna), contenido_lu, TRUE, TRUE, 0);
    g_signal_connect(factor, "value-changed", G_CALLBACK(al_cambiar_factor), columna);

    gtk_container_add(GTK_CONTAINER(ventana_lu), columna);
    gtk_widget_show_all(ventana_lu);
}
