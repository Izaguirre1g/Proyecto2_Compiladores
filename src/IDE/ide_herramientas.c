/* Puente comprobable sin ventanas: lexer real, CLI real, script original y CB.
 * Todos los temporales pertenecen a una carpeta privada por invocación y se
 * eliminan tanto en éxito como en error. Las salidas se publican tras validar. */
#include "ide_herramientas.h"
#include "codificacion_binaria.h"
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

const char *ide_token_categoria(TokenType t)
{
    if (t == TOKEN_ERROR) return "Error";
    if (t == TOKEN_IDENTIFIER) return "Identificador";
    if (t == TOKEN_INTEGER || t == TOKEN_STRING || t == TOKEN_CHARACTER ||
        t == TOKEN_DECLARE_TRUE || t == TOKEN_DECLARE_FALSE) return "Literal";
    if ((t >= TOKEN_GAUSS && t <= TOKEN_XOR) || t == TOKEN_ASSIGN || t == TOKEN_STAR)
        return "Operador";
    if (t >= TOKEN_DOT && t <= TOKEN_HASH) return "Delimitador";
    return t == TOKEN_EOF ? "Fin de archivo" : "Palabra reservada";
}

/* Solo borra nuestra carpeta plana privada, nunca el directorio del usuario. */
static void limpiar(const char *dir)
{
    GDir *d = g_dir_open(dir, 0, NULL);
    if (d) {
        const char *n;
        while ((n = g_dir_read_name(d))) {
            gchar *r = g_build_filename(dir, n, NULL);
            g_unlink(r);
            g_free(r);
        }
        g_dir_close(d);
    }
    g_rmdir(dir);
}

/* argv se pasa directamente; ni rutas ni diagnósticos se evalúan por un shell. */
static gboolean ejecutar(const char *raiz, gchar **argv, gchar **salida, GError **error)
{
    gchar *diag = NULL;
    gint estado = 0;
    gboolean ok = g_spawn_sync(raiz, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                               salida, &diag, &estado, error);
    if (ok && !g_spawn_check_wait_status(estado, NULL)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s%s%s",
                    diag && *diag ? diag : "La herramienta terminó con error.",
                    *salida && **salida ? "\n" : "", *salida ? *salida : "");
        ok = FALSE;
    }
    g_free(diag);
    return ok;
}

gchar *ide_generar_json(const char *raiz, const char *texto, gboolean spilling, GError **error)
{
    gchar *dir = g_dir_make_tmp("bal-ide-XXXXXX", error);
    if (!dir) return NULL;
    gchar *fuente = g_build_filename(dir, "editor.bal", NULL);
    gchar *json = g_build_filename(dir, "editor.json", NULL);
    gchar *balc = g_build_filename(raiz, "balc", NULL);
    gchar *script = g_build_filename(raiz, "tools", "spilling_json.py", NULL);
    gchar *salida = NULL, *resultado = NULL;
    gchar *ast_argv[] = {balc, "--ast-json", fuente, NULL};
    gchar *spill_argv[] = {"python3", script, fuente, "-o", json, NULL};
    if (g_file_set_contents(fuente, texto, -1, error) &&
        ejecutar(raiz, spilling ? spill_argv : ast_argv, &salida, error)) {
        if (spilling) g_file_get_contents(json, &resultado, NULL, error);
        else { resultado = salida; salida = NULL; }
    }
    g_free(salida); g_free(fuente); g_free(json); g_free(balc); g_free(script);
    limpiar(dir); g_free(dir);
    return resultado;
}

/* Lee un resultado binario sin tratar sus bytes nulos como fin de texto. */
static GBytes *leer_bytes(const char *ruta, GError **error)
{
    gchar *datos = NULL;
    gsize n = 0;
    if (!g_file_get_contents(ruta, &datos, &n, error)) return NULL;
    return g_bytes_new_take(datos, n);
}

gboolean ide_ensamblar_archivo(const char *raiz, const char *ruta,
                               GBytes **bin, GBytes **mem, GError **error)
{
    *bin = *mem = NULL;
    gchar *texto = NULL;
    gsize n = 0;
    if (!g_file_get_contents(ruta, &texto, &n, error)) return FALSE;
    gchar *dir = g_dir_make_tmp("bal-asm-XXXXXX", error);
    if (!dir) { g_free(texto); return FALSE; }
    gchar *asm_ruta = g_build_filename(dir, "entrada.asm", NULL);
    gchar *bin_ruta = g_build_filename(dir, "entrada.bin", NULL);
    gchar *mem_ruta = g_build_filename(dir, "entrada.mem", NULL);
    gchar *balc = g_build_filename(raiz, "balc", NULL), *salida = NULL;
    gchar *argv[] = {balc, "-x", asm_ruta, NULL};
    gboolean ok = g_file_set_contents(asm_ruta, texto, n, error) &&
                   ejecutar(raiz, argv, &salida, error);
    if (ok) { *bin = leer_bytes(bin_ruta, error); ok = *bin != NULL; }
    if (ok) { *mem = leer_bytes(mem_ruta, error); ok = *mem != NULL; }
    if (!ok) { g_clear_pointer(bin, g_bytes_unref); g_clear_pointer(mem, g_bytes_unref); }
    g_free(texto); g_free(asm_ruta); g_free(bin_ruta); g_free(mem_ruta);
    g_free(balc); g_free(salida); limpiar(dir); g_free(dir);
    return ok;
}

GBytes *ide_generar_mem(const char *texto, GError **error)
{
    CbPrograma programa = {0};
    FILE *diag = tmpfile(), *mem = tmpfile();
    GBytes *resultado = NULL;
    if (!diag || !mem) goto fin;
    if (cb_ensamblar(texto, &programa, diag) != 0) goto fin;
    if (cb_escribir_mem(&programa, mem) != 0 || fflush(mem) != 0) goto fin;
    long n = ftell(mem);
    if (n < 0 || fseek(mem, 0, SEEK_SET) != 0) goto fin;
    gchar *datos = g_malloc((gsize)n + 1);
    if (fread(datos, 1, (size_t)n, mem) == (size_t)n)
        resultado = g_bytes_new_take(datos, (gsize)n);
    else g_free(datos);
fin:
    if (!resultado) {
        GString *mensaje = g_string_new("No se pudo codificar MEM.\n");
        if (diag) { rewind(diag); int c; while ((c = fgetc(diag)) != EOF) g_string_append_c(mensaje, c); }
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, mensaje->str);
        g_string_free(mensaje, TRUE);
    }
    if (diag) fclose(diag);
    if (mem) fclose(mem);
    cb_liberar(&programa);
    return resultado;
}

gboolean ide_guardar_bytes(const char *ruta, GBytes *bytes, gboolean reemplazar, GError **error)
{
    GFile *f = g_file_new_for_path(ruta);
    GFileOutputStream *s = reemplazar
        ? g_file_replace(f, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error)
        : g_file_create(f, G_FILE_CREATE_NONE, NULL, error);
    g_object_unref(f);
    if (!s) return FALSE;
    gsize n;
    const void *datos = g_bytes_get_data(bytes, &n);
    gboolean ok = g_output_stream_write_all(G_OUTPUT_STREAM(s), datos, n, NULL, NULL, error);
    /* Conservamos el primer error; cerrar también puede detectar un fallo. */
    if (!g_output_stream_close(G_OUTPUT_STREAM(s), NULL, ok ? error : NULL)) ok = FALSE;
    g_object_unref(s);
    return ok;
}
