/* Ventanas auxiliares del IDE: reutilizan el tema GTK y las herramientas reales.
 * No interpretan BAL ni ASM. Los modelos GTK copian los textos de los tokens;
 * los botones de exportación poseen su propia instantánea hasta destruirse. */
#include "ide_herramientas_ventana.h"
#include "ide_herramientas.h"
#include "lexer.h"
#include <string.h>

static void mensaje(GtkWindow *padre, const char *texto)
{
    GtkWidget *d = gtk_message_dialog_new(padre, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", texto);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

/* El ejecutable y los tools se distribuyen juntos en la raíz del proyecto.
 * No dependemos del directorio desde el que se lanzó el IDE. */
static gchar *raiz_proyecto(void)
{
    gchar *exe = g_file_read_link("/proc/self/exe", NULL);
    gchar *raiz = exe ? g_path_get_dirname(exe) : g_get_current_dir();
    g_free(exe);
    return raiz;
}

/* La misma paleta del panel inferior: se limita a esta ventana, sin cambiar
 * el tema de las ventanas existentes ni los HTML originales. */
static void estilo_lexico(GtkWidget *ventana)
{
    static gboolean instalado = FALSE;
    gtk_widget_set_name(ventana, "ide_lexico");
    if (instalado) return;
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "#ide_lexico, #ide_lexico treeview, #ide_lexico scrolledwindow {"
        "background-color: #1E1E1E; color: #CCCCCC;}"
        "#ide_lexico label {color: #CCCCCC;}"
        "#ide_lexico treeview:selected {background-color: #04395E;}"
        "#ide_lexico treeview header button {background-image: none;"
        "background-color: #252526; color: #CCCCCC; border-color: #3C3C3C;}"
        "#ide_lexico scrollbar {background-color: #1E1E1E;}"
        "#ide_lexico scrollbar slider {background-color: #424242;}", -1, NULL);
    gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(ventana),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    instalado = TRUE;
}

void ide_mostrar_lexico(GtkWindow *padre, const char *texto)
{
    GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    estilo_lexico(w);
    gtk_window_set_title(GTK_WINDOW(w), "Análisis léxico — texto actual del editor");
    gtk_window_set_transient_for(GTK_WINDOW(w), padre);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(w), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(w), 900, 600);
    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(caja), 8);
    GtkListStore *modelo = gtk_list_store_new(7, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    Lexer l;
    lexer_init(&l, texto, strlen(texto), "editor");
    size_t total = 0, errores = 0, categorias[6] = {0};
    const char *cats[] = {"Palabra reservada", "Identificador", "Literal", "Operador", "Delimitador", "Error"};
    for (;;) {
        Token t = lexer_next(&l);
        if (t.type == TOKEN_EOF) { token_dispose(&t); break; }
        ++total;
        if (t.type == TOKEN_ERROR) ++errores;
        const char *cat = ide_token_categoria(t.type);
        for (int i = 0; i < 6; ++i) if (!strcmp(cat, cats[i])) ++categorias[i];
        gchar *numero = g_strdup_printf("%zu", total);
        gchar *linea = g_strdup_printf("%zu", t.line);
        gchar *columna = g_strdup_printf("%zu", t.column);
        gchar *lexema = g_utf8_make_valid(t.lexeme ? t.lexeme : "", t.length);
        gtk_list_store_insert_with_values(modelo, NULL, -1, 0, numero, 1, lexema,
            2, token_type_name(t.type), 3, cat, 4, linea, 5, columna,
            6, t.type == TOKEN_ERROR ? token_error_message(t.error) : "", -1);
        gboolean oom = t.error == TOKEN_OUT_OF_MEMORY;
        g_free(numero); g_free(linea); g_free(columna); g_free(lexema); token_dispose(&t);
        /* Un fallo de reserva no avanza el lexer: evitamos un bucle infinito. */
        if (oom) break;
    }
    gchar *resumen = g_strdup_printf("%zu tokens · %zu errores · %zu palabras reservadas · "
        "%zu identificadores · %zu literales · %zu operadores · %zu delimitadores\n"
        "Sin EOF, espacios ni comentarios. Instantánea del editor, incluidos cambios sin guardar.",
        total, errores, categorias[0], categorias[1], categorias[2], categorias[3], categorias[4]);
    GtkWidget *etiqueta = gtk_label_new(resumen);
    gtk_label_set_xalign(GTK_LABEL(etiqueta), 0);
    gtk_label_set_line_wrap(GTK_LABEL(etiqueta), TRUE);
    gtk_box_pack_start(GTK_BOX(caja), etiqueta, FALSE, FALSE, 0);
    g_free(resumen);
    GtkWidget *vista = gtk_tree_view_new_with_model(GTK_TREE_MODEL(modelo));
    g_object_unref(modelo);
    const char *titulos[] = {"Número", "Lexema", "Token", "Categoría", "Línea", "Columna", "Diagnóstico"};
    for (int i = 0; i < 7; ++i) {
        GtkCellRenderer *celda = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(titulos[i], celda, "text", i, NULL);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(vista), col);
    }
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), vista);
    gtk_box_pack_start(GTK_BOX(caja), scroll, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(w), caja);
    gtk_widget_show_all(w);
}

