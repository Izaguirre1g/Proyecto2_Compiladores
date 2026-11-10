/* Adaptador de prueba: copia DMEM pública de M2 para contrastar oráculos previos. */
#include "sim_ejecucion.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    /* El presupuesto acota incluso el bucle final del arranque de balc. Este
     * adaptador no interpreta instrucciones ni decide cuándo terminó main. */
    if (argc != 5 || (strcmp(argv[1], "balb") && strcmp(argv[1], "mem")) || !*argv[3]) return 64;
    for (const char *p = argv[3]; *p; ++p) if (*p < '0' || *p > '9') return 64;
    errno = 0;
    char *fin;
    unsigned long long n = strtoull(argv[3], &fin, 10);
    if (errno || *fin || n > UINT64_MAX) return 64;
    SimEjecucion *s = NULL;
    SimEjecucionCodigo c = sim_ejecucion_crear(argv[2], !strcmp(argv[1], "balb") ?
        SIM_EJECUCION_BALB : SIM_EJECUCION_MEM, SIM_BUSQUEDA_FINITA, &s, stderr);
    int salida = 1;
    if (c != SIM_EJECUCION_OK) goto liberar;
    if (sim_ejecucion_ejecutar(s, (uint64_t)n) != SIM_EJECUCION_OK) goto liberar;
    uint8_t memoria[SIM_CARGADOR_DMEM_BYTES];
    if (sim_ejecucion_memoria(s, memoria, sizeof memoria) != SIM_EJECUCION_OK) goto liberar;
    /* El archivo completo permite comparación byte a byte. No se sustituye
     * esa evidencia por FNV ni se consulta el almacenamiento privado de bóveda. */
    FILE *f = fopen(argv[4], "wbx");
    if (!f) goto liberar;
    int ok = fwrite(memoria, 1, sizeof memoria, f) == sizeof memoria;
    if (fclose(f)) ok = 0;
    if (!ok) goto liberar;
    char *reporte = NULL;
    if (sim_ejecucion_exportar(s, SIM_METRICAS_JSON, &reporte) != SIM_METRICAS_OK) goto liberar;
    ok = fputs(reporte, stdout) >= 0 && fflush(stdout) == 0;
    sim_metricas_liberar_reporte(&reporte);
    SimEjecucionEstado estado;
    if (sim_ejecucion_estado(s, &estado) != SIM_EJECUCION_OK || !ok) goto liberar;
    salida = estado.pipeline == SIM_PIPE_TERMINADO ? 0 : 2;
liberar:
    sim_ejecucion_liberar(&s);
    return salida;
}
