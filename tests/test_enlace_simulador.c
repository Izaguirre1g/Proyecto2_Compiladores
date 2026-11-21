/*
 * Prueba de extremo a extremo del enlace entre archivos (`bring`):
 * ejecuta en el simulador VLIW (src/Simulador_VLIW/) un binario compilado
 * desde un programa con módulos y revisa los valores que main dejó en su
 * arreglo `resultados`.
 *
 * El arreglo es la primera variable de main, así que queda en sp+4,
 * sp+8, ... del marco de main, que está en el tope de la pila (0x10000).
 * El tamaño del marco lo dice `balc -m` (marco_maximo_bytes).
 *
 * Uso: test_enlace_simulador programa.bin marco esperado0 [esperado1 ...]
 *      (lo corre make test-enlace)
 */
#include "sim_ejecucion.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOPE_PILA 0x10000u
#define MAX_RESULTADOS 16

/* Ejecuta el binario y copia `n` enteros desde resultados[0]. 0 bien, 1 mal. */
static int ejecutar(const char *ruta, unsigned marco, int *valores, int n)
{
    SimEjecucion *e = NULL;
    if (sim_ejecucion_crear(ruta, SIM_EJECUCION_BALB, SIM_BUSQUEDA_FINITA, &e, stderr) !=
        SIM_EJECUCION_OK) {
        fprintf(stderr, "no se pudo cargar %s\n", ruta);
        return 1;
    }
    /* El programa termina en un salto estable (sye x0,0): se corre con un
     * presupuesto de ciclos de sobra y después se lee la memoria. */
    sim_ejecucion_ejecutar(e, 2000000);
    static uint8_t memoria[65536];
    int estado = sim_ejecucion_memoria(e, memoria, sizeof memoria) == SIM_EJECUCION_OK ? 0 : 1;
    unsigned sp = TOPE_PILA - marco;
    for (int i = 0; i < n; ++i)
        memcpy(&valores[i], memoria + sp + 4 + 4u * (unsigned)i, sizeof valores[i]);
    sim_ejecucion_liberar(&e);
    return estado;
}

int main(int argc, char **argv)
{
    if (argc < 4 || argc - 3 > MAX_RESULTADOS) {
        fprintf(stderr, "uso: %s programa.bin marco esperado0 [esperado1 ...]\n", argv[0]);
        return 2;
    }
    int n = argc - 3, valores[MAX_RESULTADOS] = {0}, fallos = 0;
    unsigned marco = (unsigned)strtoul(argv[2], NULL, 10);
    if (ejecutar(argv[1], marco, valores, n) != 0) return 1;
    for (int i = 0; i < n; ++i) {
        int esperado = atoi(argv[3 + i]);
        int bien = valores[i] == esperado;
        printf("  %s resultados[%d] = %d (esperado %d)\n", bien ? "ok   " : "FALLO", i, valores[i], esperado);
        fallos += !bien;
    }
    if (fallos) {
        fprintf(stderr, "test_enlace_simulador: %d resultados distintos en %s\n", fallos, argv[1]);
        return 1;
    }
    printf("test_enlace_simulador: OK (%s)\n", argv[1]);
    return 0;
}
