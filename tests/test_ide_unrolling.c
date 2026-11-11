/*
 * Pruebas del botón "Loop unrolling" del IDE (src/IDE/ide_unrolling.c), sin
 * abrir ventanas. Usa los ejemplos de Meibel (loop_unrolling/pruebas/) y
 * revisa que el IDE dé las mismas decisiones que "balc --unroll".
 *
 * Se ejecuta con: make test-ide-unrolling
 */
#include "ide_unrolling.h"

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

/* Analiza un ejemplo de Meibel con el factor dado. */
static int analizar(const char *ruta, unsigned factor, IdeUnrolling *u)
{
    size_t largo = 0;
    char *texto = leer_archivo(ruta, &largo);
    if (texto == NULL) return -1;
    int estado = ide_unrolling_analizar(texto, largo, ruta, factor, u);
    free(texto);
    return estado;
}

/* Cuántas líneas de la IR después son nuevas ('N'). */
static size_t nuevas(const LuVista *v)
{
    size_t n = 0;
    for (size_t i = 0; i < v->cantidad; ++i) n += v->lineas[i].tipo == 'N';
    return n;
}

int main(void)
{
    IdeUnrolling u;
    char decision[200];

    /* 01: N = 5 conocido y pequeño: desenrollado TOTAL (13 -> 24 instrucciones). */
    VERIFICAR(analizar("loop_unrolling/pruebas/01_n_constante.bal", 0, &u) == 0);
    VERIFICAR(u.estado == IDE_LU_LISTO && u.factor == 4);
    VERIFICAR(u.resumen.ciclos == 1 && u.resumen.totales == 1);
    VERIFICAR(u.antes.instr == 13 && u.despues.instr == 24);
    VERIFICAR(u.despues.saltos == 0 && nuevas(&u.despues) > 0);
    if (u.resumen.cantidad_detalle == 1) {
        ide_unrolling_decision(&u.resumen.detalle[0], decision, sizeof decision);
        VERIFICAR(strcmp(decision, "TOTAL: 5 copias, sin saltos") == 0);
        VERIFICAR(u.resumen.detalle[0].linea == 5);
    }
    ide_unrolling_liberar(&u);

    /* 02: un ciclo con N desconocido (parcial + residuo) y otro con N = 3 (total). */
    VERIFICAR(analizar("loop_unrolling/pruebas/02_variable_pequeno.bal", 0, &u) == 0);
    VERIFICAR(u.resumen.ciclos == 2 && u.resumen.totales == 1 && u.resumen.parciales == 1);
    if (u.resumen.cantidad_detalle == 2) {
        ide_unrolling_decision(&u.resumen.detalle[0], decision, sizeof decision);
        VERIFICAR(strcmp(decision, "PARCIAL ×4 + ciclo residuo") == 0);
    }
    ide_unrolling_liberar(&u);

    /* 03: N = 1002: parcial con factor k. Con k = 4: 250 vueltas + 2 sueltas;
     * al cambiar el factor a 8 (como el selector de la ventana): 125 + 2. */
    VERIFICAR(analizar("loop_unrolling/pruebas/03_variable_grande.bal", 0, &u) == 0);
    if (u.resumen.cantidad_detalle == 1) {
        ide_unrolling_decision(&u.resumen.detalle[0], decision, sizeof decision);
        VERIFICAR(strcmp(decision, "PARCIAL ×4: 250 vueltas + 2 sueltas") == 0);
    }
    ide_unrolling_liberar(&u);
    VERIFICAR(analizar("loop_unrolling/pruebas/03_variable_grande.bal", 8, &u) == 0);
    VERIFICAR(u.factor == 8);
    if (u.resumen.cantidad_detalle == 1) {
        ide_unrolling_decision(&u.resumen.detalle[0], decision, sizeof decision);
        VERIFICAR(strcmp(decision, "PARCIAL ×8: 125 vueltas + 2 sueltas") == 0);
    }
    ide_unrolling_liberar(&u);

    /* 05: tres ciclos que no se pueden desenrollar: la IR no cambia y cada
     * uno dice por qué. */
    VERIFICAR(analizar("loop_unrolling/pruebas/05_no_se_desenrolla.bal", 0, &u) == 0);
    VERIFICAR(u.resumen.ciclos == 3 && u.resumen.rechazados == 3);
    VERIFICAR(u.antes.instr == u.despues.instr && nuevas(&u.despues) == 0);
    if (u.resumen.cantidad_detalle == 3) {
        ide_unrolling_decision(&u.resumen.detalle[0], decision, sizeof decision);
        VERIFICAR(strcmp(decision, "sin cambios: el cuerpo modifica el iterador") == 0);
    }
    ide_unrolling_liberar(&u);

    /* Un código con errores no se analiza. */
    const char *con_errores = "create_funk #declare_infinite_void# main() {\n    x : 5;\n}\n";
    VERIFICAR(ide_unrolling_analizar(con_errores, strlen(con_errores),
                                     "loop_unrolling/pruebas/editor.bal", 0, &u) == 0);
    VERIFICAR(u.estado == IDE_LU_CON_ERRORES);
    ide_unrolling_liberar(&u);

    if (fallos > 0) {
        fprintf(stderr, "test_ide_unrolling: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_unrolling: OK");
    return 0;
}
