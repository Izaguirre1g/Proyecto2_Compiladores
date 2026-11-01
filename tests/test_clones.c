/*
 * Pruebas de la detección de clones (src/clones.c).
 *
 * Se ejecuta con: make test-clones
 */
#include "clones.h"

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

/* Arma un ClonFuente a partir de un nombre y un texto. */
static ClonFuente fuente(const char *nombre, const char *texto)
{
    ClonFuente f = {nombre, texto, strlen(texto)};
    return f;
}

/* Muestra la lista (sirve para ver qué salió cuando una prueba falla). */
static void mostrar(const ClonLista *lista)
{
    for (size_t i = 0; i < lista->cantidad; ++i) {
        const Clon *c = &lista->elementos[i];
        fprintf(stderr, "  clon Tipo %d, %s, %zu nodos:\n", (int)c->tipo, c->que, c->nodos);
        for (size_t j = 0; j < c->cantidad; ++j) {
            const ClonFragmento *f = &c->fragmentos[j];
            fprintf(stderr, "    %s %zu:%zu-%zu:%zu\n", f->archivo, f->linea, f->columna,
                    f->linea_fin, f->columna_fin);
        }
    }
}

/* Una función factorial. Se usa en varias pruebas. */
static const char *FACTORIAL =
    "create_funk #declare_int# factorial(n * declare_int) {\n"
    "    resultado * declare_int : 1;\n"
    "    i * declare_int : 2;\n"
    "    whale (i <= n) {\n"
    "        resultado : resultado pitagoras i;\n"
    "        i : i gauss 1;\n"
    "    } stop;\n"
    "    give resultado;\n"
    "}\n";

