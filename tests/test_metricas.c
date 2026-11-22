/*
 * Pruebas de las métricas del código del editor (src/Metricas/metricas.c),
 * sin abrir ventanas.
 *
 *   1. factorial.bal termina y sus métricas son coherentes; el ciclo en que
 *      main termina es el mismo que da el simulador sobre el .bin de balc.
 *   2. dce_ensamblador.bal: el código SIN el código muerto (botón Código
 *      muerto) gasta menos ciclos e instrucciones que el original.
 *   3. Un ciclo infinito agota el presupuesto (MET_SIN_TERMINAR).
 *   4. Un código con errores no se mide.
 *
 * Se ejecuta con: make test-metricas
 */
#include "metricas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ide_dce.h"
#include "ide_ensamblador.h"

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

/* Mide un archivo de ejemplo. */
static int medir_archivo(const char *ruta, uint64_t presupuesto, MetMedicion *m)
{
    size_t largo = 0;
    char *texto = leer_archivo(ruta, &largo);
    if (texto == NULL) return -1;
    int estado = met_medir(texto, largo, ruta, presupuesto, m);
    free(texto);
    return estado;
}

/* Las métricas de una medición terminada cuadran entre sí. */
static void verificar_coherencia(const MetMedicion *m)
{
    const SimMetricasResumen *s = &m->sim;
    VERIFICAR(s->valido);
    VERIFICAR(s->ciclos > 0 && s->completados > 0);
    /* Cada bundle que llegó a WB es útil (al menos una operación) o todo NOP. */
    VERIFICAR(s->bundles_utiles + s->bundles_nop == s->completados);
    /* Las instrucciones útiles son la suma de los 4 slots. */
    uint64_t por_slot = 0;
    for (int i = 0; i < 4; ++i) por_slot += s->instrucciones_por_slot[i];
    VERIFICAR(por_slot == s->instrucciones_utiles);
    /* Con un bundle por ciclo y 4 slots, el IPC no pasa de 4. */
    VERIFICAR(s->ipc > 0.0 && s->ipc <= 4.0);
    /* main terminó: el PC está en el salto final. */
    VERIFICAR(s->pc == m->direccion_fin);
    VERIFICAR(s->traps == 0);
    /* Tiempo de compilación (mediana): las fases caben en el total. */
    const IdeAsmTiempos *t = &m->compilacion;
    VERIFICAR(t->analisis > 0.0 && t->generacion > 0.0 && t->codificacion > 0.0);
    VERIFICAR(t->analisis + t->generacion + t->codificacion <= t->total);
}

/*
 * Comprobación independiente: corre el .bin de balc ciclo por ciclo, sin
 * tramos (lento, pero muy simple), hasta el primer ciclo con el PC en el
 * salto final. Devuelve ese ciclo, o 0 si no llegó.
 */
static uint64_t ciclo_fin_paso_a_paso(const char *bin, uint32_t fin)
{
    SimEjecucion *e = NULL;
    if (sim_ejecucion_crear(bin, SIM_EJECUCION_BALB, SIM_BUSQUEDA_FINITA, &e, stderr) !=
        SIM_EJECUCION_OK) return 0;
    uint64_t ciclo = 0;
    SimMetricasResumen r;
    for (int i = 0; i < 200000; ++i) {
        sim_ejecucion_avanzar(e);
        if (sim_ejecucion_resultados(e, &r) != SIM_METRICAS_OK) break;
        if (r.pc == fin) {
            ciclo = r.ciclos;
            break;
        }
    }
    sim_ejecucion_liberar(&e);
    return ciclo;
}

