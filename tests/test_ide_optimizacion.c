/*
 * Pruebas del botón "-O0/-O1/-O2" del IDE (src/IDE/ide_optimizacion.c), sin
 * abrir ventanas. Compila programas de ejemplo con -O0, -O1 y -O2, los
 * ejecuta en el simulador y revisa:
 *   - que los tres niveles terminen y dejen el MISMO resultado de main
 *     (regla de oro: optimizar no cambia lo que hace el programa);
 *   - el valor exacto de las variables de main (por ejemplo, factorial de 5);
 *   - que -O2 aplique sus pasadas (el DCE quita el código muerto);
 *   - programas con módulos (bring);
 *   - los casos en que no se puede: código con errores y lo que -O2 todavía
 *     no soporta.
 *
 * Se ejecuta con: make test-ide-optimizacion
 */
#include "ide_optimizacion.h"

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

/* Compara -O0 y -O2 para un archivo. */
static int analizar(const char *ruta, IdeOptimizacion *r)
{
    size_t largo = 0;
    char *texto = leer_archivo(ruta, &largo);
    if (texto == NULL) return -1;
    int estado = ide_optimizacion_analizar(texto, largo, ruta, r);
    free(texto);
    return estado;
}

/* El valor final de una variable de main en un nivel (NULL si no está). */
static const char *valor(const IdeOptNivel *n, const char *nombre)
{
    for (size_t i = 0; i < n->cantidad_variables; ++i)
        if (strcmp(n->variables[i].nombre, nombre) == 0) return n->valores ? n->valores[i] : NULL;
    return NULL;
}

/* Lo que tiene que cumplir cualquier programa que los dos niveles soportan. */
static void verificar_los_dos(const char *ruta, const IdeOptimizacion *r)
{
    VERIFICAR(r->estado == IDE_OPT_LISTO);
    VERIFICAR(r->o0.compilado && r->o1.compilado && r->o2.compilado);
    VERIFICAR(r->o0.simulacion.estado == MET_LISTO && r->o1.simulacion.estado == MET_LISTO &&
              r->o2.simulacion.estado == MET_LISTO);
    VERIFICAR(r->mismo_o1 == 1 && r->mismo_o2 == 1 && r->mismo_resultado == 1);
    VERIFICAR(r->o1_codigo != NULL && r->o1.tiempos.total > 0.0);
    VERIFICAR(r->o0.bundles > 0 && r->o2.bundles > 0);
    VERIFICAR(r->o0.simulacion.sim.ciclos > 0 && r->o2.simulacion.sim.ciclos > 0);
    VERIFICAR(r->o0.tiempos.total > 0.0 && r->o2.tiempos.total > 0.0);
    VERIFICAR(r->o0.marco_main > 0 && r->o2.marco_main > 0);
    VERIFICAR(r->ir_antes != NULL && r->ir_despues != NULL && r->ir_spilling != NULL);
    printf("%s: -O0 %llu ciclos, %zu bundles | -O1 %llu ciclos, %zu bundles | -O2 %llu ciclos, %zu bundles | "
           "DCE=%zu unroll=%zu renombre=%zu reorden=%zu spills=%zu\n", ruta,
           (unsigned long long)r->o0.simulacion.sim.ciclos, r->o0.bundles,
           (unsigned long long)r->o1.simulacion.sim.ciclos, r->o1.bundles,
           (unsigned long long)r->o2.simulacion.sim.ciclos, r->o2.bundles,
           r->dce, r->unroll, r->renombradas, r->reordenadas, r->spills);
}

