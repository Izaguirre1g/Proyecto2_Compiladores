/*
 * Ventana "Reordenamiento" (ver include/ide_reordenamiento_ventana.h).
 * Solo muestra lo que calcula ide_reordenamiento.c con el módulo de Meibel.
 */
#include "ide_reordenamiento_ventana.h"

#include <string.h>

#include "ide_reordenamiento.h"

/* La ventana abierta (solo hay una) y lo que necesita para recalcular. */
static GtkWidget *ventana_ro = NULL;
static GtkWidget *contenido_ro = NULL;   /* Lo de abajo de los controles: se rehace al cambiarlos. */
static GtkWidget *columna_ro = NULL;
static GtkWidget *latencia_ro = NULL;
static GtkWidget *desenrollar_ro = NULL;
static gchar *texto_ro = NULL;           /* Copia del código del editor. */
static gchar *ruta_ro = NULL;

static void al_destruir_reordenamiento(GtkWidget *widget, gpointer datos)
{
    (void)widget;
    (void)datos;
    ventana_ro = NULL;
    contenido_ro = NULL;
    columna_ro = NULL;
    latencia_ro = NULL;
    desenrollar_ro = NULL;
    g_free(texto_ro);
    g_free(ruta_ro);
    texto_ro = NULL;
    ruta_ro = NULL;
}

/* ------------------------------------------------------------------ */
/* Pestaña "Bloques"                                                   */
/* ------------------------------------------------------------------ */

static void agregar_columna(GtkTreeView *vista, const char *titulo, int indice, int numero)
{
    GtkCellRenderer *celda = gtk_cell_renderer_text_new();
    if (numero) g_object_set(celda, "xalign", 1.0, NULL);   /* Números a la derecha. */
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(titulo, celda, "text",
                                                                      indice, NULL);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_append_column(vista, col);
}

static GtkWidget *pestana_bloques(const IdeReordenamiento *r)
{
    enum { COLUMNAS = 10 };
    GtkListStore *modelo = gtk_list_store_new(COLUMNAS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                              G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                              G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                              G_TYPE_STRING);
    for (size_t b = 0; b < r->resumen.cantidad_detalle; ++b) {
        const RoBloque *d = &r->resumen.detalle[b];
        char nombre[32], instr[32], movidas[32], ciclos[64];
        char raw[32], war[32], waw[32], mem[32], efecto[32];
        snprintf(nombre, sizeof nombre, "B%zu", b);
        snprintf(instr, sizeof instr, "%zu", d->fin - d->inicio);
        snprintf(movidas, sizeof movidas, "%zu", d->movidas);
        if (d->ciclos_despues != d->ciclos_antes)
            snprintf(ciclos, sizeof ciclos, "%zu → %zu", d->ciclos_antes, d->ciclos_despues);
        else
            snprintf(ciclos, sizeof ciclos, "%zu (sin cambio)", d->ciclos_antes);
        snprintf(raw, sizeof raw, "%zu", d->raw);
        snprintf(war, sizeof war, "%zu", d->war);
        snprintf(waw, sizeof waw, "%zu", d->waw);
        snprintf(mem, sizeof mem, "%zu", d->memoria);
        snprintf(efecto, sizeof efecto, "%zu", d->efecto);
        gtk_list_store_insert_with_values(modelo, NULL, -1, 0, nombre, 1, instr, 2, movidas,
                                          3, ciclos, 4, raw, 5, war, 6, waw, 7, mem, 8, efecto,
                                          9, d->movidas ? "reordenado" : "", -1);
    }
    GtkWidget *vista = gtk_tree_view_new_with_model(GTK_TREE_MODEL(modelo));
    g_object_unref(modelo);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(vista), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    const char *titulos[COLUMNAS] = {"Bloque", "Instr.", "Movidas", "Ciclos", "RAW", "WAR",
                                     "WAW", "Memoria", "Efectos", ""};
    for (int i = 0; i < COLUMNAS; ++i)
        agregar_columna(GTK_TREE_VIEW(vista), titulos[i], i, i >= 1 && i <= 8 && i != 3);

    GtkWidget *recuadro = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(recuadro), vista);
    return recuadro;
}

/* ------------------------------------------------------------------ */
/* Vistas de texto ("IR antes y después" y "Dependencias")             */
/* ------------------------------------------------------------------ */

