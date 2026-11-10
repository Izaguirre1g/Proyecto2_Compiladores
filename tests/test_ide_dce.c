/*
 * Pruebas del botón "Código muerto" del IDE (src/IDE/ide_dce.c), sin abrir
 * ventanas: se le da el texto del editor y se revisa qué líneas marca.
 *
 * Se ejecuta con: make test-ide-dce
 */
#include "ide_dce.h"

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

/* Lee un archivo completo (NULL si no se pudo). */
static char *leer_archivo(const char *ruta, size_t *largo)
{
    FILE *f = fopen(ruta, "rb");
    if (f == NULL) return NULL;
    char *texto = malloc(1 << 16);
    if (texto != NULL) {
        *largo = fread(texto, 1, (1 << 16) - 1, f);
        texto[*largo] = '\0';
    }
    fclose(f);
    return texto;
}

/* La entrada de la línea "linea", o NULL si esa línea no tiene código muerto. */
static const IdeDceLinea *buscar(const IdeDce *d, size_t linea)
{
    for (size_t i = 0; i < d->cantidad_lineas; ++i) {
        if (d->lineas[i].linea == linea) return &d->lineas[i];
    }
    return NULL;
}

/* El programa 1 de las pruebas del DCE, tal como lo vería el IDE. */
static void prueba_llamadas(void)
{
    size_t largo = 0;
    char *texto = leer_archivo("tests/dce/dce_llamadas.bal", &largo);
    VERIFICAR(texto != NULL);
    if (texto == NULL) return;

    IdeDce d;
    VERIFICAR(ide_dce_analizar(texto, largo, "tests/dce/dce_llamadas.bal", &d) == 0);
    VERIFICAR(d.estado == IDE_DCE_LISTO);
    VERIFICAR(d.resultado.cantidad_eliminadas == 5);
    VERIFICAR(d.cantidad_lineas == 3);

    /* Línea 12, "b * declare_int : a pitagoras 2;": se elimina COMPLETA. */
    const IdeDceLinea *b = buscar(&d, 12);
    VERIFICAR(b != NULL && b->completa && b->eliminadas == 2);
    VERIFICAR(b != NULL && strcmp(b->detalle, "$t5 = a pitagoras 2; b = $t5") == 0);

    /* Línea 13, "c * declare_int : registrar(datos, a);": solo se va la
     * copia a c; la llamada (y sus argumentos) se queda: NO es completa. */
    const IdeDceLinea *c = buscar(&d, 13);
    VERIFICAR(c != NULL && !c->completa && c->eliminadas == 1 && c->total == 4);

    /* Línea 15, "d * declare_int : a gauss 1;": completa. */
    const IdeDceLinea *dd = buscar(&d, 15);
    VERIFICAR(dd != NULL && dd->completa);

    /* La línea 14 es solo una llamada: no se toca. */
    VERIFICAR(buscar(&d, 14) == NULL);

    /* Los textos para la ventana de detalle. */
    VERIFICAR(d.ir_antes != NULL && strstr(d.ir_antes, "b = $t5") != NULL);
    VERIFICAR(d.ir_despues != NULL && strstr(d.ir_despues, "b = $t5") == NULL);
    VERIFICAR(d.ir_despues != NULL && strstr(d.ir_despues, "call registrar") != NULL);
    VERIFICAR(d.vivas != NULL && strstr(d.vivas, "IN  =") != NULL);

    ide_dce_liberar(&d);
    free(texto);
}

/* Un programa con errores no se analiza: hay que corregirlo antes. */
static void prueba_con_errores(void)
{
    const char *texto = "create_funk #declare_infinite_void# main() {\n    x : 5;\n}\n";
    IdeDce d;
    VERIFICAR(ide_dce_analizar(texto, strlen(texto), "tests/dce/editor.bal", &d) == 0);
    VERIFICAR(d.estado == IDE_DCE_CON_ERRORES);
    VERIFICAR(d.cantidad_lineas == 0);
    ide_dce_liberar(&d);
}

/* Un programa válido que la IR todavía no soporta: se dice por qué y dónde. */
static void prueba_sin_ir(void)
{
    const char *texto =
        "create_funk #declare_infinite_void# main() {\n"
        "    s * declare_int : 0;\n"
        "    cycle i let 3 until 0 step neumann 1 {\n"
        "        s : s gauss i;\n"
        "    } endgame;\n"
        "}\n";
    IdeDce d;
    VERIFICAR(ide_dce_analizar(texto, strlen(texto), "tests/dce/editor.bal", &d) == 0);
    VERIFICAR(d.estado == IDE_DCE_SIN_IR);
    VERIFICAR(d.motivo != NULL && strncmp(d.motivo, "línea 3: cycle", strlen("línea 3: cycle")) == 0);
    ide_dce_liberar(&d);
}

/* Sin código muerto: listo, sin líneas marcadas. */
static void prueba_sin_codigo_muerto(void)
{
    const char *texto =
        "create_funk #declare_int# doble(n * declare_int) {\n"
        "    give n pitagoras 2;\n"
        "}\n"
        "create_funk #declare_infinite_void# main() {\n"
        "    doble(3);\n"
        "}\n";
    IdeDce d;
    VERIFICAR(ide_dce_analizar(texto, strlen(texto), "tests/dce/editor.bal", &d) == 0);
    VERIFICAR(d.estado == IDE_DCE_LISTO);
    VERIFICAR(d.cantidad_lineas == 0);
    VERIFICAR(d.resultado.cantidad_eliminadas == 0);
    ide_dce_liberar(&d);
}

int main(void)
{
    prueba_llamadas();
    prueba_con_errores();
    prueba_sin_ir();
    prueba_sin_codigo_muerto();

    if (fallos > 0) {
        fprintf(stderr, "test_ide_dce: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_dce: OK");
    return 0;
}
