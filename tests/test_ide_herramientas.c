/* Prueba los adaptadores reales sin GTK. El modo puente permite a Python
 * validar el JSON estructuralmente y comparar BIN/MEM con la CLI original. */
#include "ide_herramientas.h"
#include "lexer.h"
#include <glib/gstdio.h>
#include <string.h>
#include <stdio.h>

static void lexico(void)
{
    /* El texto es un búfer en memoria, no un archivo guardado. CRLF y Unicode
     * deben conservar las posiciones que reporta el lexer del compilador. */
    const char *texto = "main\r\n  abc gauss 17; @";
    Lexer l; lexer_init(&l, texto, strlen(texto), "sin guardar");
    TokenType tipos[] = {TOKEN_MAIN, TOKEN_IDENTIFIER, TOKEN_GAUSS, TOKEN_INTEGER,
                         TOKEN_SEMICOLON, TOKEN_ERROR, TOKEN_EOF};
    size_t lineas[] = {1,2,2,2,2,2,2}, columnas[] = {1,3,7,13,15,17,18};
    for (size_t i = 0; i < G_N_ELEMENTS(tipos); ++i) {
        Token t = lexer_next(&l);
        g_assert_cmpint(t.type, ==, tipos[i]);
        g_assert_cmpuint(t.line, ==, lineas[i]);
        g_assert_cmpuint(t.column, ==, columnas[i]);
        if (t.type == TOKEN_ERROR) g_assert_cmpstr(token_error_message(t.error), ==, "carácter no permitido");
        token_dispose(&t);
    }
    g_assert_cmpstr(ide_token_categoria(TOKEN_MAIN), ==, "Palabra reservada");
    g_assert_cmpstr(ide_token_categoria(TOKEN_IDENTIFIER), ==, "Identificador");
    g_assert_cmpstr(ide_token_categoria(TOKEN_INTEGER), ==, "Literal");
    g_assert_cmpstr(ide_token_categoria(TOKEN_GAUSS), ==, "Operador");
    g_assert_cmpstr(ide_token_categoria(TOKEN_SEMICOLON), ==, "Delimitador");
    g_assert_cmpstr(ide_token_categoria(TOKEN_ERROR), ==, "Error");
    lexer_init(&l, "\"ñ\" abc", strlen("\"ñ\" abc"), "unicode");
    Token t = lexer_next(&l); g_assert_cmpstr(t.lexeme, ==, "\"ñ\""); token_dispose(&t);
    t = lexer_next(&l); g_assert_cmpuint(t.column, ==, 5); token_dispose(&t);
}

static void exportacion(void)
{
    /* Dos bundles NOP producen exactamente dos líneas MEM, no un contenedor
     * BALB. Se rechaza ASM inválido antes de escribir el primer archivo. */
    GError *e = NULL;
    GBytes *mem = ide_generar_mem("nop\nnop\n", &e);
    g_assert_no_error(e); g_assert_nonnull(mem);
    gsize n; const char *datos = g_bytes_get_data(mem, &n);
    g_assert_cmpuint(n, ==, 66);
    g_assert_cmpmem(datos, n, "00000000000000000000000000000000\n00000000000000000000000000000000\n", 66);
    GBytes *invalido = ide_generar_mem("instruccion_invalida\n", &e);
    g_assert_null(invalido); g_assert_nonnull(e); g_clear_error(&e);
    gchar *dir = g_dir_make_tmp("exportacion con espacios-XXXXXX", &e);
    g_assert_no_error(e);
    gchar *ruta = g_build_filename(dir, "programa.mem", NULL);
    g_assert_true(ide_guardar_bytes(ruta, mem, FALSE, &e)); g_assert_no_error(e);
    g_assert_false(ide_guardar_bytes(ruta, mem, FALSE, &e));
    g_assert_error(e, G_IO_ERROR, G_IO_ERROR_EXISTS); g_clear_error(&e);
    g_assert_true(ide_guardar_bytes(ruta, mem, TRUE, &e)); g_assert_no_error(e);
    g_assert_false(ide_guardar_bytes(dir, mem, TRUE, &e)); g_assert_nonnull(e); g_clear_error(&e);
    gchar *mala = g_build_filename(dir, "no existe", "archivo.mem", NULL);
    g_assert_false(ide_guardar_bytes(mala, mem, FALSE, &e)); g_assert_nonnull(e); g_clear_error(&e);
    g_unlink(ruta); g_rmdir(dir); g_free(dir); g_free(ruta); g_free(mala); g_bytes_unref(mem);
}

int main(int argc, char **argv)
{
    if (argc == 5) {
        GError *e = NULL;
        if (!strcmp(argv[1], "asm")) {
            GBytes *bin = NULL, *mem = NULL;
            if (ide_ensamblar_archivo(argv[2], argv[3], &bin, &mem, &e)) {
                gchar *m = g_strconcat(argv[4], ".mem", NULL);
                gboolean ok = ide_guardar_bytes(argv[4], bin, FALSE, &e) && ide_guardar_bytes(m, mem, FALSE, &e);
                g_free(m); g_bytes_unref(bin); g_bytes_unref(mem);
                if (ok) return 0;
            }
        } else {
            gchar *texto = NULL;
            if (g_file_get_contents(argv[3], &texto, NULL, &e)) {
                gchar *json = ide_generar_json(argv[2], texto, !strcmp(argv[1], "spill"), &e);
                g_free(texto);
                if (json) {
                    gboolean ok = g_file_set_contents(argv[4], json, -1, &e);
                    g_free(json); if (ok) return 0;
                }
            }
        }
        fprintf(stderr, "%s\n", e ? e->message : "sin resultado");
        g_clear_error(&e); return 1;
    }
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/herramientas/lexico", lexico);
    g_test_add_func("/herramientas/exportacion", exportacion);
    return g_test_run();
}