/* Una vista de texto con las líneas: encabezados de bloque en negrita, lo
 * que se movió ('M') en verde, la ruta crítica ('C' y 'R') en negrita. */
static GtkWidget *vista_lineas(const IdeRoTexto *t, const char *titulo)
{
    GtkWidget *texto = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(texto), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(texto), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(texto), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(texto), 6);
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(texto));
    GdkRGBA verde = {0.18, 0.62, 0.25, 0.22};
    GtkTextTag *movida = gtk_text_buffer_create_tag(b, NULL, "background-rgba", &verde, NULL);
    GtkTextTag *negrita = gtk_text_buffer_create_tag(b, NULL, "weight", PANGO_WEIGHT_BOLD, NULL);

    GtkTextIter fin;
    for (size_t i = 0; i < t->cantidad; ++i) {
        const IdeRoLinea *l = &t->lineas[i];
        GtkTextTag *marca = l->tipo == 'M' ? movida
                          : (l->tipo == 'B' || l->tipo == 'C' || l->tipo == 'R') ? negrita : NULL;
        gtk_text_buffer_get_end_iter(b, &fin);
        /* Una línea en blanco antes de cada bloque (menos el primero). */
        if (l->tipo == 'B' && i > 0) {
            gtk_text_buffer_insert(b, &fin, "\n", -1);
            gtk_text_buffer_get_end_iter(b, &fin);
        }
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
    gtk_label_set_line_wrap(GTK_LABEL(etiqueta), TRUE);

    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(caja), etiqueta, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caja), recuadro, TRUE, TRUE, 0);
    return caja;
}

static GtkWidget *pestana_ir(const IdeReordenamiento *r)
{
    GtkWidget *division = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(division),
                    vista_lineas(&r->antes, "IR antes  (en verde y con *: lo que va a cambiar de lugar)"),
                    TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(division),
                    vista_lineas(&r->despues, "IR después  (cada instrucción conserva su número original)"),
                    TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(division), 430);
    return division;
}

static GtkWidget *pestana_dependencias(const IdeReordenamiento *r)
{
    if (r->dependencias.cantidad == 0) {
        GtkWidget *vacio = gtk_label_new("Ningún bloque cambió de orden con esta latencia: "
                                         "no hay nada que mostrar.");
        return vacio;
    }
    return vista_lineas(&r->dependencias,
                        "Cada línea «a → b» dice que a tiene que ir ANTES que b, por qué y con "
                        "cuántos ciclos de separación. En negrita: la ruta crítica, la cadena más "
                        "larga de dependencias.");
}

/* ------------------------------------------------------------------ */
/* Contenido (se rehace al cambiar los controles)                      */
/* ------------------------------------------------------------------ */

static GtkWidget *armar_contenido(unsigned latencia, int desenrollar)
{
    IdeReordenamiento r;
    int estado = ide_reordenamiento_analizar(texto_ro, strlen(texto_ro), ruta_ro, latencia,
                                             desenrollar, &r);

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
    if (r.estado == IDE_RO_CON_ERRORES) {
        gtk_label_set_text(GTK_LABEL(resumen), "Este archivo tiene errores: corrígelos "
                                               "(pestaña Errores) para reordenarlo.");
        ide_reordenamiento_liberar(&r);
        return caja;
    }
    if (r.estado == IDE_RO_SIN_IR) {
        gchar *m = g_strdup_printf("La IR todavía no soporta este programa (%s).", r.motivo);
        gtk_label_set_text(GTK_LABEL(resumen), m);
        g_free(m);
        ide_reordenamiento_liberar(&r);
        return caja;
    }

    const RoResumen *s = &r.resumen;
    GString *t = g_string_new(NULL);
    g_string_append_printf(t, "Ciclos estimados: <b>%zu → %zu</b>", s->ciclos_antes, s->ciclos_despues);
    if (s->ciclos_despues != s->ciclos_antes)
        g_string_append_printf(t, " (%+lld)", (long long)s->ciclos_despues - (long long)s->ciclos_antes);
    g_string_append_printf(t, "  ·  instrucciones movidas: <b>%zu de %zu</b>  ·  bloques "
                              "reordenados: <b>%zu de %zu</b>\n",
                           s->movidas, s->instrucciones, s->bloques_cambiados, s->bloques);
    g_string_append_printf(t, "Dependencias respetadas: RAW <b>%zu</b> · WAR <b>%zu</b> · WAW <b>%zu</b>"
                              " · memoria <b>%zu</b> · efectos <b>%zu</b> · control <b>%zu</b>",
                           s->raw, s->war, s->waw, s->memoria, s->efecto, s->control);
    gtk_label_set_markup(GTK_LABEL(resumen), t->str);
    g_string_free(t, TRUE);

    GtkWidget *pestanas = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_bloques(&r), gtk_label_new("Bloques"));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_ir(&r),
                             gtk_label_new("IR antes y después"));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_dependencias(&r),
                             gtk_label_new("Dependencias"));
    gtk_box_pack_start(GTK_BOX(caja), pestanas, TRUE, TRUE, 0);
    ide_reordenamiento_liberar(&r);
    return caja;
}

