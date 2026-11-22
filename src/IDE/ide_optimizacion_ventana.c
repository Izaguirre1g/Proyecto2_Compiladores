/*
 * Ventana "Niveles -O0 / -O1 / -O2" (ver include/ide_optimizacion_ventana.h).
 * Solo muestra lo que calcula ide_optimizacion.c; la gráfica es la de las
 * Métricas (met_grafica_comparacion).
 */
#include "ide_optimizacion_ventana.h"

#include <math.h>
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

/* Varios widgets lado a lado con el mismo ancho. */
static GtkWidget *lado_a_lado(GtkWidget *const *widgets, int cantidad)
{
    GtkWidget *fila = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_set_homogeneous(GTK_BOX(fila), TRUE);
    for (int i = 0; i < cantidad; ++i) gtk_box_pack_start(GTK_BOX(fila), widgets[i], TRUE, TRUE, 0);
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

/* Los tres niveles, en el orden de las series de la gráfica. */
static const char *const NOMBRES[3] = {"-O0", "-O1", "-O2"};

static const IdeOptNivel *nivel_i(const IdeOptimizacion *r, int i)
{
    return i == 0 ? &r->o0 : i == 1 ? &r->o1 : &r->o2;
}

/* Cómo le fue a un nivel, en una línea. */
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

/* Una línea del veredicto: cómo quedó un nivel optimizado frente a -O0. */
static gchar *veredicto_de(const char *nombre, int mismo, const IdeOptNivel *n)
{
    if (mismo == 1)
        return g_markup_printf_escaped("<span foreground='#2e7d32'>✔ <b>%s</b> deja el mismo resultado de main que -O0.</span>", nombre);
    if (mismo == 0)
        return g_markup_printf_escaped("<span foreground='#c62828'>✘ <b>%s</b> deja un resultado DISTINTO de -O0.</span>", nombre);
    gchar *e = estado_de(nombre, n);
    gchar *t = g_strdup_printf("— No se compara %s: %s", nombre, e);
    g_free(e);
    return t;
}

/* El veredicto de arriba: ¿-O1 y -O2 hacen lo mismo que -O0? */
static GtkWidget *veredicto(const IdeOptimizacion *r)
{
    const char *titulo =
        r->mismo_resultado == 1 ? "<span foreground='#2e7d32' weight='bold' size='large'>✔ Los tres niveles dejan el mismo resultado</span>" :
        r->mismo_resultado == 0 ? "<span foreground='#c62828' weight='bold' size='large'>✘ Un nivel optimizado cambia el resultado</span>" :
                                  "<span weight='bold' size='large'>Resultado de main</span>";
    gchar *uno = veredicto_de("-O1", r->mismo_o1, &r->o1);
    gchar *dos = veredicto_de("-O2", r->mismo_o2, &r->o2);
    gchar *cero = r->o0.compilado ? NULL : estado_de("-O0", &r->o0);
    gchar *texto = g_strdup_printf("%s\n%s%s%s\n%s", titulo, cero ? cero : "", cero ? "\n" : "", uno, dos);
    GtkWidget *w = parrafo(texto);
    g_free(texto);
    g_free(uno);
    g_free(dos);
    g_free(cero);
    return w;
}

/* El dato de un nivel para la gráfica, o NAN si no lo tiene. */
static double dato(const IdeOptNivel *n, int simulado, double valor)
{
    if (!n->compilado || (simulado && n->simulacion.estado != MET_LISTO)) return NAN;
    return valor;
}

/* Pestaña "Comparación": la gráfica de los tres niveles. */
static GtkWidget *pestana_comparacion(const IdeOptimizacion *r)
{
    MetFilaComparacion filas[7];
    memset(filas, 0, sizeof filas);
    const char *nombres[7] = {"Ciclos", "Instrucciones ejecutadas", "IPC (instrucciones por ciclo)",
                              "Bundles solo NOP ejecutados", "Tamaño del programa (bundles)",
                              "Tamaño del código (bytes)", "Tiempo de compilación (ms)"};
    const int decimales[7] = {-1, -1, 3, -1, -1, -1, 3};
    for (int f = 0; f < 7; ++f) {
        filas[f].nombre = nombres[f];
        filas[f].decimales = decimales[f];
        filas[f].etiqueta_corta[1] = "-O1";
        filas[f].etiqueta_corta[2] = "-O2";
        for (int i = 0; i < 3; ++i) {
            const IdeOptNivel *n = nivel_i(r, i);
            const SimMetricasResumen *m = &n->simulacion.sim;
            double v[7] = {(double)m->ciclos, (double)m->instrucciones_utiles, m->ipc, (double)m->bundles_nop,
                           (double)n->bundles, (double)n->bundles * MET_BYTES_BUNDLE, n->tiempos.total};
            filas[f].valor[i] = dato(n, f < 4, v[f]);
        }
    }
    const char *leyendas[3] = {"-O0 (backend tradicional)", "-O1 (DCE + backend tradicional)",
                               "-O2 (IR optimizada + cadena VLIW)"};
    gchar *sub = g_strdup_printf("Cada fila tiene su propia escala. Debajo del nombre: cuánto cambia cada nivel "
                                 "respecto de -O0. Tiempo: mediana de %d compilaciones", MET_COMPILACIONES);
    GtkWidget *g = met_grafica_comparacion("-O0, -O1 y -O2", sub, leyendas, 3, filas, 7);
    g_free(sub);
    GtkWidget *recuadro = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(recuadro), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(recuadro), g);
    return recuadro;
}

