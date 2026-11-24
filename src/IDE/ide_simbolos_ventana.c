/*
 * Ventana "Tabla de símbolos" (ver include/ide_simbolos_ventana.h).
 * Solo muestra lo que ya copió ide_simbolos.c de la SimTabla; no calcula nada.
 */
#include "ide_simbolos_ventana.h"

#include <string.h>

/* La ventana abierta (solo hay una): NULL si está cerrada. */
static GtkWidget *ventana_simbolos = NULL;

/* Al cerrarla se olvida, para que el botón la vuelva a crear. */
static void al_destruir(GtkWidget *widget, gpointer datos)
{
    (void)widget;
    (void)datos;
    ventana_simbolos = NULL;
}

/* Agrega una columna de texto a una tabla (GtkTreeView). */
static void agregar_columna(GtkWidget *vista, const char *titulo, int indice, gboolean derecha)
{
    GtkCellRenderer *celda = gtk_cell_renderer_text_new();
    if (derecha) g_object_set(celda, "xalign", 1.0, NULL);
    GtkTreeViewColumn *columna =
        gtk_tree_view_column_new_with_attributes(titulo, celda, "text", indice, NULL);
    gtk_tree_view_column_set_resizable(columna, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(vista), columna);
}

/* Pone una tabla dentro de un recuadro con barras de desplazamiento. */
static GtkWidget *con_desplazamiento(GtkWidget *vista)
{
    GtkWidget *recuadro = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(recuadro), vista);
    return recuadro;
}

/* Las columnas de la pestaña Símbolos: las mismas del archivo .simbolos.md
 * que escribe "balc -m" (exportar_simbolos_md, cli.c). */
enum { C_NOMBRE, C_CLASE, C_TIPO, C_AMBITO, C_NIVEL, C_ALMACEN, C_BYTES,
       C_DESPLAZAMIENTO, C_DIRECCION, C_COLUMNAS };

/* La sección "Memoria" de abajo, como en el .simbolos.md. */
static GtkWidget *seccion_memoria(const IdeSimbolos *s)
{
    GString *t = g_string_new("<span size=\"large\"><b>Memoria</b></span>\n");
    if (s->con_memoria) {
        g_string_append_printf(t,
            "  •  Funciones: %zu\n"
            "  •  Marco maximo: %zu bytes\n"
            "  •  Codigo desde: <tt>0x0000</tt>\n"
            "  •  GP: <tt>0x%04zx</tt>\n"
            "  •  Memoria global: %zu bytes\n"
            "  •  SP: <tt>0x%05x</tt>",
            s->funciones, s->marco_maximo, s->gp, s->bytes_globales, s->sp);
        if (!s->con_codigo)
            g_string_append(t, "\n<small>El backend no pudo generar el código: las "
                               "direcciones de código quedan pendientes.</small>");
    } else {
        g_string_append(t, "  La memoria no se pudo asignar (el backend no soporta algo de "
                           "este programa): almacén, desplazamiento y direcciones quedan "
                           "pendientes.");
    }
    GtkWidget *e = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(e), t->str);
    gtk_label_set_xalign(GTK_LABEL(e), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(e), TRUE);
    gtk_widget_set_margin_start(e, 10);
    gtk_widget_set_margin_bottom(e, 8);
    g_string_free(t, TRUE);
    return e;
}

/* Pestaña 1: la tabla (una fila por símbolo, en el orden en que se
 * declararon) y debajo la sección Memoria. */
static GtkWidget *pestana_simbolos(const IdeSimbolos *s)
{
    GtkListStore *datos = gtk_list_store_new(C_COLUMNAS, G_TYPE_STRING, G_TYPE_STRING,
                                             G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                             G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                             G_TYPE_STRING);
    for (size_t i = 0; i < s->cantidad; ++i) {
        const IdeSimbolo *x = &s->simbolos[i];
        char nivel[16], bytes[24], desplazamiento[32], direccion[16];

        snprintf(nivel, sizeof nivel, "%u", x->nivel);
        snprintf(bytes, sizeof bytes, "%zu", x->bytes);
        /* Desde gp (globales) o desde sp (locales y parámetros); el resto, pendiente. */
        if (x->con_desplazamiento)
            snprintf(desplazamiento, sizeof desplazamiento, "%ld", x->desplazamiento);
        else
            snprintf(desplazamiento, sizeof desplazamiento, "pendiente");
        /* Solo las funciones tienen dirección de código (la da el enlazador). */
        if (x->con_direccion)
            snprintf(direccion, sizeof direccion, "0x%04x", x->direccion);
        else
            snprintf(direccion, sizeof direccion, "pendiente");

        GtkTreeIter fila;
        gtk_list_store_append(datos, &fila);
        gtk_list_store_set(datos, &fila, C_NOMBRE, x->nombre, C_CLASE, x->clase,
                           C_TIPO, x->tipo, C_AMBITO, x->ambito, C_NIVEL, nivel,
                           C_ALMACEN, x->almacen, C_BYTES, bytes,
                           C_DESPLAZAMIENTO, desplazamiento, C_DIRECCION, direccion, -1);
    }

    GtkWidget *vista = gtk_tree_view_new_with_model(GTK_TREE_MODEL(datos));
    g_object_unref(datos);   /* La vista se queda con su propia referencia. */
    agregar_columna(vista, "Nombre", C_NOMBRE, FALSE);
    agregar_columna(vista, "Clase", C_CLASE, FALSE);
    agregar_columna(vista, "Tipo", C_TIPO, FALSE);
    agregar_columna(vista, "Ambito", C_AMBITO, FALSE);
    agregar_columna(vista, "Nivel", C_NIVEL, TRUE);
    agregar_columna(vista, "Almacen", C_ALMACEN, FALSE);
    agregar_columna(vista, "Bytes", C_BYTES, TRUE);
    agregar_columna(vista, "Desplazamiento", C_DESPLAZAMIENTO, TRUE);
    agregar_columna(vista, "Direccion de codigo", C_DIRECCION, FALSE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(vista), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);

    /* Arriba el título, en medio la tabla y abajo la memoria. */
    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *titulo = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(titulo), "<span size=\"x-large\"><b>Tabla de simbolos</b></span>");
    gtk_label_set_xalign(GTK_LABEL(titulo), 0.0);
    gtk_widget_set_margin_start(titulo, 10);
    gtk_widget_set_margin_top(titulo, 6);
    gtk_box_pack_start(GTK_BOX(caja), titulo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caja), con_desplazamiento(vista), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(caja), seccion_memoria(s), FALSE, FALSE, 0);
    return caja;
}