/* Un solo selector de salida; más abajo se confirma el conjunto completo. */
static gchar *elegir_salida(GtkWindow *padre, const char *titulo, const char *nombre)
{
    GtkWidget *d = gtk_file_chooser_dialog_new(titulo, padre, GTK_FILE_CHOOSER_ACTION_SAVE,
        "Cancelar", GTK_RESPONSE_CANCEL, "Guardar", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), nombre);
    gchar *ruta = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT
        ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d)) : NULL;
    gtk_widget_destroy(d);
    return ruta;
}

/* Preguntamos por todas las colisiones antes de escribir el primer archivo. */
static gboolean confirmar(GtkWindow *padre, gchar **rutas, size_t n)
{
    GString *s = g_string_new("Se reemplazarán estos archivos:\n");
    gboolean hay = FALSE;
    for (size_t i = 0; i < n; ++i) if (g_file_test(rutas[i], G_FILE_TEST_EXISTS)) {
        hay = TRUE; g_string_append_printf(s, "%s\n", rutas[i]);
    }
    gboolean ok = TRUE;
    if (hay) {
        GtkWidget *d = gtk_message_dialog_new(padre, GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL, "%s", s->str);
        ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK;
        gtk_widget_destroy(d);
    }
    g_string_free(s, TRUE);
    return ok;
}

void ide_visualizar_json(GtkWindow *padre, const char *texto, gboolean spilling)
{
    gchar *destino = elegir_salida(padre, "Guardar JSON del texto actual",
        spilling ? "editor.spilling.json" : "editor.ast.json");
    if (!destino) return;
    gchar *rutas[] = {destino};
    if (!confirmar(padre, rutas, 1)) { g_free(destino); return; }
    gchar *raiz = raiz_proyecto();
    GError *error = NULL;
    gchar *json = ide_generar_json(raiz, texto, spilling, &error);
    if (json) {
        GBytes *bytes = g_bytes_new(json, strlen(json));
        if (ide_guardar_bytes(destino, bytes, TRUE, &error)) {
            gchar *html = g_build_filename(raiz, "tools",
                spilling ? "visualizar_spilling.html" : "visualizar_ast.html", NULL);
            gchar *uri = g_filename_to_uri(html, NULL, &error);
            if (uri) {
                if (!g_file_test(html, G_FILE_TEST_IS_REGULAR))
                    g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No está el HTML original en tools/.");
                else gtk_show_uri_on_window(padre, uri, GDK_CURRENT_TIME, &error);
            }
            g_free(uri); g_free(html);
            if (!error) {
                /* El selector del HTML evita restricciones fetch de file://.
                 * Copiamos la ruta para pegarla en su selector de archivos. */
                gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), destino, -1);
                gchar *info = g_strdup_printf("JSON generado desde el editor actual:\n%s\n\n"
                    "En el HTML original, seleccione este archivo JSON. La ruta se copió al portapapeles.", destino);
                mensaje(padre, info); g_free(info);
            }
        }
        g_bytes_unref(bytes);
    }
    if (error) { mensaje(padre, error->message); g_error_free(error); }
    g_free(json); g_free(raiz); g_free(destino);
}

/* Publica archivos validados; informa con precisión qué se alcanzó a guardar.
 * No promete atomicidad del conjunto si el sistema falla entre archivos. */
static void guardar_conjunto(GtkWindow *padre, gchar **rutas, GBytes **bytes, size_t n)
{
    if (!confirmar(padre, rutas, n)) return;
    GError *error = NULL;
    size_t i = 0;
    for (; i < n; ++i) if (!ide_guardar_bytes(rutas[i], bytes[i], TRUE, &error)) break;
    GString *info = g_string_new(NULL);
    for (size_t j = 0; j < i; ++j) g_string_append_printf(info, "Guardado: %s\n", rutas[j]);
    if (error) { g_string_append_printf(info, "Error al guardar %s: %s", rutas[i], error->message); g_error_free(error); }
    mensaje(padre, info->str); g_string_free(info, TRUE);
}

