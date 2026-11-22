/*
 * Ventana "-O0 / -O2" (ver include/ide_optimizacion_ventana.h).
 * Solo muestra lo que calcula ide_optimizacion.c; la gráfica es la de las
 * Métricas (met_grafica_comparacion).
 */
#include "ide_optimizacion_ventana.h"

#include <string.h>

#include "ide_optimizacion.h"
#include "metricas_ventana.h"

/* La ventana abierta (solo hay una) y sus datos: NULL si está cerrada. */
static GtkWidget *ventana_opt = NULL;
static IdeOptimizacion *datos_opt = NULL;

/* Al cerrarla se liberan los datos y se olvida. */
static void al_destruir_optimizacion(GtkWidget *widget, gpointer datos)
{
    (void)widget;
    (void)datos;
    if (datos_opt != NULL) {
        ide_optimizacion_liberar(datos_opt);
        g_free(datos_opt);
        datos_opt = NULL;
    }
    ventana_opt = NULL;
}

/* Un texto que ocupa el ancho y se parte en líneas (con marcado Pango). */
static GtkWidget *parrafo(const char *marcado)
{
    GtkWidget *etiqueta = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(etiqueta), marcado);
    gtk_label_set_xalign(GTK_LABEL(etiqueta), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(etiqueta), TRUE);
    return etiqueta;
}

/* Un texto de solo lectura en letra monoespaciada, con barra de desplazamiento. */
static GtkWidget *vista_de_texto(const char *texto)
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

/* Un título en negrita encima de un widget. */
static GtkWidget *con_titulo(const char *titulo, GtkWidget *contenido)
{
    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gchar *marcado = g_markup_printf_escaped("<b>%s</b>", titulo);
    gtk_box_pack_start(GTK_BOX(caja), parrafo(marcado), FALSE, FALSE, 0);
    g_free(marcado);
    gtk_box_pack_start(GTK_BOX(caja), contenido, TRUE, TRUE, 0);
    return caja;
}

/* Dos widgets lado a lado con el mismo ancho. */
static GtkWidget *lado_a_lado(GtkWidget *izquierda, GtkWidget *derecha)
{
    GtkWidget *fila = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_set_homogeneous(GTK_BOX(fila), TRUE);
    gtk_box_pack_start(GTK_BOX(fila), izquierda, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(fila), derecha, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(fila), 6);
    return fila;
}

/* Una tabla de solo lectura con "columnas" columnas de texto. */
static GtkWidget *tabla(GtkListStore *modelo, const char *const *titulos, int columnas)
{
    GtkWidget *vista = gtk_tree_view_new_with_model(GTK_TREE_MODEL(modelo));
    g_object_unref(modelo);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(vista), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    for (int i = 0; i < columnas; ++i) {
        GtkCellRenderer *celda = gtk_cell_renderer_text_new();
        g_object_set(celda, "wrap-mode", PANGO_WRAP_WORD, "wrap-width", i == columnas - 1 ? 420 : 200, NULL);
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(titulos[i], celda, "markup", i, NULL);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(vista), col);
    }
    GtkWidget *recuadro = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(recuadro), vista);
    return recuadro;
}

/* Cómo le fue a un nivel, en una línea (para cuando no hay gráfica). */
static gchar *estado_de(const char *nombre, const IdeOptNivel *n)
{
    if (!n->compilado)
        return g_markup_printf_escaped("<b>%s</b> no se pudo compilar: %s", nombre,
                                       n->motivo ? n->motivo : "motivo desconocido");
    if (n->simulacion.estado != MET_LISTO)
        return g_markup_printf_escaped("<b>%s</b> compiló (%zu bundles), pero en el simulador: %s", nombre,
                                       n->bundles, n->simulacion.motivo ? n->simulacion.motivo : "no terminó");
    return g_markup_printf_escaped("<b>%s</b>: %llu ciclos, %zu bundles.", nombre,
                                   (unsigned long long)n->simulacion.sim.ciclos, n->bundles);
}

