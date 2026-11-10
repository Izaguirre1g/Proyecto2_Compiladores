/* Herramienta de sesiones VLIW con reportes M1, independiente de balc. */
#include "sim_ejecucion.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    /* Los cinco argumentos fijan formato MEM/BALB, ruta, búsqueda, presupuesto
     * y salida TEXTO/JSON. Exigirlos también acota FISICA, que no termina por
     * alcanzar el final del archivo. Una opción desconocida es error de uso. */
    if (argc != 6 || (strcmp(argv[1], "mem") && strcmp(argv[1], "balb")) ||
        (strcmp(argv[3], "finita") && strcmp(argv[3], "fisica")) ||
        (strcmp(argv[5], "texto") && strcmp(argv[5], "json"))) goto uso;
    /* strtoumax admite signos y espacios iniciales; este contrato solo acepta
     * dígitos y una cadena no vacía. La conversión posterior comprueba errno
     * y el rango de uint64_t para no truncar un presupuesto desbordado. */
    if (!*argv[4]) goto uso;
    for (const char *p = argv[4]; *p; ++p) if (*p < '0' || *p > '9') goto uso;
    errno = 0;
    char *fin;
    uintmax_t n = strtoumax(argv[4], &fin, 10);
    if (errno || *fin || n > UINT64_MAX) goto uso;
    /* La API pública posee carga, CPU y M1. La herramienta pide un presupuesto
     * y consulta su resultado; no ejecuta ni recolecta eventos por su cuenta.
     * Los diagnósticos van a stderr para mantener stdout como reporte legible. */
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
    /* M1 publica una cadena propia solo al completar el reporte. La salida
     * comienza en NULL y el error no entrega un texto parcial al usuario. */
    char *reporte = NULL;
    SimMetricasEstado e = sim_ejecucion_exportar(s,
        !strcmp(argv[5], "json") ? SIM_METRICAS_JSON : SIM_METRICAS_TEXTO, &reporte);
    if (e != SIM_METRICAS_OK) {
        fprintf(stderr, "ejecucion: error al exportar métricas %d\n", (int)e);
        goto terminar;
    }
    /* fputs consume la cadena, por lo que ya puede liberarse. Su retorno y
     * fflush cubren tanto el fallo inmediato como el del búfer de salida;
     * un destino que falla no puede informarse como reporte exitoso. */
    int escrito = fputs(reporte, stdout);
    sim_metricas_liberar_reporte(&reporte);
    if (escrito < 0 || fflush(stdout) != 0) {
        fputs("ejecucion: error al escribir reporte\n", stderr);
        goto terminar;
    }
    /* Un reporte escrito distingue 0 (TERMINADO) de 2 (presupuesto agotado).
     * Las rutas de error conservan salida=1; uso devuelve 64 sin crear sesión. */
    salida = estado.pipeline == SIM_PIPE_TERMINADO ? 0 : 2;
/* Cualquier salida posterior a crear la sesión pasa por su destrucción.
 * La reanudación entre presupuestos corresponde al cliente de la biblioteca. */
terminar:
    sim_ejecucion_liberar(&s);
    return salida;
uso:
    fputs("uso: sim_vliw_metricas mem|balb archivo finita|fisica ciclos texto|json\n", stderr);
    return 64;
}