void ide_elegir_asm(GtkWindow *padre)
{
    GtkWidget *d = gtk_file_chooser_dialog_new("Ensamblar archivo .asm", padre, GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancelar", GTK_RESPONSE_CANCEL, "Ensamblar", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "Ensamblador (*.asm)"); gtk_file_filter_add_pattern(f, "*.asm");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), f);
    gchar *ruta = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT
        ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d)) : NULL;
    gtk_widget_destroy(d);
    if (!ruta) return;
    gchar *raiz = raiz_proyecto();
    GBytes *bytes[2] = {NULL, NULL}; GError *error = NULL;
    if (!g_str_has_suffix(ruta, ".asm"))
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Seleccione un archivo .asm.");
    else if (ide_ensamblar_archivo(raiz, ruta, &bytes[0], &bytes[1], &error)) {
        gchar *base = g_strndup(ruta, strlen(ruta) - 4);
        gchar *rutas[] = {g_strconcat(base, ".bin", NULL), g_strconcat(base, ".mem", NULL)};
        guardar_conjunto(padre, rutas, bytes, 2);
        g_free(base); g_free(rutas[0]); g_free(rutas[1]);
    }
    if (error) { mensaje(padre, error->message); g_error_free(error); }
    for (int i = 0; i < 2; ++i) if (bytes[i]) g_bytes_unref(bytes[i]);
    g_free(raiz); g_free(ruta);
}

typedef struct {
    GtkWindow *padre;
    gchar *asm_texto, *nombre, *titulo;
    GBytes *bin;
} Exportacion;
static void liberar_exportacion(gpointer datos, GClosure *closure)
{
    (void)closure;
    Exportacion *e = datos;
    g_free(e->asm_texto); g_free(e->nombre); g_free(e->titulo);
    g_bytes_unref(e->bin); g_free(e);
}
static void exportar(GtkWidget *boton, gpointer datos)
{
    (void)boton;
    Exportacion *e = datos;
    gchar *ruta = elegir_salida(e->padre, e->titulo, e->nombre);
    if (!ruta) return;
    gchar *base = g_str_has_suffix(ruta, ".asm") ? g_strndup(ruta, strlen(ruta) - 4) : g_strdup(ruta);
    gchar *rutas[] = {g_strconcat(base, ".asm", NULL), g_strconcat(base, ".bin", NULL), g_strconcat(base, ".mem", NULL)};
    GError *error = NULL;
    GBytes *mem = ide_generar_mem(e->asm_texto, &error);
    if (mem) {
        GBytes *asm_bytes = g_bytes_new(e->asm_texto, strlen(e->asm_texto));
        GBytes *bytes[] = {asm_bytes, e->bin, mem};
        guardar_conjunto(e->padre, rutas, bytes, 3);
        g_bytes_unref(asm_bytes); g_bytes_unref(mem);
    }
    if (error) { mensaje(e->padre, error->message); g_error_free(error); }
    for (int i = 0; i < 3; ++i) g_free(rutas[i]);
    g_free(base); g_free(ruta);
}
GtkWidget *ide_boton_exportar(GtkWindow *padre, const IdeEnsamblador *a)
{
    Exportacion *e = g_new0(Exportacion, 1);
    e->padre = padre; e->asm_texto = g_strdup(a->ensamblador);
    e->nombre = g_strdup("programa.asm");
    e->titulo = g_strdup("Exportar O0: ASM, contenedor BALB y MEM de instrucciones");
    e->bin = g_bytes_new(a->balb, a->largo_balb);
    GtkWidget *b = gtk_button_new_with_label("Exportar archivos...");
    gtk_widget_set_tooltip_text(b, "O0 de esta ventana: .asm, .bin BALB completo y .mem de instrucciones; no es O2.");
    g_signal_connect_data(b, "clicked", G_CALLBACK(exportar), e, liberar_exportacion, 0);
    return b;
}

/* Reutiliza el mismo diálogo, codificador MEM y confirmación de reemplazos.
 * Las copias pertenecen al botón: destruir o actualizar la comparación no
 * invalida un resultado retenido por la ventana. No ejecuta ningún backend. */
GtkWidget *ide_boton_exportar_nivel(GtkWindow *padre, const IdeOptNivel *nivel,
                                   const char *nombre, const char *etiqueta)
{
    GtkWidget *b = gtk_button_new_with_label("Exportar");
    if (!nivel->compilado || !nivel->ensamblador || !nivel->balb || !nivel->largo_balb) {
        gtk_widget_set_sensitive(b, FALSE);
        return b;
    }
    Exportacion *e = g_new0(Exportacion, 1);
    e->padre = padre;
    e->asm_texto = g_strdup(nivel->ensamblador);
    e->bin = g_bytes_new(nivel->balb, nivel->largo_balb);
    gchar *base = g_path_get_basename(nombre);
    if (g_str_has_suffix(base, ".bal")) base[strlen(base) - 4] = '\0';
    e->nombre = g_strdup_printf("%s_%s.asm", base, etiqueta);
    e->titulo = g_strdup_printf("Exportar %s: ASM, contenedor BALB y MEM de instrucciones", etiqueta);
    gtk_widget_set_tooltip_text(b, e->titulo);
    g_free(base);
    g_signal_connect_data(b, "clicked", G_CALLBACK(exportar), e, liberar_exportacion, 0);
    return b;
}