/* Al cambiar la latencia o la opción de desenrollar: recalcular. */
static void al_cambiar_control(GtkWidget *control, gpointer datos)
{
    (void)control;
    (void)datos;
    if (contenido_ro != NULL) gtk_widget_destroy(contenido_ro);
    contenido_ro = armar_contenido(
        (unsigned)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(latencia_ro)),
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(desenrollar_ro)));
    gtk_box_pack_start(GTK_BOX(columna_ro), contenido_ro, TRUE, TRUE, 0);
    gtk_widget_show_all(contenido_ro);
}

void ide_mostrar_reordenamiento(GtkWindow *padre, const char *texto, const char *ruta,
                                const char *nombre)
{
    if (ventana_ro != NULL) gtk_widget_destroy(ventana_ro);
    texto_ro = g_strdup(texto);
    ruta_ro = ruta ? g_strdup(ruta) : NULL;

    gchar *base = g_path_get_basename(nombre);
    gchar *titulo = g_strdup_printf("Reordenamiento — %s", base);
    ventana_ro = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ventana_ro), titulo);
    gtk_window_set_transient_for(GTK_WINDOW(ventana_ro), padre);
    gtk_window_set_default_size(GTK_WINDOW(ventana_ro), 980, 660);
    g_signal_connect(ventana_ro, "destroy", G_CALLBACK(al_destruir_reordenamiento), NULL);
    g_free(titulo);
    g_free(base);

    /* Arriba: qué es y los controles. */
    GtkWidget *ayuda = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ayuda),
        "El <b>reordenamiento</b> cambia el orden de las instrucciones <b>dentro</b> de cada "
        "bloque básico para separar una escritura de la lectura que la usa: el procesador VLIW "
        "tarda <i>latencia</i> ciclos en tener listo un valor y no tiene forwarding. Nunca rompe "
        "una dependencia RAW, WAR o WAW, ni cruza una llamada, un acceso a memoria o un salto. "
        "Un bloque solo cambia si el nuevo orden gasta menos ciclos. "
        "Módulo de Meibel: <tt>reordenamiento/</tt>.");
    gtk_label_set_xalign(GTK_LABEL(ayuda), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(ayuda), TRUE);

    RoOpciones defecto;
    ro_opciones_defecto(&defecto);
    latencia_ro = gtk_spin_button_new_with_range(1, 16, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(latencia_ro), defecto.latencia);
    desenrollar_ro = gtk_check_button_new_with_label("Desenrollar los ciclos primero (loop unrolling)");
    GtkWidget *fila = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(fila), gtk_label_new("Latencia (ciclos entre escribir y leer):"),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fila), latencia_ro, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fila), desenrollar_ro, FALSE, FALSE, 18);

    columna_ro = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(columna_ro), 8);
    gtk_box_pack_start(GTK_BOX(columna_ro), ayuda, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(columna_ro), fila, FALSE, FALSE, 0);
    contenido_ro = armar_contenido(defecto.latencia, 0);
    gtk_box_pack_start(GTK_BOX(columna_ro), contenido_ro, TRUE, TRUE, 0);
    g_signal_connect(latencia_ro, "value-changed", G_CALLBACK(al_cambiar_control), NULL);
    g_signal_connect(desenrollar_ro, "toggled", G_CALLBACK(al_cambiar_control), NULL);

    gtk_container_add(GTK_CONTAINER(ventana_ro), columna_ro);
    gtk_widget_show_all(ventana_ro);
}
