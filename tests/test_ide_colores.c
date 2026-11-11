/*
 * Pruebas de los colores del código (src/IDE/ide_colores.c), sin abrir
 * ventanas: se revisa la categoría de cada pedazo del texto.
 *
 * Se ejecuta con: make test-ide-colores
 */
#include "ide_colores.h"

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

/* El color del tramo que empieza en (linea, columna), o -1 si no hay. */
static int color_en(const IdeColores *c, size_t linea, size_t columna)
{
    for (size_t i = 0; i < c->cantidad; ++i) {
        if (c->elementos[i].linea == linea && c->elementos[i].columna == columna) {
            return (int)c->elementos[i].color;
        }
    }
    return -1;
}

/* El tramo que empieza en (linea, columna), o NULL. */
static const IdeTramoColor *tramo_en(const IdeColores *c, size_t linea, size_t columna)
{
    for (size_t i = 0; i < c->cantidad; ++i) {
        if (c->elementos[i].linea == linea && c->elementos[i].columna == columna) {
            return &c->elementos[i];
        }
    }
    return NULL;
}

int main(void)
{
    const char *codigo =
        "%% Factorial\n"                                                   /* 1 */
        "create_funk #declare_int# factorial(n * declare_int) {\n"        /* 2 */
        "    r * declare_int : 1;   %% acumulado\n"                      /* 3 */
        "    whale (n > 1) {\n"                                           /* 4 */
        "        r : r pitagoras n;\n"                                    /* 5 */
        "        n : n neumann 1;\n"                                      /* 6 */
        "    } stop;\n"                                                   /* 7 */
        "    give r;\n"                                                   /* 8 */
        "}\n"                                                             /* 9 */
        "create_funk #declare_infinite_void# main() {\n"                  /* 10 */
        "    t * declare_text : \"año\";\n"                               /* 11 */
        "    x * declare_int : factorial((2 gauss 3));\n"                 /* 12 */
        "}\n";                                                            /* 13 */
    IdeColores c;
    VERIFICAR(ide_colorear(codigo, strlen(codigo), &c) == 0);

    /* Comentarios: el de la línea 1 completo, y el del final de la línea 3. */
    const IdeTramoColor *com = tramo_en(&c, 1, 1);
    VERIFICAR(com != NULL && com->color == IDE_COLOR_COMENTARIO &&
              com->linea_fin == 1 && com->columna_fin == 13);
    VERIFICAR(color_en(&c, 3, 28) == IDE_COLOR_COMENTARIO);

    /* Palabras del lenguaje, tipos, nombres de función y variables. */
    VERIFICAR(color_en(&c, 2, 1) == IDE_COLOR_PALABRA);    /* create_funk */
    VERIFICAR(color_en(&c, 2, 14) == IDE_COLOR_TIPO);      /* declare_int */
    VERIFICAR(color_en(&c, 2, 27) == IDE_COLOR_FUNCION);   /* factorial( */
    VERIFICAR(color_en(&c, 2, 37) == IDE_COLOR_VARIABLE);  /* n */
    VERIFICAR(color_en(&c, 3, 5) == IDE_COLOR_VARIABLE);   /* r */
    VERIFICAR(color_en(&c, 3, 23) == IDE_COLOR_NUMERO);    /* 1 */

    /* Control del flujo y operadores con nombre. */
    VERIFICAR(color_en(&c, 4, 5) == IDE_COLOR_CONTROL);    /* whale */
    VERIFICAR(color_en(&c, 5, 15) == IDE_COLOR_PALABRA);   /* pitagoras */
    VERIFICAR(color_en(&c, 7, 7) == IDE_COLOR_CONTROL);    /* stop */
    VERIFICAR(color_en(&c, 8, 5) == IDE_COLOR_CONTROL);    /* give */
    VERIFICAR(color_en(&c, 10, 37) == IDE_COLOR_FUNCION);  /* main */

    /* Un texto con una letra de dos bytes ("ñ"): la columna cuenta letras. */
    const IdeTramoColor *txt = tramo_en(&c, 11, 24);
    VERIFICAR(txt != NULL && txt->color == IDE_COLOR_TEXTO && txt->columna_fin == 29);

    /* Pares: cada nivel con su color, y el cierre igual que su apertura.
     * El { de la función es el nivel 1; lo de adentro, el 2, el 3...
     * Línea 12, dentro de main: factorial ( ( 2 gauss 3 ) ) */
    VERIFICAR(color_en(&c, 2, 54) == IDE_COLOR_PAR_1);    /* { de la función */
    VERIFICAR(color_en(&c, 4, 19) == IDE_COLOR_PAR_2);    /* { del whale (dentro de la función) */
    VERIFICAR(color_en(&c, 12, 32) == IDE_COLOR_PAR_2);   /* ( de la llamada */
    VERIFICAR(color_en(&c, 12, 33) == IDE_COLOR_PAR_3);   /* ( de adentro */
    VERIFICAR(color_en(&c, 12, 43) == IDE_COLOR_PAR_3);   /* ) de adentro */
    VERIFICAR(color_en(&c, 12, 44) == IDE_COLOR_PAR_2);   /* ) de la llamada */

    /* Los símbolos comunes (; : *) van con el color normal: sin tramo. */
    VERIFICAR(color_en(&c, 3, 7) == -1);                  /* * */
    ide_liberar_colores(&c);

    /* Un comentario de bloque de varias líneas y un texto sin cerrar no
     * rompen nada. */
    const char *raro = "%%// uno\ndos //%%\nx : \"sin cerrar\n";
    VERIFICAR(ide_colorear(raro, strlen(raro), &c) == 0);
    const IdeTramoColor *bloque = tramo_en(&c, 1, 1);
    VERIFICAR(bloque != NULL && bloque->color == IDE_COLOR_COMENTARIO && bloque->linea_fin == 2);
    ide_liberar_colores(&c);

    /* Texto vacío. */
    VERIFICAR(ide_colorear("", 0, &c) == 0 && c.cantidad == 0);
    ide_liberar_colores(&c);

    if (fallos > 0) {
        fprintf(stderr, "test_ide_colores: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_colores: OK");
    return 0;
}
