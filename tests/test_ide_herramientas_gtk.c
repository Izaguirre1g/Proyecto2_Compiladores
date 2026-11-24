/* Ejercita las ventanas reales con GTK y un display disponible. Los diálogos
 * se completan desde el bucle GTK, sin sustituir los callbacks de producción.
 * No abre el navegador ni declara comprobada la interacción con los HTML. */
#include "ide_herramientas_ventana.h"
#include "ide_herramientas.h"
#include <glib/gstdio.h>
#include <string.h>

static gchar *carpeta;
static gboolean cancelar;
static unsigned selectores, avisos;

/* Visita el árbol GTK para comprobar el modelo, independientemente del texto
 * mostrado por el resumen. La fuente existe solo en memoria. */
static void comprobar_tabla(GtkWidget *w, gpointer datos)
{
    unsigned *tablas = datos;
    if (GTK_IS_TREE_VIEW(w)) {
        GtkTreeModel *m = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
        g_assert_cmpint(gtk_tree_model_get_n_columns(m), ==, 7);
        g_assert_cmpint(gtk_tree_model_iter_n_children(m, NULL), ==, 2);
        GtkTreeIter it; g_assert_true(gtk_tree_model_get_iter_first(m, &it));
        gchar *lexema = NULL;
        gtk_tree_model_get(m, &it, 1, &lexema, -1);
        g_assert_cmpstr(lexema, ==, "main"); g_free(lexema);
        g_assert_true(gtk_tree_model_iter_next(m, &it));
        gchar *diagnostico = NULL;
        gtk_tree_model_get(m, &it, 6, &diagnostico, -1);
        g_assert_cmpstr(diagnostico, ==, "carácter no permitido"); g_free(diagnostico);
        ++*tablas;
    }
    if (GTK_IS_CONTAINER(w)) gtk_container_foreach(GTK_CONTAINER(w), comprobar_tabla, datos);
}

/* Se ejecuta también durante gtk_dialog_run; acepta el destino y responde
 * a mensajes/colisiones de modo determinista. Todos los archivos son privados. */
static gboolean dialogos(gpointer datos)
{
    (void)datos;
    GList *lista = gtk_window_list_toplevels();
    for (GList *i = lista; i; i = i->next) {
        GtkWidget *w = i->data;
        if (!gtk_widget_get_visible(w)) continue;
        if (GTK_IS_FILE_CHOOSER(w)) {
            if (!g_object_get_data(G_OBJECT(w), "preparado")) {
                gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(w), carpeta);
                gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(w), "exportado.asm");
                g_object_set_data(G_OBJECT(w), "preparado", GINT_TO_POINTER(1));
            } else { ++selectores; gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_ACCEPT); }
        } else if (GTK_IS_MESSAGE_DIALOG(w)) {
            ++avisos;
            gtk_dialog_response(GTK_DIALOG(w), cancelar ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_CLOSE);
        }
    }
    g_list_free(lista);
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    GError *error = NULL;
    carpeta = g_dir_make_tmp("ide GTK con espacios-XXXXXX", &error); g_assert_no_error(error);
    GtkWidget *padre = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_show(padre);
    ide_mostrar_lexico(GTK_WINDOW(padre), "main @");
    unsigned tablas = 0;
    GList *lista = gtk_window_list_toplevels();
    for (GList *i = lista; i; i = i->next) comprobar_tabla(i->data, &tablas);
    g_list_free(lista); g_assert_cmpuint(tablas, ==, 1);

    /* BIN procede de balc -x; el botón retiene su copia incluso si se libera
     * la entrada antes de pulsarlo. MEM y ASM deben corresponder a esa copia. */
    gchar *asm_ruta = g_build_filename(carpeta, "entrada.asm", NULL);
    g_assert_true(g_file_set_contents(asm_ruta, "nop\nnop\n", -1, &error)); g_assert_no_error(error);
    GBytes *bin = NULL, *mem = NULL;
    gchar *raiz = g_get_current_dir();
    g_assert_true(ide_ensamblar_archivo(raiz, asm_ruta, &bin, &mem, &error)); g_assert_no_error(error);
    IdeEnsamblador a = {0};
    a.ensamblador = g_strdup("nop\nnop\n");
    gsize n; const void *b = g_bytes_get_data(bin, &n);
    a.balb = g_malloc(n); memcpy(a.balb, b, n); a.largo_balb = n;
    GtkWidget *boton = ide_boton_exportar(GTK_WINDOW(padre), &a);
    gtk_container_add(GTK_CONTAINER(padre), boton); gtk_widget_show(boton);
    g_free(a.ensamblador); g_free(a.balb);
    guint timer = g_timeout_add(250, dialogos, NULL);
    gtk_button_clicked(GTK_BUTTON(boton));
    g_assert_cmpuint(selectores, >=, 1); g_assert_cmpuint(avisos, ==, 1);
    gchar *salida = g_build_filename(carpeta, "exportado.bin", NULL), *contenido = NULL;
    gsize largo;
    g_assert_true(g_file_get_contents(salida, &contenido, &largo, &error)); g_assert_no_error(error);
    g_assert_cmpmem(contenido, largo, b, n); g_free(contenido);
    gchar *mem_ruta = g_build_filename(carpeta, "exportado.mem", NULL);
    g_assert_true(g_file_get_contents(mem_ruta, &contenido, &largo, &error)); g_assert_no_error(error);
    b = g_bytes_get_data(mem, &n); g_assert_cmpmem(contenido, largo, b, n); g_free(contenido);
    /* Cancelar una colisión conserva el archivo previo: no se escribe nada. */
    g_assert_true(g_file_set_contents(salida, "conservar", -1, &error)); g_assert_no_error(error);
    cancelar = TRUE;
    gtk_button_clicked(GTK_BUTTON(boton));
    g_assert_true(g_file_get_contents(salida, &contenido, &largo, &error)); g_assert_no_error(error);
    g_assert_cmpstr(contenido, ==, "conservar"); g_free(contenido);
    g_source_remove(timer);
    lista = gtk_window_list_toplevels();
    for (GList *i = lista; i; i = i->next) g_object_ref(i->data);
    for (GList *i = lista; i; i = i->next) gtk_widget_destroy(i->data);
    g_list_free_full(lista, g_object_unref);
    GDir *dir = g_dir_open(carpeta, 0, NULL); const char *nombre;
    while ((nombre = g_dir_read_name(dir))) {
        gchar *r = g_build_filename(carpeta, nombre, NULL); g_unlink(r); g_free(r);
    }
    g_dir_close(dir); g_rmdir(carpeta);
    g_free(carpeta); g_free(asm_ruta); g_free(raiz); g_free(salida); g_free(mem_ruta);
    g_bytes_unref(bin); g_bytes_unref(mem);
    g_print("OK: GTK lexer actual, exportación con copia propia, BALB/MEM y cancelación de reemplazo\n");
    return 0;
}
