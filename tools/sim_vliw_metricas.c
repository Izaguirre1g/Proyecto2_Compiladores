/* Herramienta de sesiones VLIW con reportes M1, independiente de balc. */
#include "sim_ejecucion.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    /* Se rechazan signos, espacios y desbordamientos. Todos los modos exigen
     * presupuesto explícito, de modo que FISICA nunca se ejecuta sin límite. */
    if (argc != 6 || (strcmp(argv[1], "mem") && strcmp(argv[1], "balb")) ||
        (strcmp(argv[3], "finita") && strcmp(argv[3], "fisica")) ||
        (strcmp(argv[5], "texto") && strcmp(argv[5], "json"))) goto uso;
    if (!*argv[4]) goto uso;
    for (const char *p = argv[4]; *p; ++p) if (*p < '0' || *p > '9') goto uso;
    errno = 0;
    char *fin;
    uintmax_t n = strtoumax(argv[4], &fin, 10);
    if (errno || *fin || n > UINT64_MAX) goto uso;
    SimEjecucion *s = NULL;
    SimEjecucionCodigo c = sim_ejecucion_crear(argv[2],
        !strcmp(argv[1], "mem") ? SIM_EJECUCION_MEM : SIM_EJECUCION_BALB,
        !strcmp(argv[3], "finita") ? SIM_BUSQUEDA_FINITA : SIM_BUSQUEDA_FISICA, &s, stderr);
    if (c != SIM_EJECUCION_OK) return 1;
    c = sim_ejecucion_ejecutar(s, (uint64_t)n);
    SimEjecucionEstado estado;
    (void)sim_ejecucion_estado(s, &estado);
    int salida = 1;
    if (c != SIM_EJECUCION_OK) {
        fprintf(stderr, "ejecucion: código %d, pipeline %d, métricas %d, pendiente %d\n",
            (int)c, (int)estado.pipeline, (int)estado.metricas, (int)estado.pendiente);
        goto terminar;
    }
    char *reporte = NULL;
    SimMetricasEstado e = sim_ejecucion_exportar(s,
        !strcmp(argv[5], "json") ? SIM_METRICAS_JSON : SIM_METRICAS_TEXTO, &reporte);
    if (e != SIM_METRICAS_OK) {
        fprintf(stderr, "ejecucion: error al exportar métricas %d\n", (int)e);
        goto terminar;
    }
    int escrito = fputs(reporte, stdout);
    sim_metricas_liberar_reporte(&reporte);
    if (escrito < 0 || fflush(stdout) != 0) {
        fputs("ejecucion: error al escribir reporte\n", stderr);
        goto terminar;
    }
    /* 0: TERMINADO; 2: LIMITE con reporte válido; 1: error; 64: uso inválido. */
    salida = estado.pipeline == SIM_PIPE_TERMINADO ? 0 : 2;
terminar:
    sim_ejecucion_liberar(&s);
    return salida;
uso:
    fputs("uso: sim_vliw_metricas mem|balb archivo finita|fisica ciclos texto|json\n", stderr);
    return 64;
}