/* Agrega una fila de 3 columnas (con marcado) a la tabla de pasadas. */
static void fila_pasada(GtkListStore *m, const char *pasada, const char *cambios, const char *que)
{
    gtk_list_store_insert_with_values(m, NULL, -1, 0, pasada, 1, cambios, 2, que, -1);
}

/* Pestaña "Qué hace cada nivel": el DCE de -O1 y cada pasada de -O2, en orden. */
static GtkWidget *pestana_pasadas(const IdeOptimizacion *r)
{
    GtkListStore *m = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    char t[200];
    fila_pasada(m, "<b>-O0</b>", "ninguna",
                "El backend tradicional genera el ensamblador directo del AST, sin optimizar.");

    if (r->o1.compilado || r->o1_codigo != NULL) {
        snprintf(t, sizeof t, "%zu instrucciones IR\n%zu sentencias borradas\n%zu reescritas",
                 r->o1_eliminadas_ir, r->o1_borradas, r->o1_cambiadas);
        fila_pasada(m, "<b>-O1</b>  Eliminación de código muerto\n(José)", t,
                    r->o1_cambio
                        ? "El DCE con variables vivas quita de la IR las asignaciones que nadie usa y el código "
                          "inalcanzable; ese resultado se lleva al código BAL (pestaña Código de -O1) y se "
                          "compila con el backend tradicional."
                        : r->o1_eliminadas_ir > 0
                        ? "El DCE quitó de la IR cosas que no están escritas en el código BAL (por "
                          "ejemplo, el salto que la IR pone después de un give dentro de un whether): "
                          "no hay sentencias que borrar, así que -O1 compila el mismo código que -O0."
                        : "El DCE no encontró nada que quitar: -O1 compila el mismo código que -O0.");
    } else {
        gchar *e = g_markup_escape_text(r->o1.motivo ? r->o1.motivo : "motivo desconocido", -1);
        fila_pasada(m, "<b>-O1</b>  Eliminación de código muerto\n(José)", "no se hizo", e);
        g_free(e);
    }

    if (!r->o2.compilado) {
        gchar *e = g_markup_escape_text(r->o2.motivo ? r->o2.motivo : "motivo desconocido", -1);
        fila_pasada(m, "<b>-O2</b>", "no se hizo", e);
        g_free(e);
    } else {
        snprintf(t, sizeof t, "%zu ciclo%s", r->unroll, r->unroll == 1 ? "" : "s");
        fila_pasada(m, "<b>-O2</b>  1. Loop unrolling\n(Meibel)", t,
                    "Repite el cuerpo de los ciclos para hacer menos saltos y dar más instrucciones "
                    "independientes al VLIW.");
        snprintf(t, sizeof t, "%zu versiones\n%zu WAR y %zu WAW quitadas", r->renombradas, r->war_eliminadas,
                 r->waw_eliminadas);
        fila_pasada(m, "<b>-O2</b>  2. Renombramiento", t,
                    "Da un nombre nuevo a cada valor que se vuelve a asignar: así desaparecen las "
                    "dependencias falsas WAR y WAW, y el reordenamiento tiene más libertad.");
        snprintf(t, sizeof t, "%zu instrucciones eliminadas", r->dce);
        fila_pasada(m, "<b>-O2</b>  3. Eliminación de código muerto\n(José)", t,
                    "El mismo DCE con variables vivas, ahora sobre la IR ya desenrollada y renombrada. "
                    "En -O2 las variables de main se tratan como resultado y no se tocan.");
        snprintf(t, sizeof t, "%zu instrucciones movidas", r->reordenadas);
        fila_pasada(m, "<b>-O2</b>  4. Reordenamiento\n(Meibel)", t,
                    "Dentro de cada bloque básico separa una escritura de la lectura que la usa, "
                    "respetando las dependencias RAW, WAR, WAW, de memoria y de efectos.");
        snprintf(t, sizeof t, "%zu ranuras\n%zu guardados, %zu cargas", r->spills, r->guardados_spill, r->cargas_spill);
        fila_pasada(m, "<b>-O2</b>  5. Spilling\n(Dylan)", t,
                    "Solo hay 3 registros temporales: los valores que no caben se guardan en la "
                    "pila (store) y se vuelven a cargar (load) cuando se necesitan.");
        snprintf(t, sizeof t, "%zu bundles\n%zu %s", r->o2.bundles, r->generaciones,
                 r->generaciones == 1 ? "generación" : "generaciones");
        fila_pasada(m, "<b>-O2</b>  6. Cadena física VLIW\n(Javier)", t,
                    "Pasa la IR a instrucciones del procesador, arma el grafo de flujo y de "
                    "dependencias, y calendariza en bundles de 4 slots. Se repite (generaciones) "
                    "hasta que las direcciones de los saltos ya no cambian.");
    }
    const char *titulos[3] = {"Nivel y pasada (en orden)", "Cambios", "Qué hace"};
    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(caja), parrafo(
        "Lo mismo que <tt>balc -O0</tt>, <tt>balc -O1</tt> y <tt>balc -O2</tt>. -O1 optimiza el "
        "<b>código BAL</b> y usa el backend tradicional; -O2 optimiza la <b>IR</b> "
        "(<tt>src/ir_vliw_programa.c</tt>) y genera el código con la cadena física VLIW."),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caja), tabla(m, titulos, 3), TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(caja), 6);
    return caja;
}