/* El veredicto de arriba: ¿-O2 hace lo mismo que -O0? */
static GtkWidget *veredicto(const IdeOptimizacion *r)
{
    gchar *texto;
    if (r->mismo_resultado == 1) {
        texto = g_markup_printf_escaped(
            "<span foreground='#2e7d32' weight='bold' size='large'>✔ -O2 deja el mismo resultado que -O0</span>\n"
            "Las %zu variables de main terminan con los mismos valores en los dos niveles "
            "(pestaña Resultado de main): la optimización respetó la regla de oro.",
            r->o0.cantidad_variables);
    } else if (r->mismo_resultado == 0) {
        texto = g_markup_printf_escaped(
            "<span foreground='#c62828' weight='bold' size='large'>✘ -O2 deja un resultado distinto de -O0</span>\n"
            "Alguna variable de main termina con otro valor (pestaña Resultado de main).");
    } else {
        gchar *a = estado_de("-O0", &r->o0), *b = estado_de("-O2", &r->o2);
        texto = g_strdup_printf("<b>No se puede comparar el resultado de main.</b>\n%s\n%s", a, b);
        g_free(a);
        g_free(b);
    }
    GtkWidget *w = parrafo(texto);
    g_free(texto);
    return w;
}

/* Pestaña "Comparación": la gráfica de los dos niveles. */
static GtkWidget *pestana_comparacion(const IdeOptimizacion *r)
{
    const IdeOptNivel *a = &r->o0, *b = &r->o2;
    int simulados = a->simulacion.estado == MET_LISTO && b->simulacion.estado == MET_LISTO;
    MetFilaComparacion filas[7];
    int n = 0;
    if (simulados) {
        const SimMetricasResumen *x = &a->simulacion.sim, *y = &b->simulacion.sim;
        filas[n++] = (MetFilaComparacion){"Ciclos", {(double)x->ciclos, (double)y->ciclos}, -1};
        filas[n++] = (MetFilaComparacion){"Instrucciones ejecutadas",
                                          {(double)x->instrucciones_utiles, (double)y->instrucciones_utiles}, -1};
        filas[n++] = (MetFilaComparacion){"IPC (instrucciones por ciclo)", {x->ipc, y->ipc}, 3};
        filas[n++] = (MetFilaComparacion){"Bundles solo NOP ejecutados",
                                          {(double)x->bundles_nop, (double)y->bundles_nop}, -1};
    }
    if (a->compilado && b->compilado) {
        filas[n++] = (MetFilaComparacion){"Tamaño del programa (bundles)", {(double)a->bundles, (double)b->bundles}, -1};
        filas[n++] = (MetFilaComparacion){"Tamaño del código (bytes)",
                                          {(double)a->bundles * MET_BYTES_BUNDLE, (double)b->bundles * MET_BYTES_BUNDLE}, -1};
        filas[n++] = (MetFilaComparacion){"Tiempo de compilación (ms)", {a->tiempos.total, b->tiempos.total}, 3};
    }
    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    if (n > 0) {
        gchar *sub = g_strdup_printf("Cada fila tiene su propia escala. Debajo del nombre: cuánto cambia de -O0 a -O2. "
                                     "Tiempo: mediana de %d compilaciones", MET_COMPILACIONES);
        GtkWidget *g = met_grafica_comparacion("-O0 contra -O2", sub, "-O0 (backend tradicional)",
                                               "-O2 (IR optimizada + cadena VLIW)", filas, n);
        g_free(sub);
        gtk_box_pack_start(GTK_BOX(caja), g, TRUE, TRUE, 0);
    }
    if (!simulados) {
        gchar *x = estado_de("-O0", a), *y = estado_de("-O2", b);
        gchar *texto = g_strdup_printf("%s\n%s", x, y);
        GtkWidget *nota = parrafo(texto);
        gtk_widget_set_margin_start(nota, 8);
        gtk_box_pack_start(GTK_BOX(caja), nota, FALSE, FALSE, 6);
        g_free(texto);
        g_free(x);
        g_free(y);
    }
    GtkWidget *recuadro = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(recuadro), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(recuadro), caja);
    return recuadro;
}

/* Agrega una fila de 3 columnas (con marcado) a la tabla de pasadas. */
static void fila_pasada(GtkListStore *m, const char *pasada, const char *cambios, const char *que)
{
    gtk_list_store_insert_with_values(m, NULL, -1, 0, pasada, 1, cambios, 2, que, -1);
}