int main(void)
{
    /* Prueba 1: la misma función en dos archivos, con otros espacios,
     * comentarios y paréntesis de más. Es un clon Tipo I de la función
     * completa, y NO se reportan también sus partes (el whale, etc.). */
    {
        const char *copia =
            "%% copia con otro formato\n"
            "create_funk #declare_int# factorial(n * declare_int) {\n"
            "  resultado * declare_int : 1;   i * declare_int : 2;\n"
            "  whale ((i <= n)) {\n"
            "    resultado : (resultado pitagoras i);\n"
            "    i : i gauss 1;   %% siguiente\n"
            "  } stop;\n"
            "  give resultado;\n"
            "}\n";
        ClonFuente fuentes[] = {fuente("a.bal", FACTORIAL), fuente("b.bal", copia)};
        ClonLista lista = {0};
        VERIFICAR(clones_buscar(fuentes, 2, CLONES_MINIMO_NODOS, &lista) == 0);
        VERIFICAR(lista.cantidad == 1);
        if (lista.cantidad == 1) {
            const Clon *c = &lista.elementos[0];
            VERIFICAR(c->tipo == CLON_TIPO_1);
            VERIFICAR(strcmp(c->que, "funcion") == 0);
            VERIFICAR(c->cantidad == 2);
            if (c->cantidad == 2) {
                VERIFICAR(strcmp(c->fragmentos[0].archivo, "a.bal") == 0);
                VERIFICAR(c->fragmentos[0].linea == 1 && c->fragmentos[0].columna == 1);
                VERIFICAR(c->fragmentos[0].linea_fin == 9);
                VERIFICAR(strcmp(c->fragmentos[1].archivo, "b.bal") == 0);
                VERIFICAR(c->fragmentos[1].linea == 2);  /* La línea 1 es el comentario. */
            }
        } else {
            mostrar(&lista);
        }
        clones_liberar(&lista);
    }

    /* Prueba 2: la misma función con otros nombres y valores: Tipo II. */
    {
        const char *renombrada =
            "create_funk #declare_int# producto(tope * declare_int) {\n"
            "    acumulado * declare_int : 1;\n"
            "    k * declare_int : 3;\n"
            "    whale (k <= tope) {\n"
            "        acumulado : acumulado pitagoras k;\n"
            "        k : k gauss 2;\n"
            "    } stop;\n"
            "    give acumulado;\n"
            "}\n";
        ClonFuente fuentes[] = {fuente("a.bal", FACTORIAL), fuente("c.bal", renombrada)};
        ClonLista lista = {0};
        VERIFICAR(clones_buscar(fuentes, 2, CLONES_MINIMO_NODOS, &lista) == 0);
        VERIFICAR(lista.cantidad == 1);
        if (lista.cantidad == 1) {
            VERIFICAR(lista.elementos[0].tipo == CLON_TIPO_2);
            VERIFICAR(strcmp(lista.elementos[0].que, "funcion") == 0);
        } else {
            mostrar(&lista);
        }
        clones_liberar(&lista);
    }

    /* Prueba 3: si se cambia un operador (gauss por neumann), cambia la
     * forma del código: ni la función ni el whale son clones. Solo quedan
     * iguales sentencias sueltas, que no llegan al tamaño mínimo. */
    {
        const char *otra_operacion =
            "create_funk #declare_int# factorial(n * declare_int) {\n"
            "    resultado * declare_int : 1;\n"
            "    i * declare_int : 2;\n"
            "    whale (i <= n) {\n"
            "        resultado : resultado pitagoras i;\n"
            "        i : i neumann 1;\n"
            "    } stop;\n"
            "    give resultado;\n"
            "}\n";
        ClonFuente fuentes[] = {fuente("a.bal", FACTORIAL), fuente("d.bal", otra_operacion)};
        ClonLista lista = {0};
        VERIFICAR(clones_buscar(fuentes, 2, CLONES_MINIMO_NODOS, &lista) == 0);
        VERIFICAR(lista.cantidad == 0);
        if (lista.cantidad != 0) mostrar(&lista);
        clones_liberar(&lista);
    }

    /* Prueba 4: dos funciones distintas en el MISMO archivo que comparten
     * un ciclo copiado: el clon es el whale, no las funciones. */
    {
        const char *mismo_archivo =
            "create_funk #declare_infinite_void# uno() {\n"
            "    i * declare_int : 0;\n"
            "    whale (i < 10) {\n"
            "        i : i gauss 1;\n"
            "        j * declare_int : i pitagoras 2;\n"
            "    } stop;\n"
            "}\n"
            "create_funk #declare_infinite_void# dos() {\n"
            "    x * declare_text : \"hola\";\n"
            "    whale (i < 10) {\n"
            "        i : i gauss 1;\n"
            "        j * declare_int : i pitagoras 2;\n"
            "    } stop;\n"
            "}\n";
        ClonFuente fuentes[] = {fuente("e.bal", mismo_archivo)};
        ClonLista lista = {0};
        VERIFICAR(clones_buscar(fuentes, 1, CLONES_MINIMO_NODOS, &lista) == 0);
        VERIFICAR(lista.cantidad == 1);
        if (lista.cantidad == 1) {
            const Clon *c = &lista.elementos[0];
            VERIFICAR(c->tipo == CLON_TIPO_1);
            VERIFICAR(strcmp(c->que, "ciclo whale") == 0);
            VERIFICAR(c->cantidad == 2);
            if (c->cantidad == 2) {
                VERIFICAR(c->fragmentos[0].linea == 3 && c->fragmentos[0].columna == 5);
                VERIFICAR(c->fragmentos[1].linea == 10 && c->fragmentos[1].columna == 5);
            }
        } else {
            mostrar(&lista);
        }
        clones_liberar(&lista);
    }

    /* Prueba 5: tres copias, dos idénticas y una renombrada. Sale un clon
     * Tipo I con las dos idénticas y un Tipo II con las tres. */
    {
        const char *renombrada =
            "create_funk #declare_int# producto(tope * declare_int) {\n"
            "    acumulado * declare_int : 1;\n"
            "    k * declare_int : 2;\n"
            "    whale (k <= tope) {\n"
            "        acumulado : acumulado pitagoras k;\n"
            "        k : k gauss 1;\n"
            "    } stop;\n"
            "    give acumulado;\n"
            "}\n";
        ClonFuente fuentes[] = {fuente("a.bal", FACTORIAL), fuente("b.bal", FACTORIAL),
                                fuente("c.bal", renombrada)};
        ClonLista lista = {0};
        VERIFICAR(clones_buscar(fuentes, 3, CLONES_MINIMO_NODOS, &lista) == 0);
        VERIFICAR(lista.cantidad == 2);
        if (lista.cantidad == 2) {
            VERIFICAR(lista.elementos[0].tipo == CLON_TIPO_1 && lista.elementos[0].cantidad == 2);
            VERIFICAR(lista.elementos[1].tipo == CLON_TIPO_2 && lista.elementos[1].cantidad == 3);
        } else {
            mostrar(&lista);
        }
        clones_liberar(&lista);
    }

    /* Prueba 6: sentencias cortas repetidas no son clones (tamaño mínimo). */
    {
        const char *corto =
            "create_funk #declare_infinite_void# main() {\n"
            "    i * declare_int : 0;\n"
            "    i : i gauss 1;\n"
            "    i : i gauss 1;\n"
            "}\n";
        ClonFuente fuentes[] = {fuente("f.bal", corto)};
        ClonLista lista = {0};
        VERIFICAR(clones_buscar(fuentes, 1, CLONES_MINIMO_NODOS, &lista) == 0);
        VERIFICAR(lista.cantidad == 0);
        clones_liberar(&lista);

        /* Pero con un mínimo más bajo, sí aparecen. */
        VERIFICAR(clones_buscar(fuentes, 1, 4, &lista) == 0);
        VERIFICAR(lista.cantidad == 1);
        if (lista.cantidad == 1) {
            VERIFICAR(strcmp(lista.elementos[0].que, "asignacion") == 0);
        }
        clones_liberar(&lista);
    }

    /* Prueba 7: un archivo con errores de sintaxis se salta y se cuenta. */
    {
        const char *roto = "create_funk #declare_int# f( {\n";
        ClonFuente fuentes[] = {fuente("a.bal", FACTORIAL), fuente("roto.bal", roto),
                                fuente("b.bal", FACTORIAL)};
        ClonLista lista = {0};
        VERIFICAR(clones_buscar(fuentes, 3, CLONES_MINIMO_NODOS, &lista) == 0);
        VERIFICAR(lista.omitidos == 1);
        if (lista.omitidos == 1) {
            VERIFICAR(strcmp(lista.nombres_omitidos[0], "roto.bal") == 0);
        }
        VERIFICAR(lista.cantidad == 1);
        clones_liberar(&lista);
    }

    /* Prueba 8: sin archivos no hay clones. */
    {
        ClonLista lista = {0};
        VERIFICAR(clones_buscar(NULL, 0, CLONES_MINIMO_NODOS, &lista) == 0);
        VERIFICAR(lista.cantidad == 0);
        clones_liberar(&lista);
    }

    if (fallos > 0) {
        fprintf(stderr, "test_clones: %d fallos\n", fallos);
        return 1;
    }
    puts("test_clones: OK");
    return 0;
}
