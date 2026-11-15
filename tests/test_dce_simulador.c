/*
 * Prueba con el simulador VLIW (src/Simulador_VLIW/) de que el código sin
 * el código muerto hace LO MISMO que el original (regla de oro, diapositiva
 * 12 de "Optimización de Código"): se ejecutan los dos binarios y se
 * comparan las variables de main al terminar.
 *
 * El programa es examples/dce/dce_ensamblador.bal. Su main guarda
 *     resultados[0] = r = cuadrado(4)    = 16
 *     resultados[1] = s = suma_hasta(5)  = 15
 * El arreglo está en el marco de main, en el tope de la pila (que empieza en
 * 0x10000): resultados[0] en sp+4 y resultados[1] en sp+8. El marco mide
 * distinto en cada versión (el código nuevo ya no tiene la variable suma),
 * así que su tamaño se pasa al ejecutar (lo dice balc -m).
 *
 * Uso: test_dce_simulador original.bin marco nuevo.bin marco
 *      (lo corre make test-ide-dce)
 */
#include "sim_ejecucion.h"

#include <stdio.h>
#include <string.h>

#include <stdlib.h>

#define TOPE_PILA 0x10000u

/* Ejecuta el binario y deja resultados[0] y [1] de main.
 * Devuelve 0 bien, 1 si no se pudo. */
static int ejecutar(const char *ruta, unsigned marco, int *r, int *s)
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
    memcpy(r, memoria + sp + 4, sizeof *r);
    memcpy(s, memoria + sp + 8, sizeof *s);
    sim_ejecucion_liberar(&e);
    return estado;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "uso: %s original.bin marco nuevo.bin marco\n", argv[0]);
        return 2;
    }
    int r1 = 0, s1 = 0, r2 = 0, s2 = 0;
    unsigned marco1 = (unsigned)strtoul(argv[2], NULL, 10);
    unsigned marco2 = (unsigned)strtoul(argv[4], NULL, 10);
    if (ejecutar(argv[1], marco1, &r1, &s1) != 0 || ejecutar(argv[3], marco2, &r2, &s2) != 0) return 1;
    printf("original:          r = %d, s = %d\n", r1, s1);
    printf("sin codigo muerto: r = %d, s = %d\n", r2, s2);
    if (r1 != 16 || s1 != 15 || r2 != r1 || s2 != s1) {
        fprintf(stderr, "test_dce_simulador: los resultados no coinciden\n");
        return 1;
    }
    puts("test_dce_simulador: OK (mismo resultado con y sin el codigo muerto)");
    return 0;
}
