/*
 * Pruebas del núcleo del IDE (src/ide_nucleo.c).
 *
 * Revisa las tres partes del núcleo:
 *   1. Sugerencias de código.
 *   2. Autocierre de paréntesis, llaves, corchetes y comillas.
 *   3. Revisión de delimitadores sin cerrar o mal cerrados.
 *
 * Se ejecuta con: make test-ide-nucleo
 */
#include "ide_nucleo.h"

#include <stdio.h>
#include <string.h>

/* Cuántas verificaciones fallaron. */
static int fallos = 0;

/* Si la condición es falsa, muestra la línea que falló y cuenta el fallo. */
#define VERIFICAR(condicion) do { \
    if (!(condicion)) { \
        fprintf(stderr, "FALLO en la linea %d: %s\n", __LINE__, #condicion); \
        fallos++; \
    } \
} while (0)

/* Revisa los delimitadores de un texto y devuelve la lista de errores. */
static DiagLista revisar(const char *texto)
{
    DiagLista lista = {0};
    VERIFICAR(ide_revisar_delimitadores(texto, strlen(texto), "a.bal", &lista) == 0);
    return lista;
}

int main(void)
{
    /* Un programa pequeño para las pruebas de sugerencias. */
    const char *programa =
        "create_funk #declare_int# contar() {\n"
        "    contador * declare_int : 0;\n"
        "    contenido * declare_int : 1;\n"
        "    %% conejo no debe salir: esta en un comentario\n"
        "    whale (contador < 3) { contador : contador gauss 1; } stop;\n"
        "    give contador;\n"
        "}\n";

    /* ---------- 1. Sugerencias ---------- */

    /* Prueba 1.1: "cont" sugiere los identificadores del archivo, en orden
     * alfabético y sin repetir (contador aparece varias veces). */
    IdeSugerencias s = {0};
    VERIFICAR(ide_sugerencias(programa, strlen(programa), "cont", &s) == 0);
    VERIFICAR(s.cantidad == 3);
    if (s.cantidad == 3) {
        VERIFICAR(strcmp(s.elementos[0].texto, "contador") == 0);
        VERIFICAR(strcmp(s.elementos[1].texto, "contar") == 0);
        VERIFICAR(strcmp(s.elementos[2].texto, "contenido") == 0);
        VERIFICAR(s.elementos[0].tipo == IDE_IDENTIFICADOR);
    }
    ide_liberar_sugerencias(&s);
    VERIFICAR(s.cantidad == 0 && s.elementos == NULL);

    /* Prueba 1.2: "co" no sugiere "conejo", porque está en un comentario. */
    VERIFICAR(ide_sugerencias(programa, strlen(programa), "co", &s) == 0);
    for (size_t i = 0; i < s.cantidad; ++i) {
        VERIFICAR(strcmp(s.elementos[i].texto, "conejo") != 0);
    }
    ide_liberar_sugerencias(&s);

    /* Prueba 1.3: "wh" sugiere las palabras clave whale y whether, con ayuda. */
    VERIFICAR(ide_sugerencias("", 0, "wh", &s) == 0);
    VERIFICAR(s.cantidad == 2);
    if (s.cantidad == 2) {
        VERIFICAR(strcmp(s.elementos[0].texto, "whale") == 0);
        VERIFICAR(strcmp(s.elementos[1].texto, "whether") == 0);
        VERIFICAR(s.elementos[1].tipo == IDE_PALABRA_CLAVE);
        VERIFICAR(strcmp(s.elementos[1].descripcion, "condicional (si)") == 0);
    }
    ide_liberar_sugerencias(&s);

    /* Prueba 1.4: los identificadores salen antes que las palabras clave.
     * "ga" encuentra la variable "ganancia" y la palabra clave "gauss". */
    const char *con_ganancia = "ganancia * declare_int : 5;";
    VERIFICAR(ide_sugerencias(con_ganancia, strlen(con_ganancia), "ga", &s) == 0);
    VERIFICAR(s.cantidad == 2);
    if (s.cantidad == 2) {
        VERIFICAR(strcmp(s.elementos[0].texto, "ganancia") == 0);
        VERIFICAR(strcmp(s.elementos[1].texto, "gauss") == 0);
    }
    ide_liberar_sugerencias(&s);

    /* ---------- 2. Autocierre ---------- */

    VERIFICAR(ide_cierre_de('(') == ')');
    VERIFICAR(ide_cierre_de('[') == ']');
    VERIFICAR(ide_cierre_de('{') == '}');
    VERIFICAR(ide_cierre_de('"') == '"');
    VERIFICAR(ide_cierre_de('$') == '$');
    VERIFICAR(ide_cierre_de('a') == '\0');  /* Una letra no abre nada. */

    /* ---------- 3. Delimitadores ---------- */

    /* Prueba 3.1: un programa bien cerrado no tiene errores. */
    DiagLista l = revisar(programa);
    VERIFICAR(l.cantidad == 0);
    diag_liberar(&l);

    /* Prueba 3.2: una llave que nunca se cierra. El error apunta a la llave. */
    l = revisar("create_funk #declare_int# f() {\n    give 1;\n");
    VERIFICAR(l.cantidad == 1);
    if (l.cantidad == 1) {
        VERIFICAR(l.elementos[0].fase == DIAG_SINTACTICO);
        VERIFICAR(l.elementos[0].linea == 1 && l.elementos[0].columna == 31);
        VERIFICAR(strcmp(l.elementos[0].mensaje, "falta cerrar este '{' con '}'") == 0);
    }
    diag_liberar(&l);

    /* Prueba 3.3: un cierre que sobra. */
    l = revisar("x : 1;\n}");
    VERIFICAR(l.cantidad == 1);
    if (l.cantidad == 1) {
        VERIFICAR(l.elementos[0].linea == 2 && l.elementos[0].columna == 1);
        VERIFICAR(strstr(l.elementos[0].mensaje, "sobra") != NULL);
    }
    diag_liberar(&l);

    /* Prueba 3.4: un paréntesis cerrado con una llave. */
    l = revisar("f(1, 2};");
    VERIFICAR(l.cantidad == 1);
    if (l.cantidad == 1) {
        VERIFICAR(l.elementos[0].columna == 7);  /* Apunta a la '}' equivocada. */
        VERIFICAR(strcmp(l.elementos[0].mensaje,
                         "se esperaba ')' para cerrar el '(' de la linea 1, "
                         "columna 2, pero se encontro '}'") == 0);
    }
    diag_liberar(&l);

    /* Prueba 3.4b: un '(' sin cerrar dentro de llaves. El '}' es de la '{',
     * así que el error es el '(' abierto, no un cierre equivocado. */
    l = revisar("{\n  x : (1;\n}");
    VERIFICAR(l.cantidad == 1);
    if (l.cantidad == 1) {
        VERIFICAR(l.elementos[0].linea == 2 && l.elementos[0].columna == 7);
        VERIFICAR(strcmp(l.elementos[0].mensaje, "falta cerrar este '(' con ')'") == 0);
    }
    diag_liberar(&l);

    /* Prueba 3.4c: un ')' que sobra dentro de llaves (en otra línea que la '{'). */
    l = revisar("{\n  x : 1);\n}");
    VERIFICAR(l.cantidad == 1);
    if (l.cantidad == 1) {
        VERIFICAR(l.elementos[0].linea == 2 && l.elementos[0].columna == 8);
        VERIFICAR(strstr(l.elementos[0].mensaje, "sobra") != NULL);
    }
    diag_liberar(&l);

    /* Prueba 3.5: los delimitadores dentro de cadenas y comentarios no cuentan. */
    l = revisar("t * declare_text : \"(((\";\n%% }}} comentario\n");
    VERIFICAR(l.cantidad == 0);
    diag_liberar(&l);

    if (fallos > 0) {
        fprintf(stderr, "test_ide_nucleo: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_nucleo: OK");
    return 0;
}
