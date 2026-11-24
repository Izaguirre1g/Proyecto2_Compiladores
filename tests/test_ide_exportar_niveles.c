/* Integración GTK real: compila una sola comparación, exporta sus tres niveles
 * mediante los botones y contrasta ASM, BALB, MEM y cargador del simulador.
 * Los diálogos se conducen desde el bucle GTK; no se sustituye la exportación. */
#include "ide_herramientas_ventana.h"
#include "ide_herramientas.h"
#include "sim_cargador.h"
#include <glib/gstdio.h>
#include <string.h>

static gchar *carpeta;
static gboolean cancelar;
static unsigned confirmaciones;

static gboolean dialogos(gpointer datos)
{
    (void)datos;
    GList *lista = gtk_window_list_toplevels();
    for (GList *i = lista; i; i = i->next) {
        GtkWidget *w = i->data;
        if (!gtk_widget_get_visible(w)) continue;
        if (GTK_IS_FILE_CHOOSER(w)) {
            if (!g_object_get_data(G_OBJECT(w), "preparado")) {
                /* Se conserva el nombre propuesto por el botón, O0/O1/O2. */
                gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(w), carpeta);
                g_object_set_data(G_OBJECT(w), "preparado", GINT_TO_POINTER(1));
            } else gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_ACCEPT);
        } else if (GTK_IS_MESSAGE_DIALOG(w)) {
            if (cancelar) ++confirmaciones;
            gtk_dialog_response(GTK_DIALOG(w), cancelar ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_OK);
        }
    }
    g_list_free(lista);
    return G_SOURCE_CONTINUE;
}

static void comprobar(const IdeOptNivel *n, int nivel)
{
    gchar *base = g_strdup_printf("%s/Reordenamiento_O%d", carpeta, nivel);
    gchar *a = g_strconcat(base, ".asm", NULL), *b = g_strconcat(base, ".bin", NULL);
    gchar *m = g_strconcat(base, ".mem", NULL), *texto = NULL;
    gsize largo; GError *error = NULL;
    g_assert_true(g_file_get_contents(a, &texto, &largo, &error)); g_assert_no_error(error);
    g_assert_cmpstr(texto, ==, n->ensamblador);
    GBytes *esperado = ide_generar_mem(texto, &error); g_assert_no_error(error); g_free(texto);
    g_assert_true(g_file_get_contents(b, &texto, &largo, &error)); g_assert_no_error(error);
    g_assert_cmpmem(texto, largo, n->balb, n->largo_balb); g_free(texto);
    g_assert_true(g_file_get_contents(m, &texto, &largo, &error)); g_assert_no_error(error);
    gsize tam; const void *bytes = g_bytes_get_data(esperado, &tam);
    g_assert_cmpmem(texto, largo, bytes, tam); g_free(texto); g_bytes_unref(esperado);
    SimImagen *bin = NULL, *mem = NULL;
    g_assert_cmpint(sim_cargar_balb(b, &bin, stderr), ==, SIM_CARGA_OK);
    g_assert_cmpint(sim_cargar_mem(m, &mem, stderr), ==, SIM_CARGA_OK);
    g_assert_cmpuint(bin->cantidad_bundles, ==, n->bundles);
    g_assert_cmpuint(mem->cantidad_bundles, ==, n->bundles);
    g_assert_cmpmem(bin->imem, n->bundles * 16, mem->imem, n->bundles * 16);
    g_print("O%d: ASM exacto, BALB exacto, MEM y cargador compatibles (%zu bundles)\n", nivel, n->bundles);
    sim_liberar_imagen(&bin); sim_liberar_imagen(&mem);
    g_free(base); g_free(a); g_free(b); g_free(m);
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    GError *error = NULL; gchar *fuente = NULL;
    g_assert_true(g_file_get_contents("examples/dce/dce_ensamblador.bal", &fuente, NULL, &error));
    g_assert_no_error(error);
    IdeOptimizacion r;
    g_assert_cmpint(ide_optimizacion_analizar(fuente, strlen(fuente), NULL, &r), ==, 0);
    g_free(fuente);
    const char *destino = g_getenv("IDE_EXPORT_TEST_DIR");
    carpeta = destino ? g_strdup(destino) : g_dir_make_tmp("exportar niveles-XXXXXX", &error);
    g_assert_no_error(error); g_assert_nonnull(carpeta);
    g_assert_cmpint(g_mkdir_with_parents(carpeta, 0700), ==, 0);
    GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *fila = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_add(GTK_CONTAINER(w), fila); gtk_widget_show_all(w);
    guint timer = g_timeout_add(250, dialogos, NULL);
    const IdeOptNivel *niveles[] = {&r.o0, &r.o1, &r.o2};
    GtkWidget *botones[3];
    for (int i = 0; i < 3; ++i) {
        gchar *etiqueta = g_strdup_printf("O%d", i);
        g_assert_true(niveles[i]->compilado);
        botones[i] = ide_boton_exportar_nivel(GTK_WINDOW(w), niveles[i], "Reordenamiento.bal", etiqueta);
        g_free(etiqueta);
        gtk_box_pack_start(GTK_BOX(fila), botones[i], FALSE, FALSE, 0);
        gtk_widget_show(botones[i]); g_assert_true(gtk_widget_get_sensitive(botones[i]));
        gtk_button_clicked(GTK_BUTTON(botones[i]));
        comprobar(niveles[i], i);
    }
    IdeOptNivel fallido = {0};
    GtkWidget *deshabilitado = ide_boton_exportar_nivel(GTK_WINDOW(w), &fallido, "malo.bal", "O2");
    g_object_ref_sink(deshabilitado);
    g_assert_false(gtk_widget_get_sensitive(deshabilitado));
    gtk_widget_destroy(deshabilitado); g_object_unref(deshabilitado);
    /* Una colisión cancelada no reemplaza el archivo previo. */
    gchar *bin = g_build_filename(carpeta, "Reordenamiento_O2.bin", NULL);
    g_assert_true(g_file_set_contents(bin, "conservar", -1, &error)); g_assert_no_error(error);
    cancelar = TRUE; gtk_button_clicked(GTK_BUTTON(botones[2]));
    gchar *contenido = NULL;
    g_assert_true(g_file_get_contents(bin, &contenido, NULL, &error)); g_assert_no_error(error);
    g_assert_cmpstr(contenido, ==, "conservar"); g_assert_cmpuint(confirmaciones, >, 0);
    g_free(contenido); cancelar = FALSE;
    /* Aceptar el reemplazo vuelve a exportar los mismos bytes del nivel. */
    gtk_button_clicked(GTK_BUTTON(botones[2]));
    comprobar(&r.o2, 2);
    g_free(bin);
    g_source_remove(timer); gtk_widget_destroy(w); ide_optimizacion_liberar(&r);
    if (!destino) {
        GDir *dir = g_dir_open(carpeta, 0, NULL); const char *nombre;
        while ((nombre = g_dir_read_name(dir))) {
            gchar *ruta = g_build_filename(carpeta, nombre, NULL); g_unlink(ruta); g_free(ruta);
        }
        g_dir_close(dir); g_rmdir(carpeta);
    }
    g_free(carpeta);
    g_print("OK: tres niveles, nombres propuestos, botón deshabilitado y confirmación de reemplazo\n");
    return 0;
}
