/*
 * Pruebas obligatorias del proyecto (tests/Pruebas_obligatorias/): 15
 * programas, 3 por cada optimización, para observar y medir su efecto por
 * separado. Por cada programa se comprueba, con el mismo módulo que usa el
 * botón "-O0 / -O2" del IDE (src/IDE/ide_optimizacion.c):
 *
 *   1. Que compila con -O0 (backend tradicional) y con -O2 (IR optimizada
 *      + cadena física VLIW), y que los dos binarios terminan en el
 *      simulador.
 *   2. Que el arreglo "resultados" de main tiene los valores esperados
 *      (calculados a mano, escritos en el comentario de cada programa), en
 *      los DOS niveles.
 *   3. Que -O0 y -O2 dejan TODAS las variables de main iguales (regla de
 *      oro: optimizar no cambia lo que hace el programa).
 *   4. Que la optimización de su categoría realmente actuó en -O2:
 *        unrolling       -> al menos tantos ciclos desenrollados como se esperan
 *        renombramiento  -> versiones nuevas y dependencias WAR y WAW quitadas
 *        código muerto   -> instrucciones eliminadas por el DCE
 *        reordenamiento  -> instrucciones movidas
 *        spilling        -> valores guardados en la pila Y vueltos a cargar
 *
 * Se ejecuta con: make test-pruebas-obligatorias
 */
#include "ide_optimizacion.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cuántas verificaciones fallaron. */
static int fallos = 0;

/* Si la condición es falsa, muestra el programa y la línea, y cuenta el fallo. */
#define VERIFICAR(condicion) do { \
    if (!(condicion)) { \
        fprintf(stderr, "FALLO (%s) en la linea %d: %s\n", actual, __LINE__, #condicion); \
        fallos++; \
    } \
} while (0)

/* El programa que se está revisando (para los mensajes de fallo). */
static const char *actual = "";

/* La categoría de cada programa: qué optimización tiene que actuar. */
typedef enum { UNROLL, RENOMBRE, DCE, REORDEN, SPILLING } Categoria;

typedef struct {
    const char *archivo;      /* Dentro de tests/Pruebas_obligatorias/. */
    Categoria categoria;
    const char *resultados;   /* El valor esperado de resultados[] en main. */
    const char *extra;        /* Otra variable de main que se revisa ("nombre=valor"), o NULL. */
    size_t minimo;            /* Para UNROLL: ciclos desenrollados; para DCE: instrucciones eliminadas. */
} Caso;

static const Caso CASOS[] = {
    {"01_unroll_n_constante.bal",           UNROLL,   "[28, 22, 121, -6]", NULL, 5},
    {"02_unroll_n_variable_pequena.bal",    UNROLL,   "[31, 0, 10, 33, 46, 9]", NULL, 2},
    {"03_unroll_n_grande.bal",              UNROLL,   "[502503, 335, 250, 128]", NULL, 3},
    {"04_renombre_war.bal",                 RENOMBRE, "[17, 22, 103, 6, 384, 22, 409]", NULL, 0},
    {"05_renombre_waw.bal",                 RENOMBRE, "[24, 35, 36, 18, 4, 22, 256, 377]", NULL, 0},
    {"06_renombre_war_waw_ciclos.bal",      RENOMBRE, "[6765, 1, 1705, 21, 1]", NULL, 0},
    {"07_dce_llamadas_con_efectos.bal",     DCE,      "[100, 60]", "datos=[60, 20, 30, 0, 0]", 1},
    {"08_dce_efectos_en_ciclos.bal",        DCE,      "[16, 15, 9]", "hist=[1, 2, 3, 4, 15]", 1},
    {"09_dce_operaciones_que_fallan.bal",   DCE,      "21", "res=[21, 7, 25, 10]", 1},
    {"10_reorden_saltos_condicionales.bal", REORDEN,  "[3, 1, 2, 2, 120, -20, 33, 4]", NULL, 0},
    {"11_reorden_funciones.bal",            REORDEN,  "[5040, 55, 5, 7, 3]", NULL, 0},
    {"12_reorden_memoria_y_ciclos.bal",     REORDEN,  "[-9, 21, 8, 10]", NULL, 0},
    {"13_spilling_expresiones.bal",         SPILLING, "[36, 2, -6, 106, -51]", NULL, 0},
    {"14_spilling_variables_vivas_en_ciclo.bal", SPILLING, "[210, 1540, 8855, -10, 10946, 4, -13]", NULL, 0},
    {"15_spilling_llamadas_y_recursion.bal", SPILLING, "[49, 56, 930, 91, 1035]", NULL, 0},
};

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