/* Pestaña "Pasadas de -O2": qué hizo cada optimización, en su orden. */
static GtkWidget *pestana_pasadas(const IdeOptimizacion *r)
{
    if (!r->o2.compilado) {
        gchar *t = g_markup_printf_escaped("-O2 no pudo compilar este programa: %s",
                                           r->o2.motivo ? r->o2.motivo : "motivo desconocido");
        GtkWidget *w = parrafo(t);
        g_free(t);
        gtk_widget_set_valign(w, GTK_ALIGN_START);
        gtk_widget_set_margin_start(w, 8);
        gtk_widget_set_margin_top(w, 8);
        return w;
    }
    GtkListStore *m = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    char t[160];
    snprintf(t, sizeof t, "%zu ciclo%s", r->unroll, r->unroll == 1 ? "" : "s");
    fila_pasada(m, "<b>1. Loop unrolling</b>\n(Meibel)", t,
                "Repite el cuerpo de los ciclos para hacer menos saltos y dar más instrucciones "
                "independientes al VLIW.");
    snprintf(t, sizeof t, "%zu versiones\n%zu WAR y %zu WAW quitadas", r->renombradas, r->war_eliminadas,
             r->waw_eliminadas);
    fila_pasada(m, "<b>2. Renombramiento</b>", t,
                "Da un nombre nuevo a cada valor que se vuelve a asignar: así desaparecen las "
                "dependencias falsas WAR y WAW, y el reordenamiento tiene más libertad.");
    snprintf(t, sizeof t, "%zu instrucciones eliminadas", r->dce);
    fila_pasada(m, "<b>3. Eliminación de código muerto</b>\n(José)", t,
                "El DCE con variables vivas (src/DCE/dce.c). Quita asignaciones que nadie usa y "
                "código inalcanzable; nunca llamadas, escrituras ni operaciones que pueden fallar.");
    snprintf(t, sizeof t, "%zu instrucciones movidas", r->reordenadas);
    fila_pasada(m, "<b>4. Reordenamiento</b>\n(Meibel)", t,
                "Dentro de cada bloque básico separa una escritura de la lectura que la usa, "
                "respetando las dependencias RAW, WAR, WAW, de memoria y de efectos.");
    snprintf(t, sizeof t, "%zu ranuras\n%zu guardados, %zu cargas", r->spills, r->guardados_spill, r->cargas_spill);
    fila_pasada(m, "<b>5. Spilling</b>\n(Dylan)", t,
                "Solo hay 3 registros temporales: los valores que no caben se guardan en la "
                "pila (store) y se vuelven a cargar (load) cuando se necesitan.");
    snprintf(t, sizeof t, "%zu bundles\n%zu %s", r->o2.bundles, r->generaciones,
             r->generaciones == 1 ? "generación" : "generaciones");
    fila_pasada(m, "<b>6. Cadena física VLIW</b>\n(Javier)", t,
                "Pasa la IR a instrucciones del procesador, arma el grafo de flujo y de "
                "dependencias, y calendariza en bundles de 4 slots. Se repite (generaciones) "
                "hasta que las direcciones de los saltos ya no cambian.");
    const char *titulos[3] = {"Pasada (en orden)", "Cambios", "Qué hace"};
    GtkWidget *t_w = tabla(m, titulos, 3);
    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(caja), parrafo(
        "Las mismas pasadas que <tt>balc -O2</tt> (<tt>src/ir_vliw_programa.c</tt>), sobre la IR de "
        "tres direcciones. <tt>-O0</tt> no aplica ninguna: genera el ensamblador directo del AST."),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caja), t_w, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(caja), 6);
    return caja;
}

/* Pestaña "Resultado de main": cada variable de main en los dos niveles. */
static GtkWidget *pestana_resultado(const IdeOptimizacion *r)
{
    GtkListStore *m = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    const IdeOptNivel *a = &r->o0, *b = &r->o2;
    size_t n = a->cantidad_variables > b->cantidad_variables ? a->cantidad_variables : b->cantidad_variables;
    for (size_t i = 0; i < n; ++i) {
        const char *nombre = i < a->cantidad_variables ? a->variables[i].nombre : b->variables[i].nombre;
        const char *va = a->valores && i < a->cantidad_variables ? a->valores[i] : "—";
        const char *vb = b->valores && i < b->cantidad_variables ? b->valores[i] : "—";
        const char *igual = a->valores && b->valores ? (strcmp(va, vb) == 0
                                ? "<span foreground='#2e7d32'>✔ igual</span>"
                                : "<span foreground='#c62828'><b>✘ distinto</b></span>") : "—";
        gchar *e_nombre = g_markup_escape_text(nombre, -1);
        gchar *e_a = g_markup_escape_text(va, -1), *e_b = g_markup_escape_text(vb, -1);
        gtk_list_store_insert_with_values(m, NULL, -1, 0, e_nombre, 1, e_a, 2, e_b, 3, igual, -1);
        g_free(e_nombre);
        g_free(e_a);
        g_free(e_b);
    }
    const char *titulos[4] = {"Variable de main", "-O0", "-O2", "¿Igual?"};
    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(caja), parrafo(
        "BAL no tiene <tt>print</tt>: el resultado de un programa es el valor final de las variables de "
        "<tt>main</tt>. Se leen en la memoria del simulador en el ciclo en que main termina, en el marco "
        "de main (tope de la pila − marco + desplazamiento). Si -O2 deja los mismos valores, hace lo "
        "mismo que -O0."), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caja), tabla(m, titulos, 4), TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(caja), 6);
    return caja;
}