/* Pestaña 2: los ámbitos, en el orden en que se crearon. */
static GtkWidget *pestana_ambitos(const IdeSimbolos *s)
{
    GtkListStore *datos = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING,
                                             G_TYPE_STRING, G_TYPE_STRING);
    for (size_t i = 0; i < s->cantidad_ambitos; ++i) {
        const IdeAmbito *a = &s->ambitos[i];
        char nivel[16], simbolos[24];
        snprintf(nivel, sizeof nivel, "%u", a->nivel);
        snprintf(simbolos, sizeof simbolos, "%zu", a->simbolos);
        GtkTreeIter fila;
        gtk_list_store_append(datos, &fila);
        gtk_list_store_set(datos, &fila, 0, a->nombre, 1, nivel,
                           2, a->padre[0] ? a->padre : "—", 3, simbolos, -1);
    }
    GtkWidget *vista = gtk_tree_view_new_with_model(GTK_TREE_MODEL(datos));
    g_object_unref(datos);
    agregar_columna(vista, "Ámbito", 0, FALSE);
    agregar_columna(vista, "Nivel", 1, TRUE);
    agregar_columna(vista, "Ámbito padre", 2, FALSE);
    agregar_columna(vista, "Símbolos en su tabla hash", 3, TRUE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(vista), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    return con_desplazamiento(vista);
}

/* Pestaña 3: el listado de sim_imprimir, en letra monoespaciada. */
static GtkWidget *pestana_listado(const char *texto)
{
    GtkWidget *vista = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(vista), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(vista), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(vista), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(vista), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(vista), 6);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(vista)),
                             texto != NULL ? texto : "", -1);
    return con_desplazamiento(vista);
}

/* Una línea de texto arriba de la ventana (con márgenes y que se ajusta). */
static GtkWidget *etiqueta(const char *marcado)
{
    GtkWidget *e = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(e), marcado);
    gtk_label_set_xalign(GTK_LABEL(e), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(e), TRUE);
    gtk_widget_set_margin_start(e, 10);
    gtk_widget_set_margin_end(e, 10);
    gtk_widget_set_margin_top(e, 8);
    return e;
}

void ide_mostrar_simbolos(GtkWindow *padre, const char *texto, const char *ruta,
                          const char *nombre)
{
    /* Si ya estaba abierta, se cierra: el resultado nuevo la reemplaza. */
    if (ventana_simbolos != NULL) gtk_widget_destroy(ventana_simbolos);

    ventana_simbolos = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gchar *titulo = g_strdup_printf("Tabla de símbolos — %s", nombre);
    gtk_window_set_title(GTK_WINDOW(ventana_simbolos), titulo);
    g_free(titulo);
    gtk_window_set_transient_for(GTK_WINDOW(ventana_simbolos), padre);
    gtk_window_set_default_size(GTK_WINDOW(ventana_simbolos), 1180, 600);
    g_signal_connect(ventana_simbolos, "destroy", G_CALLBACK(al_destruir), NULL);

    GtkWidget *columna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(ventana_simbolos), columna);

    IdeSimbolos s;
    int estado = ide_simbolos_generar(texto, strlen(texto), ruta, &s);

    if (estado != 0) {
        gtk_box_pack_start(GTK_BOX(columna),
                           etiqueta("No se pudo analizar (falta de memoria o no se pudo "
                                    "escribir el archivo temporal)."), FALSE, FALSE, 0);
    } else if (s.estado == IDE_SIM_CON_ERRORES) {
        gtk_box_pack_start(GTK_BOX(columna),
                           etiqueta("Este archivo tiene errores: corrígelos (pestaña Errores). "
                                    "La tabla de símbolos solo queda completa cuando el "
                                    "análisis semántico acepta el programa."), FALSE, FALSE, 0);
    } else {
        /* Resumen arriba. */
        GString *r = g_string_new(NULL);
        g_string_append_printf(r, "<b>%zu símbolo%s en %zu ámbito%s</b>  ·  integridad "
                                  "(sim_verificar_pre_memoria): %s",
                               s.cantidad, s.cantidad == 1 ? "" : "s",
                               s.cantidad_ambitos, s.cantidad_ambitos == 1 ? "" : "s",
                               s.consistente ? "correcta" : "con problemas");
        gtk_box_pack_start(GTK_BOX(columna), etiqueta(r->str), FALSE, FALSE, 0);
        g_string_free(r, TRUE);

        GtkWidget *pestanas = gtk_notebook_new();
        gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_simbolos(&s),
                                 gtk_label_new("Símbolos"));
        gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_ambitos(&s),
                                 gtk_label_new("Ámbitos"));
        gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_listado(s.listado),
                                 gtk_label_new("sim_imprimir"));
        gtk_box_pack_start(GTK_BOX(columna), pestanas, TRUE, TRUE, 0);
    }
    if (estado == 0) ide_simbolos_liberar(&s);

    gtk_widget_show_all(ventana_simbolos);
}