/* El valor final de una variable de main en un nivel (NULL si no está). */
static const char *valor(const IdeOptNivel *n, const char *nombre)
{
    for (size_t i = 0; i < n->cantidad_variables; ++i)
        if (strcmp(n->variables[i].nombre, nombre) == 0) return n->valores ? n->valores[i] : NULL;
    return NULL;
}

/* 1 si la variable "nombre" vale "esperado" en el nivel. */
static int vale(const IdeOptNivel *n, const char *nombre, const char *esperado)
{
    const char *v = valor(n, nombre);
    return v != NULL && strcmp(v, esperado) == 0;
}

/* Revisa "nombre=valor" (la variable extra) en el nivel. */
static int vale_extra(const IdeOptNivel *n, const char *extra)
{
    char nombre[64];
    const char *igual = strchr(extra, '=');
    size_t largo = igual != NULL ? (size_t)(igual - extra) : 0;
    if (largo == 0 || largo >= sizeof nombre) return 0;
    memcpy(nombre, extra, largo);
    nombre[largo] = '\0';
    return vale(n, nombre, igual + 1);
}

static void revisar(const Caso *c)
{
    char ruta[256];
    snprintf(ruta, sizeof ruta, "tests/Pruebas_obligatorias/%s", c->archivo);
    actual = c->archivo;
    size_t largo = 0;
    char *texto = leer_archivo(ruta, &largo);
    VERIFICAR(texto != NULL);
    if (texto == NULL) return;

    IdeOptimizacion r;
    VERIFICAR(ide_optimizacion_analizar(texto, largo, ruta, &r) == 0);
    free(texto);

    /* 1. Los dos niveles compilan y terminan. */
    VERIFICAR(r.estado == IDE_OPT_LISTO);
    VERIFICAR(r.o0.compilado && r.o2.compilado);
    VERIFICAR(r.o0.simulacion.estado == MET_LISTO && r.o2.simulacion.estado == MET_LISTO);

    /* 2. Los resultados esperados, en los dos niveles. */
    VERIFICAR(vale(&r.o0, "resultados", c->resultados));
    VERIFICAR(vale(&r.o2, "resultados", c->resultados));
    if (c->extra != NULL) {
        VERIFICAR(vale_extra(&r.o0, c->extra));
        VERIFICAR(vale_extra(&r.o2, c->extra));
    }

    /* 3. Regla de oro: todas las variables de main iguales. */
    VERIFICAR(r.mismo_resultado == 1);

    /* 4. La optimización de la categoría actuó. */
    switch (c->categoria) {
        case UNROLL:   VERIFICAR(r.unroll >= c->minimo); break;
        case RENOMBRE: VERIFICAR(r.renombradas > 0 && r.war_eliminadas > 0 && r.waw_eliminadas > 0); break;
        case DCE:      VERIFICAR(r.dce >= c->minimo); break;
        case REORDEN:  VERIFICAR(r.reordenadas > 0); break;
        case SPILLING: VERIFICAR(r.spills > 0 && r.guardados_spill > 0 && r.cargas_spill > 0); break;
    }

    printf("%-42s -O0 %6llu ciclos %4zu bundles | -O2 %6llu ciclos %4zu bundles | "
           "unroll=%zu renombre=%zu (WAR %zu, WAW %zu) DCE=%zu reorden=%zu spill=%zu/%zu\n",
           c->archivo,
           (unsigned long long)r.o0.simulacion.sim.ciclos, r.o0.bundles,
           (unsigned long long)r.o2.simulacion.sim.ciclos, r.o2.bundles,
           r.unroll, r.renombradas, r.war_eliminadas, r.waw_eliminadas, r.dce, r.reordenadas,
           r.guardados_spill, r.cargas_spill);
    ide_optimizacion_liberar(&r);
}

int main(void)
{
    for (size_t i = 0; i < sizeof CASOS / sizeof CASOS[0]; ++i) revisar(&CASOS[i]);
    if (fallos > 0) {
        fprintf(stderr, "test_pruebas_obligatorias: %d fallos\n", fallos);
        return 1;
    }
    puts("test_pruebas_obligatorias: OK (15 programas)");
    return 0;
}