/* Pestaña "IR de -O2": la IR en cada etapa. */
static GtkWidget *pestana_ir(const IdeOptimizacion *r)
{
    GtkWidget *sub = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(sub), vista_de_texto(r->ir_antes),
                             gtk_label_new("Antes de optimizar"));
    gtk_notebook_append_page(GTK_NOTEBOOK(sub), vista_de_texto(r->ir_despues),
                             gtk_label_new("Optimizada (pasadas 1 a 4)"));
    gtk_notebook_append_page(GTK_NOTEBOOK(sub), vista_de_texto(r->ir_spilling),
                             gtk_label_new("Con spilling (pasada 5)"));
    return sub;
}

void ide_mostrar_optimizacion(GtkWindow *padre, const char *texto, const char *ruta, const char *nombre)
{
    if (ventana_opt != NULL) gtk_widget_destroy(ventana_opt);

    /* Tarda un poco (compila cada nivel varias veces y simula los dos): cursor de espera. */
    GdkWindow *gdk_padre = gtk_widget_get_window(GTK_WIDGET(padre));
    GdkCursor *espera = gdk_padre ? gdk_cursor_new_for_display(gdk_window_get_display(gdk_padre), GDK_WATCH) : NULL;
    if (gdk_padre) {
        gdk_window_set_cursor(gdk_padre, espera);
        while (gtk_events_pending()) gtk_main_iteration();
    }
    datos_opt = g_new0(IdeOptimizacion, 1);
    int estado = ide_optimizacion_analizar(texto, strlen(texto), ruta, datos_opt);
    if (gdk_padre) gdk_window_set_cursor(gdk_padre, NULL);
    if (espera) g_object_unref(espera);

    gchar *base = g_path_get_basename(nombre);
    gchar *titulo = g_strdup_printf("-O0 / -O2 — %s", base);
    ventana_opt = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ventana_opt), titulo);
    gtk_window_set_transient_for(GTK_WINDOW(ventana_opt), padre);
    gtk_window_set_default_size(GTK_WINDOW(ventana_opt), 980, 720);
    g_signal_connect(ventana_opt, "destroy", G_CALLBACK(al_destruir_optimizacion), NULL);
    g_free(titulo);
    g_free(base);

    GtkWidget *columna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(columna), 10);
    gtk_container_add(GTK_CONTAINER(ventana_opt), columna);
    gtk_box_pack_start(GTK_BOX(columna), parrafo(
        "Los dos niveles de optimización de <tt>balc</tt>. <b>-O0</b>: el backend tradicional, sin "
        "optimizar (el mismo del botón Ensamblador). <b>-O2</b>: la IR pasa por unrolling, "
        "renombramiento, DCE, reordenamiento y spilling, y la cadena física VLIW la calendariza. "
        "Los dos binarios se ejecutan en el simulador hasta que main termina."), FALSE, FALSE, 0);

    if (estado != 0 || datos_opt->estado == IDE_OPT_CON_ERRORES) {
        gtk_box_pack_start(GTK_BOX(columna), parrafo(estado != 0
            ? "<b>No se pudo hacer la comparación</b> (falta de memoria o archivo temporal)."
            : "<b>Este archivo tiene errores:</b> corrígelos (pestaña Errores) para compararlo."),
            FALSE, FALSE, 0);
        gtk_widget_show_all(ventana_opt);
        return;
    }
    const IdeOptimizacion *r = datos_opt;
    gtk_box_pack_start(GTK_BOX(columna), veredicto(r), FALSE, FALSE, 4);

    GtkWidget *pestanas = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_comparacion(r), gtk_label_new("Comparación"));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_pasadas(r), gtk_label_new("Pasadas de -O2"));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_resultado(r), gtk_label_new("Resultado de main"));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas),
                             lado_a_lado(con_titulo("-O0", vista_de_texto(r->o0.ensamblador)),
                                         con_titulo("-O2", vista_de_texto(r->o2.ensamblador))),
                             gtk_label_new("Ensamblador"));
    if (r->o2.compilado)
        gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_ir(r), gtk_label_new("IR de -O2"));
    gtk_box_pack_start(GTK_BOX(columna), pestanas, TRUE, TRUE, 0);
    gtk_widget_show_all(ventana_opt);
}
