/*
 * Pruebas de la corrección automática (src/IDE/ide_arreglos.c).
 *
 * Cada caso hace lo mismo que haría la persona en el IDE:
 *   1. Analiza un código con un error.
 *   2. Pide el arreglo de ese error y revisa la descripción.
 *   3. Aplica el arreglo.
 *   4. Vuelve a analizar: el código corregido ya no debe tener errores.
 *
 * Se ejecuta con: make test-ide-arreglos
 */
#include "ide_analisis.h"
#include "ide_arreglos.h"

#include <stdio.h>
#include <stdlib.h>
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

/* Cuántos errores tiene un texto. */
static size_t contar_errores(const char *texto, size_t largo)
{
    DiagLista lista = {0};
    if (ide_analizar_texto(texto, largo, "tests/editor.bal", &lista) != 0) {
        diag_liberar(&lista);
        return (size_t)-1;
    }
    size_t cantidad = lista.cantidad;
    diag_liberar(&lista);
    return cantidad;
}

/*
 * Analiza "codigo", toma el PRIMER error que tenga arreglo, revisa que su
 * descripción sea "descripcion", lo aplica y revisa que el resultado sea
 * "corregido" y que ya no tenga errores.
 */
static void revisar(int linea, const char *codigo, const char *descripcion,
                    const char *corregido)
{
    size_t largo = strlen(codigo);
    DiagLista lista = {0};
    if (ide_analizar_texto(codigo, largo, "tests/editor.bal", &lista) != 0) {
        fprintf(stderr, "FALLO en la linea %d: no se pudo analizar\n", linea);
        fallos++;
        return;
    }

    /* Buscar el primer error que tenga arreglo. */
    IdeArreglo arreglo;
    int hay = 0;
    for (size_t i = 0; i < lista.cantidad && !hay; ++i) {
        hay = ide_proponer_arreglo(codigo, largo, &lista.elementos[i], &arreglo) == 1;
    }
    diag_liberar(&lista);

    if (!hay) {
        fprintf(stderr, "FALLO en la linea %d: no se propuso ningun arreglo\n", linea);
        fallos++;
        return;
    }
    if (strcmp(arreglo.descripcion, descripcion) != 0) {
        fprintf(stderr, "FALLO en la linea %d:\n  descripcion esperada: %s\n  obtenida: %s\n",
                linea, descripcion, arreglo.descripcion);
        fallos++;
    }

    size_t nuevo_largo = 0;
    char *nuevo = ide_aplicar_arreglo(codigo, largo, &arreglo, &nuevo_largo);
    ide_liberar_arreglo(&arreglo);

    if (nuevo == NULL || strcmp(nuevo, corregido) != 0) {
        fprintf(stderr, "FALLO en la linea %d:\n  codigo esperado:\n%s\n  obtenido:\n%s\n",
                linea, corregido, nuevo ? nuevo : "(NULL)");
        fallos++;
    } else if (contar_errores(nuevo, nuevo_largo) != 0) {
        fprintf(stderr, "FALLO en la linea %d: el codigo corregido todavia tiene errores\n", linea);
        fallos++;
    }
    free(nuevo);
}

/* El inicio y el final de main, para no repetirlos en cada caso. */
#define INICIO "create_funk #declare_infinite_void# main() {\n"
#define FINAL  "}\n"

