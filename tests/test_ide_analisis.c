/*
 * Pruebas del análisis del editor (src/IDE/ide_analisis.c).
 *
 * Revisa que el IDE pueda analizar texto SIN guardar:
 *   - que encuentre los errores y los reporte con el nombre original,
 *   - que los "bring" sigan funcionando (por eso el temporal va en la misma carpeta),
 *   - que no deje el archivo temporal olvidado.
 *
 * Se ejecuta con: make test-ide-analisis
 */
#include "ide_analisis.h"

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

/* Devuelve 1 si el archivo existe. */
static int existe(const char *ruta)
{
    FILE *f = fopen(ruta, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

int main(void)
{
    /* Prueba 1: un error semántico se reporta con el nombre ORIGINAL. */
    const char *con_error =
        "create_funk #declare_infinite_void# main() {\n"
        "    numero * declare_int : 10;\n"
        "    numero : declare_true;\n"
        "}\n";
    DiagLista lista = {0};
    VERIFICAR(ide_analizar_texto(con_error, strlen(con_error),
                                 "tests/editor.bal", &lista) == 0);
    VERIFICAR(lista.cantidad == 1);
    if (lista.cantidad == 1) {
        VERIFICAR(lista.elementos[0].fase == DIAG_SEMANTICO);
        VERIFICAR(strcmp(lista.elementos[0].archivo, "tests/editor.bal") == 0);
        VERIFICAR(lista.elementos[0].linea == 3 && lista.elementos[0].columna == 5);
    }
    diag_liberar(&lista);

    /* Prueba 2: el archivo temporal se borró y el original nunca se creó. */
    VERIFICAR(!existe("tests/.ide_editor.bal"));
    VERIFICAR(!existe("tests/editor.bal"));

    /* Prueba 3: un archivo que importa un módulo de su carpeta no tiene errores.
     * (tests/modulos/matematicas.bal existe; el texto se analiza como si
     * estuviera guardado en esa carpeta.) */
    const char *con_bring =
        "bring matematicas aka math;\n"
        "\n"
        "create_funk #declare_infinite_void# main() {\n"
        "}\n";
    VERIFICAR(ide_analizar_texto(con_bring, strlen(con_bring),
                                 "tests/modulos/nuevo.bal", &lista) == 0);
    VERIFICAR(lista.cantidad == 0);
    diag_liberar(&lista);

    /* Prueba 4: una llave sin cerrar. El parser se queja del final del
     * archivo (ya traducido a español). La revisión de delimitadores YA NO
     * agrega "falta cerrar este '{'": el LL(1) con recuperación explica el
     * error (su arreglo es "agregar '}' para cerrar la '{' de la linea 1"),
     * así que sería el mismo error repetido. */
    const char *sin_cerrar =
        "create_funk #declare_infinite_void# main() {\n"
        "    numero * declare_int : 10;\n";
    VERIFICAR(ide_analizar_texto(sin_cerrar, strlen(sin_cerrar),
                                 "tests/editor.bal", &lista) == 0);
    int vio_parser = 0, vio_llave = 0;
    for (size_t i = 0; i < lista.cantidad; ++i) {
        const Diagnostico *d = &lista.elementos[i];
        /* La tabla LL(1) predice qué falta, y el mensaje lo dice. */
        if (d->fase == DIAG_SINTACTICO && strcmp(d->mensaje, "el archivo termina antes de tiempo; "
                                              "se esperaba '}'") == 0) vio_parser = 1;
        if (d->fase == DIAG_SINTACTICO && d->linea == 1 && d->columna == 44 &&
            strcmp(d->mensaje, "falta cerrar este '{' con '}'") == 0) vio_llave = 1;
    }
    VERIFICAR(vio_parser);
    VERIFICAR(!vio_llave);
    VERIFICAR(lista.cantidad == 1);
    diag_liberar(&lista);

    /* Prueba 5: sin ruta (archivo nuevo) usa el nombre "sin_nombre.bal". */
    VERIFICAR(ide_analizar_texto(con_error, strlen(con_error), NULL, &lista) == 0);
    VERIFICAR(lista.cantidad == 1);
    if (lista.cantidad == 1) {
        VERIFICAR(strcmp(lista.elementos[0].archivo, IDE_SIN_NOMBRE) == 0);
    }
    VERIFICAR(!existe(".ide_sin_nombre.bal"));
    diag_liberar(&lista);

    /* Prueba 6: el mensaje del parser dice qué palabra estaba escrita, y
     * qué faltaba según la tabla LL(1). Falta el ';' de la línea 2, así que
     * el parser tropieza con "numero" al inicio de la línea 3. */
    const char *sin_punto_y_coma =
        "create_funk #declare_infinite_void# main() {\n"
        "    numero * declare_int : 10\n"
        "    numero : 5;\n"
        "}\n";
    VERIFICAR(ide_analizar_texto(sin_punto_y_coma, strlen(sin_punto_y_coma),
                                 "tests/editor.bal", &lista) == 0);
    VERIFICAR(lista.cantidad == 1);
    if (lista.cantidad == 1) {
        VERIFICAR(lista.elementos[0].linea == 3 && lista.elementos[0].columna == 5);
        VERIFICAR(strcmp(lista.elementos[0].mensaje,
                         "no se esperaba el nombre 'numero' en este lugar; se esperaba ';'") == 0);
    }
    diag_liberar(&lista);

    /* Prueba: se muestran TODOS los errores de sintaxis, no solo el primero.
     * Bison se detiene en el primero (línea 5); los demás los encuentra el
     * LL(1) con recuperación a nivel de frase, incluidas las palabras clave
     * mal escritas. Como con esas correcciones el programa queda válido,
     * no se agrega "el '}' sobra" (era solo consecuencia del '{' que falta). */
    {
        const char *varios =
            "create_funk #declare_infinite_void# main() {\n"   /* 1 */
            "    n * declare_int : 5;\n"                          /* 2 */
            "    resultado * declare_int : 1\n"                   /* 3: falta ';' */
            "    i * declar_int : 2;\n"                           /* 4: declar_int */
            "    whethr (n <= 1)\n"                               /* 5: whethr, falta '{' */
            "        resultado : 1;\n"                            /* 6 */
            "    }\n"                                             /* 7 */
            "}\n";                                                /* 8 */
        VERIFICAR(ide_analizar_texto(varios, strlen(varios), "tests/editor.bal", &lista) == 0);
        VERIFICAR(lista.cantidad == 4);
        if (lista.cantidad == 4) {
            VERIFICAR(lista.elementos[0].linea == 4 && lista.elementos[0].columna == 5);
            VERIFICAR(lista.elementos[1].linea == 4 && lista.elementos[1].columna == 9);
            VERIFICAR(strcmp(lista.elementos[1].mensaje,
                             "no se esperaba el nombre 'declar_int' en este lugar; "
                             "parece la palabra 'declare_int' mal escrita") == 0);
            VERIFICAR(lista.elementos[2].linea == 5 && lista.elementos[2].columna == 5);
            VERIFICAR(strcmp(lista.elementos[2].mensaje,
                             "no se esperaba el nombre 'whethr' en este lugar; "
                             "parece la palabra 'whether' mal escrita") == 0);
            VERIFICAR(lista.elementos[3].linea == 6 && lista.elementos[3].columna == 9);
        }
        diag_liberar(&lista);
    }

    /* Prueba: cada error se marca donde ocurre, aunque dos caigan en el mismo
     * token. Falta el ';' de la línea 3 y "wheter" está mal escrita: los dos
     * se detectan en "wheter" (línea 4). También faltan el ';' de la línea 6
     * y la '}' del final. */
    {
        const char *dos_en_uno =
            "create_funk #declare_infinite_void# main() {\n"  /* 1 */
            "    i * declare_int : 2;\n"                         /* 2 */
            "    n * declare_int : 1\n"                          /* 3: falta ';' */
            "    wheter (n <= 1) {\n"                            /* 4: wheter */
            "        i : i gauss 1\n"                            /* 5: falta ';' */
            "    }\n";                                           /* 6, y falta '}' */
        VERIFICAR(ide_analizar_texto(dos_en_uno, strlen(dos_en_uno), "tests/editor.bal", &lista) == 0);
        VERIFICAR(lista.cantidad == 4);
        if (lista.cantidad == 4) {
            VERIFICAR(lista.elementos[0].linea == 4 && lista.elementos[1].linea == 4);
            VERIFICAR(strstr(lista.elementos[0].mensaje, "mal escrita") != NULL);
            VERIFICAR(strcmp(lista.elementos[1].mensaje,
                             "no se esperaba la palabra 'whether' en este lugar; se esperaba ';'") == 0);
            VERIFICAR(lista.elementos[2].linea == 6);
            VERIFICAR(lista.elementos[3].linea == 7);
        }
        diag_liberar(&lista);
    }

    /* Prueba: un nombre parecido a una palabra clave, pero bien usado como
     * nombre, NO se marca como palabra mal escrita. */
    {
        const char *nombre_parecido =
            "create_funk #declare_infinite_void# main() {\n"
            "    whales * declare_int : 1;\n"
            "    whales : whales gauss 1;\n"
            "}\n";
        VERIFICAR(ide_analizar_texto(nombre_parecido, strlen(nombre_parecido),
                                     "tests/editor.bal", &lista) == 0);
        VERIFICAR(lista.cantidad == 0);
        diag_liberar(&lista);
    }

    if (fallos > 0) {
        fprintf(stderr, "test_ide_analisis: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_analisis: OK");
    return 0;
}
