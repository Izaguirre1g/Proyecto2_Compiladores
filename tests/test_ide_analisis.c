/*
 * Pruebas del análisis del editor (src/ide_analisis.c).
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
     * archivo, y la revisión de delimitadores señala la llave exacta. */
    const char *sin_cerrar =
        "create_funk #declare_infinite_void# main() {\n"
        "    numero * declare_int : 10;\n";
    VERIFICAR(ide_analizar_texto(sin_cerrar, strlen(sin_cerrar),
                                 "tests/editor.bal", &lista) == 0);
    int vio_parser = 0, vio_llave = 0;
    for (size_t i = 0; i < lista.cantidad; ++i) {
        const Diagnostico *d = &lista.elementos[i];
        if (d->fase == DIAG_SINTACTICO && strstr(d->mensaje, "syntax error")) vio_parser = 1;
        if (d->fase == DIAG_SINTACTICO && d->linea == 1 && d->columna == 44 &&
            strcmp(d->mensaje, "falta cerrar este '{' con '}'") == 0) vio_llave = 1;
    }
    VERIFICAR(vio_parser);
    VERIFICAR(vio_llave);
    diag_liberar(&lista);

    /* Prueba 5: sin ruta (archivo nuevo) usa el nombre "sin_nombre.bal". */
    VERIFICAR(ide_analizar_texto(con_error, strlen(con_error), NULL, &lista) == 0);
    VERIFICAR(lista.cantidad == 1);
    if (lista.cantidad == 1) {
        VERIFICAR(strcmp(lista.elementos[0].archivo, IDE_SIN_NOMBRE) == 0);
    }
    VERIFICAR(!existe(".ide_sin_nombre.bal"));
    diag_liberar(&lista);

    if (fallos > 0) {
        fprintf(stderr, "test_ide_analisis: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_analisis: OK");
    return 0;
}