/* El valor de una variable de main en un nivel, buscada por nombre y por
 * cuántas veces apareció ese nombre antes (la misma regla de la comparación).
 * NULL si el nivel no la tiene. */
static const char *valor_en(const IdeOptNivel *n, const char *nombre, size_t repeticion)
{
    if (n->valores == NULL) return NULL;
    for (size_t j = 0; j < n->cantidad_variables; ++j) {
        if (strcmp(n->variables[j].nombre, nombre) != 0) continue;
        if (repeticion-- > 0) continue;
        return n->valores[j];
    }
    return NULL;
}

/* Pestaña "Resultado de main": cada variable de main en los tres niveles. */
static GtkWidget *pestana_resultado(const IdeOptimizacion *r)
{
    GtkListStore *m = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                         G_TYPE_STRING);
    /* La lista de variables: la de -O0 (tiene todas); si -O0 no compiló, la de -O2. */
    const IdeOptNivel *base = r->o0.cantidad_variables > 0 ? &r->o0 : &r->o2;
    for (size_t i = 0; i < base->cantidad_variables; ++i) {
        const char *nombre = base->variables[i].nombre;
        size_t repeticion = 0;
        for (size_t k = 0; k < i; ++k) repeticion += strcmp(base->variables[k].nombre, nombre) == 0;
        const char *v[3];
        int distinto = 0, comparadas = 0;
        for (int l = 0; l < 3; ++l) {
            v[l] = valor_en(nivel_i(r, l), nombre, repeticion);
            if (v[l] != NULL && v[0] != NULL && l > 0) {
                comparadas++;
                distinto |= strcmp(v[0], v[l]) != 0;
            }
        }
        gchar *celdas[3];
        for (int l = 0; l < 3; ++l) {
            const IdeOptNivel *n = nivel_i(r, l);
            if (v[l] != NULL) celdas[l] = g_markup_escape_text(v[l], -1);
            else if (l == 1 && n->simulacion.estado == MET_LISTO)
                celdas[l] = g_strdup("<i>eliminada (código muerto)</i>");
            else celdas[l] = g_strdup("—");
        }
        const char *igual = distinto ? "<span foreground='#c62828'><b>✘ distinto</b></span>"
                          : comparadas > 0 ? "<span foreground='#2e7d32'>✔ igual</span>" : "—";
        gchar *e_nombre = g_markup_escape_text(nombre, -1);
        gtk_list_store_insert_with_values(m, NULL, -1, 0, e_nombre, 1, celdas[0], 2, celdas[1],
                                          3, celdas[2], 4, igual, -1);
        g_free(e_nombre);
        for (int l = 0; l < 3; ++l) g_free(celdas[l]);
    }
    const char *titulos[5] = {"Variable de main", "-O0", "-O1", "-O2", "¿Igual a -O0?"};
    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(caja), parrafo(
        "BAL no tiene <tt>print</tt>: el resultado de un programa es el valor final de las variables de "
        "<tt>main</tt>. Se leen en la memoria del simulador en el ciclo en que main termina. Si -O1 y -O2 "
        "dejan los mismos valores, hacen lo mismo que -O0. Una variable que -O1 ya no tiene no es una "
        "diferencia: el DCE la borró porque nadie la lee (no es un resultado)."), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caja), tabla(m, titulos, 5), TRUE, TRUE, 0);
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

    /* Tarda un poco (compila cada nivel varias veces y simula los tres): cursor de espera. */
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
    gchar *titulo = g_strdup_printf("Niveles -O0 / -O1 / -O2 — %s", base);
    ventana_opt = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ventana_opt), titulo);
    gtk_window_set_transient_for(GTK_WINDOW(ventana_opt), padre);
    gtk_window_set_default_size(GTK_WINDOW(ventana_opt), 1040, 760);
    g_signal_connect(ventana_opt, "destroy", G_CALLBACK(al_destruir_optimizacion), NULL);
    g_free(titulo);
    g_free(base);

    GtkWidget *columna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(columna), 10);
    gtk_container_add(GTK_CONTAINER(ventana_opt), columna);
    gtk_box_pack_start(GTK_BOX(columna), parrafo(
        "Los tres niveles de optimización de <tt>balc</tt>. <b>-O0</b>: el backend tradicional, sin "
        "optimizar. <b>-O1</b>: el DCE quita el código muerto del código BAL y se compila con el mismo "
        "backend. <b>-O2</b>: la IR pasa por unrolling, renombramiento, DCE, reordenamiento y spilling, "
        "y la cadena física VLIW la calendariza. Los tres binarios se ejecutan en el simulador hasta que "
        "main termina."), FALSE, FALSE, 0);

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
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_pasadas(r), gtk_label_new("Qué hace cada nivel"));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_resultado(r), gtk_label_new("Resultado de main"));
    GtkWidget *asm_niveles[3];
    for (int i = 0; i < 3; ++i)
        asm_niveles[i] = con_titulo(NOMBRES[i], vista_de_texto(nivel_i(r, i)->ensamblador));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), lado_a_lado(asm_niveles, 3), gtk_label_new("Ensamblador"));
    if (r->o1_codigo != NULL)
        gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), vista_de_texto(r->o1_codigo), gtk_label_new("Código de -O1"));
    if (r->o2.compilado)
        gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_ir(r), gtk_label_new("IR de -O2"));
    gtk_box_pack_start(GTK_BOX(columna), pestanas, TRUE, TRUE, 0);
    gtk_widget_show_all(ventana_opt);
}
