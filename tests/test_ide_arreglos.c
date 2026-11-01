/*
 * Pruebas de la corrección automática (src/ide_arreglos.c).
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

    /* 3. Falta ')' antes de '{'. */
    revisar(__LINE__,
            INICIO "    x * declare_int : 1;\n    whether (x == 1 {\n    }\n" FINAL,
            "agregar ')' antes de '{' en la linea 3",
            INICIO "    x * declare_int : 1;\n    whether (x == 1) {\n    }\n" FINAL);

    /* 4. Una llave sin cerrar: se agrega '}' al final, con la sangría de su línea. */
    revisar(__LINE__,
            INICIO "    numero * declare_int : 10;\n",
            "agregar '}' al final del archivo para cerrar la '{' de la linea 1",
            INICIO "    numero * declare_int : 10;\n" FINAL);

    /* 5. Igual, pero el archivo no termina en salto de línea. */
    revisar(__LINE__,
            INICIO "    numero * declare_int : 10;",
            "agregar '}' al final del archivo para cerrar la '{' de la linea 1",
            INICIO "    numero * declare_int : 10;\n" FINAL);

    /* 6. Un '(' sin cerrar: el ')' va antes del ';' final. */
    revisar(__LINE__,
            INICIO "    numero * declare_int : (10 gauss 2;\n" FINAL,
            "agregar ')' para cerrar el '(' de la linea 2",
            INICIO "    numero * declare_int : (10 gauss 2);\n" FINAL);

    /* 7. Un ')' que sobra: se borra. */
    revisar(__LINE__,
            INICIO "    numero * declare_int : 10);\n" FINAL,
            "borrar el ')' que sobra",
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

    if (fallos > 0) {
        fprintf(stderr, "test_ide_arreglos: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_arreglos: OK");
    return 0;
}