int main(int argc, char **argv)
{
    MetMedicion m;

    /* 1. factorial.bal. argv[1] (opcional): el .bin que generó balc para
     *    el mismo archivo (lo hace make test-metricas). */
    VERIFICAR(medir_archivo("examples/factorial.bal", 0, &m) == 0);
    VERIFICAR(m.estado == MET_LISTO);
    if (m.estado == MET_LISTO) {
        verificar_coherencia(&m);
        VERIFICAR(m.bundles > 0 && m.direccion_fin > 0);
        if (argc > 1) VERIFICAR(m.sim.ciclos == ciclo_fin_paso_a_paso(argv[1], m.direccion_fin));
        printf("factorial: %llu ciclos, IPC %.3f, %llu instrucciones utiles, "
               "%llu bundles NOP, fin en 0x%x, compilacion %.3f ms\n",
               (unsigned long long)m.sim.ciclos, m.sim.ipc,
               (unsigned long long)m.sim.instrucciones_utiles,
               (unsigned long long)m.sim.bundles_nop, (unsigned)m.direccion_fin,
               m.compilacion.total);
    }
    met_liberar(&m);

    /* El .bin que arma el IDE (ide_ensamblador.c, paso 5) es idéntico, byte
     * por byte, al que escribió balc. */
    if (argc > 1) {
        size_t largo_editor = 0, largo_balc = 0;
        char *fuente = leer_archivo("examples/factorial.bal", &largo_editor);
        char *de_balc = leer_archivo(argv[1], &largo_balc);
        IdeEnsamblador a;
        VERIFICAR(fuente != NULL && de_balc != NULL);
        if (fuente != NULL && de_balc != NULL &&
            ide_ensamblador_generar(fuente, largo_editor, "examples/factorial.bal", &a) == 0) {
            VERIFICAR(a.largo_balb == largo_balc && memcmp(a.balb, de_balc, largo_balc) == 0);
            ide_ensamblador_liberar(&a);
        }
        free(fuente);
        free(de_balc);
    }

    /* Medir dos veces lo mismo da lo mismo (no depende de corridas anteriores). */
    MetMedicion otra;
    VERIFICAR(medir_archivo("examples/factorial.bal", 0, &m) == 0);
    VERIFICAR(medir_archivo("examples/factorial.bal", 0, &otra) == 0);
    VERIFICAR(m.sim.ciclos == otra.sim.ciclos && m.sim.dmem_fnv1a64 == otra.sim.dmem_fnv1a64);
    met_liberar(&m);
    met_liberar(&otra);

    /* 2. Antes y después del DCE. */
    const char *ruta_dce = "examples/dce/dce_ensamblador.bal";
    size_t largo = 0;
    char *original = leer_archivo(ruta_dce, &largo);
    VERIFICAR(original != NULL);
    IdeDce dce;
    if (original != NULL && ide_dce_analizar(original, largo, ruta_dce, &dce) == 0) {
        VERIFICAR(dce.codigo_nuevo != NULL);
        MetMedicion antes, despues;
        VERIFICAR(met_medir(original, largo, ruta_dce, 0, &antes) == 0);
        if (dce.codigo_nuevo != NULL) {
            VERIFICAR(met_medir(dce.codigo_nuevo, strlen(dce.codigo_nuevo), ruta_dce, 0, &despues) == 0);
            VERIFICAR(antes.estado == MET_LISTO && despues.estado == MET_LISTO);
            if (antes.estado == MET_LISTO && despues.estado == MET_LISTO) {
                verificar_coherencia(&antes);
                verificar_coherencia(&despues);
                VERIFICAR(despues.bundles < antes.bundles);
                VERIFICAR(despues.sim.ciclos < antes.sim.ciclos);
                VERIFICAR(despues.sim.instrucciones_utiles < antes.sim.instrucciones_utiles);
                SimMetricasComparacion c;
                VERIFICAR(met_comparar(&antes, &despues, &c) == 0);
                VERIFICAR(c.ciclos.negativa && c.porcentaje_definido && c.cambio_porcentual_ciclos < 0.0);
                printf("DCE: %llu -> %llu ciclos (%.1f %%), %zu -> %zu bundles\n",
                       (unsigned long long)antes.sim.ciclos, (unsigned long long)despues.sim.ciclos,
                       c.cambio_porcentual_ciclos, antes.bundles, despues.bundles);
            }
            met_liberar(&despues);
        }
        met_liberar(&antes);
        ide_dce_liberar(&dce);
    }
    free(original);

    /* 3. Un ciclo infinito: con 20000 ciclos de presupuesto no termina. */
    const char *infinito =
        "create_funk #declare_infinite_void# main() {\n"
        "    i * declare_int : 0;\n"
        "    whale (i < 1) {\n"
        "        i : i gauss 0;\n"
        "    } stop;\n"
        "}\n";
    VERIFICAR(met_medir(infinito, strlen(infinito), "examples/editor.bal", 20000, &m) == 0);
    VERIFICAR(m.estado == MET_SIN_TERMINAR);
    VERIFICAR(m.motivo != NULL && strstr(m.motivo, "20000") != NULL);
    VERIFICAR(m.sim.ciclos >= 20000 && m.sim.pc != m.direccion_fin);
    met_liberar(&m);

    /* 4. Con errores no se mide (aquí x no está declarada). */
    const char *con_errores = "create_funk #declare_infinite_void# main() {\n    x : 5;\n}\n";
    VERIFICAR(met_medir(con_errores, strlen(con_errores), "examples/editor.bal", 0, &m) == 0);
    VERIFICAR(m.estado == MET_CON_ERRORES);
    met_liberar(&m);

    /* Funciones de apoyo para las gráficas. */
    VERIFICAR(met_porcentaje(1, 4) == 25.0 && met_porcentaje(3, 0) == 0.0);
    VERIFICAR(strcmp(met_nombre_slot(2), "BRU") == 0 && strcmp(met_nombre_slot(7), "?") == 0);

    if (fallos > 0) {
        fprintf(stderr, "test_metricas: %d fallos\n", fallos);
        return 1;
    }
    puts("test_metricas: OK");
    return 0;
}