int main(void)
{
    IdeOptimizacion r;

    /* factorial de 5: resultado = 120 en los dos niveles. */
    VERIFICAR(analizar("examples/factorial.bal", &r) == 0);
    verificar_los_dos("factorial", &r);
    VERIFICAR(valor(&r.o0, "resultado") && strcmp(valor(&r.o0, "resultado"), "120") == 0);
    VERIFICAR(valor(&r.o2, "resultado") && strcmp(valor(&r.o2, "resultado"), "120") == 0);
    /* El ciclo de factorial se desenrolla en -O2. */
    VERIFICAR(r.unroll >= 1);
    ide_optimizacion_liberar(&r);

    /* El ejemplo del DCE: un arreglo de main y código muerto que -O2 quita. */
    VERIFICAR(analizar("examples/dce/dce_ensamblador.bal", &r) == 0);
    verificar_los_dos("dce_ensamblador", &r);
    VERIFICAR(valor(&r.o2, "resultados") && strcmp(valor(&r.o2, "resultados"), "[16, 15]") == 0);
    VERIFICAR(r.dce > 0);
    /* -O1: el DCE quita "doble" de cuadrado() y la variable muerta "suma"
     * de main: -O1 ya no la tiene (no es una diferencia) y el código es más chico. */
    VERIFICAR(r.o1_cambio && r.o1_eliminadas_ir > 0 && r.o1_borradas > 0);
    VERIFICAR(valor(&r.o1, "resultados") && strcmp(valor(&r.o1, "resultados"), "[16, 15]") == 0);
    VERIFICAR(r.o1.bundles < r.o0.bundles);
    VERIFICAR(r.o1.simulacion.sim.ciclos < r.o0.simulacion.sim.ciclos);
    ide_optimizacion_liberar(&r);

    /* Números negativos: multiplicacion.bal deja r = -42. */
    VERIFICAR(analizar("examples/multiplicacion.bal", &r) == 0);
    verificar_los_dos("multiplicacion", &r);
    VERIFICAR(valor(&r.o2, "r") && strcmp(valor(&r.o2, "r"), "-42") == 0);
    ide_optimizacion_liberar(&r);

    /* Llamadas entre funciones. */
    VERIFICAR(analizar("examples/llamadas.bal", &r) == 0);
    verificar_los_dos("llamadas", &r);
    ide_optimizacion_liberar(&r);

    /* Código con errores: no se compila con ningún nivel. */
    const char *con_errores = "create_funk #declare_infinite_void# main() {\n    x : 5;\n}\n";
    VERIFICAR(ide_optimizacion_analizar(con_errores, strlen(con_errores),
                                        "examples/editor.bal", &r) == 0);
    VERIFICAR(r.estado == IDE_OPT_CON_ERRORES && r.mismo_resultado == -1);
    ide_optimizacion_liberar(&r);

    /* Con módulos (bring): los dos niveles los enlazan y dan lo mismo. */
    VERIFICAR(analizar("tests/modulos/enlace_principal.bal", &r) == 0);
    verificar_los_dos("enlace_principal", &r);
    VERIFICAR(valor(&r.o2, "resultados") &&
              strcmp(valor(&r.o2, "resultados"), "[109, 107, 100, 10]") == 0);
    ide_optimizacion_liberar(&r);

    /* Un programa que -O2 todavía no soporta (cycle con paso que no es una
     * constante positiva): -O0 sí lo compila y lo ejecuta; -O2 dice por qué
     * no, igual que balc -O2, y no hay nada que comparar. */
    VERIFICAR(analizar("examples/cycle.bal", &r) == 0);
    VERIFICAR(r.estado == IDE_OPT_LISTO && r.o0.compilado && !r.o1.compilado && !r.o2.compilado);
    VERIFICAR(r.o0.simulacion.estado == MET_LISTO);
    /* -O1 también necesita la IR (el DCE trabaja sobre ella), igual que balc -O1. */
    VERIFICAR(r.o1.motivo != NULL && strstr(r.o1.motivo, "cycle") != NULL);
    VERIFICAR(r.o2.motivo != NULL && strstr(r.o2.motivo, "cycle") != NULL);
    VERIFICAR(r.mismo_resultado == -1 && r.mismo_o1 == -1 && r.mismo_o2 == -1);
    ide_optimizacion_liberar(&r);

    if (fallos > 0) {
        fprintf(stderr, "test_ide_optimizacion: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_optimizacion: OK");
    return 0;
}