int main(void)
{
    /* 1. Falta ';': el parser se queja en la línea 3, pero el ';' va en la 2. */
    revisar(__LINE__,
            INICIO "    numero * declare_int : 10\n    numero : 5;\n" FINAL,
            "agregar ';' al final de la linea 2",
            INICIO "    numero * declare_int : 10;\n    numero : 5;\n" FINAL);

    /* 2. Falta ';' antes de la llave que cierra la función. */
    revisar(__LINE__,
            INICIO "    numero * declare_int : 10\n" FINAL,
            "agregar ';' al final de la linea 2",
            INICIO "    numero * declare_int : 10;\n" FINAL);

    /* 3. Falta ')' antes de '{': la tabla LL(1) lo predice. */
    revisar(__LINE__,
            INICIO "    x * declare_int : 1;\n    whether (x == 1 {\n    }\n" FINAL,
            "agregar ')' antes de '{' en la linea 3",
            INICIO "    x * declare_int : 1;\n    whether (x == 1) {\n    }\n" FINAL);

    /* 4. Una llave sin cerrar: la tabla predice '}', y se pone en una línea
     *    nueva con la sangría de la línea que abrió la llave. */
    revisar(__LINE__,
            INICIO "    numero * declare_int : 10;\n",
            "agregar '}' para cerrar la '{' de la linea 1",
            INICIO "    numero * declare_int : 10;\n" FINAL);

    /* 5. Igual, pero el archivo no termina en salto de línea. */
    revisar(__LINE__,
            INICIO "    numero * declare_int : 10;",
            "agregar '}' para cerrar la '{' de la linea 1",
            INICIO "    numero * declare_int : 10;\n}");

    /* 6. Un '(' sin cerrar: la tabla predice ')' justo antes del ';'. */
    revisar(__LINE__,
            INICIO "    numero * declare_int : (10 gauss 2;\n" FINAL,
            "agregar ')' antes de ';' en la linea 2",
            INICIO "    numero * declare_int : (10 gauss 2);\n" FINAL);

    /* 7. Un ')' que sobra: la tabla no espera nada que lo arregle, así que
     *    se prueban correcciones y la mejor es borrarlo. */
    revisar(__LINE__,
            INICIO "    numero * declare_int : 10);\n" FINAL,
            "borrar ')' de la linea 2",
            INICIO "    numero * declare_int : 10;\n" FINAL);

    /* 8. Un cierre equivocado en la misma línea: se cambia. */
    revisar(__LINE__,
            INICIO "    numero * declare_int : (10 gauss 2];\n" FINAL,
            "cambiar ']' por ')'",
            INICIO "    numero * declare_int : (10 gauss 2);\n" FINAL);

    /* 9. Un error semántico no tiene arreglo automático. */
    {
        const char *codigo = INICIO "    numero : 5;\n" FINAL;
        DiagLista lista = {0};
        VERIFICAR(ide_analizar_texto(codigo, strlen(codigo), "tests/editor.bal", &lista) == 0);
        VERIFICAR(lista.cantidad == 1);
        IdeArreglo arreglo;
        if (lista.cantidad == 1) {
            VERIFICAR(ide_proponer_arreglo(codigo, strlen(codigo),
                                           &lista.elementos[0], &arreglo) == 0);
        }
        diag_liberar(&lista);
    }

    /* 10. Aplicar respeta las letras de varios bytes: la columna 3 de "ñañ"
     *     está después de "ña", no después del tercer byte. */
    {
        IdeArreglo arreglo = {1, 3, 0, "X", "prueba"};
        size_t nuevo_largo = 0;
        char *nuevo = ide_aplicar_arreglo("ñañ", strlen("ñañ"), &arreglo, &nuevo_largo);
        VERIFICAR(nuevo != NULL && strcmp(nuevo, "ñaXñ") == 0);
        free(nuevo);

        /* Y borrar un carácter de varios bytes lo borra completo. */
        IdeArreglo borrar = {1, 2, 1, "", "prueba"};
        nuevo = ide_aplicar_arreglo("ñañ", strlen("ñañ"), &borrar, &nuevo_largo);
        VERIFICAR(nuevo != NULL && strcmp(nuevo, "ññ") == 0);
        free(nuevo);

        /* Una posición que no existe no se aplica. */
        IdeArreglo fuera = {5, 1, 0, "X", "prueba"};
        VERIFICAR(ide_aplicar_arreglo("ñañ", strlen("ñañ"), &fuera, &nuevo_largo) == NULL);
    }

    /* 11. Errores del parser: UN arreglo, el que predice la tabla LL(1). */

    /* La tabla obliga a dos tokens seguidos: después del bloque de whale
     * solo puede ir 'stop', y después de 'stop' solo ';'. */
    revisar(__LINE__,
            INICIO "    x * declare_int : 0;\n"
                   "    whale (x < 3) {\n        x : x gauss 1;\n    }\n" FINAL,
            "agregar 'stop;' al final de la linea 5",
            INICIO "    x * declare_int : 0;\n"
                   "    whale (x < 3) {\n        x : x gauss 1;\n    } stop;\n" FINAL);

    /* Falta el '#' después de create_funk: la tabla solo permite '#'. */
    revisar(__LINE__,
            "create_funk declare_infinite_void# main() {\n}\n",
            "agregar '#' antes de 'declare_infinite_void' en la linea 1",
            "create_funk # declare_infinite_void# main() {\n}\n");

    /* Aquí la tabla NO obliga a un token (después de 'gauss' puede ir
     * cualquier valor): se prueban correcciones y gana borrar el 'gauss'. */
    revisar(__LINE__,
            INICIO "    x * declare_int : 1;\n    x : 5 gauss ;\n" FINAL,
            "borrar 'gauss' de la linea 3",
            INICIO "    x * declare_int : 1;\n    x : 5 ;\n" FINAL);

    /* Un número de más: lo más sencillo es borrarlo (la columna dice cuál). */
    revisar(__LINE__,
            "create_funk #declare_int# uno() {\n    give 1 1;\n}\n"
            "create_funk #declare_infinite_void# main() {\n}\n",
            "borrar '1' de la linea 2, columna 12",
            "create_funk #declare_int# uno() {\n    give 1;\n}\n"
            "create_funk #declare_infinite_void# main() {\n}\n");

    /* 12. Error semántico: una variable usada como función en una sentencia
     *     "i ( i gauss 1);" era una asignación. */
    revisar(__LINE__,
            INICIO "    i * declare_int : 1;\n    i (  i gauss 1);\n" FINAL,
            "es una asignacion: escribir 'i : i gauss 1'",
            INICIO "    i * declare_int : 1;\n    i : i gauss 1;\n" FINAL);

    /* Con dos argumentos no hay una asignación clara: no se propone nada. */
    {
        const char *codigo = INICIO "    i * declare_int : 1;\n    i (1, 2);\n" FINAL;
        DiagLista lista = {0};
        VERIFICAR(ide_analizar_texto(codigo, strlen(codigo), "tests/editor.bal", &lista) == 0);
        VERIFICAR(lista.cantidad == 1);
        if (lista.cantidad == 1) {
            IdeArreglo arreglo;
            VERIFICAR(ide_proponer_arreglo(codigo, strlen(codigo), &lista.elementos[0], &arreglo) == 0);
        }
        diag_liberar(&lista);
    }

    /* Dentro de una expresión ("x : i(3);") tampoco: no empieza la sentencia. */
    {
        const char *codigo = INICIO "    i * declare_int : 1;\n    i : i(3);\n" FINAL;
        DiagLista lista = {0};
        VERIFICAR(ide_analizar_texto(codigo, strlen(codigo), "tests/editor.bal", &lista) == 0);
        for (size_t i = 0; i < lista.cantidad; ++i) {
            IdeArreglo arreglo;
            VERIFICAR(ide_proponer_arreglo(codigo, strlen(codigo), &lista.elementos[i], &arreglo) == 0);
        }
        diag_liberar(&lista);
    }

    if (fallos > 0) {
        fprintf(stderr, "test_ide_arreglos: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_arreglos: OK");
    return 0;
}
