/* CLI de comparación B1; ejecución y métricas siguen perteneciendo a M2/M1. */
#include "sim_comparador.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
/*
 * Valida formatos, modo y presupuesto de ambas imágenes, invoca B1 y exporta su conclusión;
 * conserva códigos distintos para equivalencia, diferencia, inconclusión y error y libera los
 * recursos.
 */
int main(int argc,char **argv)
{
    /* Cada archivo declara su formato; un presupuesto común limita por
     * separado ambos programas. FISICA no se considera terminación final. */
    if (argc!=8 || (strcmp(argv[1],"mem") && strcmp(argv[1],"balb")) ||
        (strcmp(argv[3],"mem") && strcmp(argv[3],"balb")) ||
        (strcmp(argv[5],"finita") && strcmp(argv[5],"fisica")) ||
        (strcmp(argv[7],"json") && strcmp(argv[7],"texto"))) goto uso;
    if (!*argv[6]) goto uso;
    for (const char *s=argv[6];*s;++s) if (*s<'0' || *s>'9') goto uso;
    errno=0;char *fin;uintmax_t n=strtoumax(argv[6],&fin,10);
    if (errno || *fin || n>UINT64_MAX) goto uso;
    SimComparadorOpciones o={
        .referencia={argv[2],!strcmp(argv[1],"mem")?SIM_EJECUCION_MEM:SIM_EJECUCION_BALB},
        .candidato={argv[4],!strcmp(argv[3],"mem")?SIM_EJECUCION_MEM:SIM_EJECUCION_BALB},
        .modo=!strcmp(argv[5],"finita")?SIM_BUSQUEDA_FINITA:SIM_BUSQUEDA_FISICA,
        .presupuesto=(uint64_t)n,.reintentos=2};
    // B1 adquiere ambas sesiones M2, comprueba iniciales y ejecuta la comparación estricta. La
    // CLI solo traduce sus opciones y entrega su reporte; no implementa otro comparador de
    // registros ni memoria.
    SimComparador *c=NULL;SimComparadorCodigo e=sim_comparador_ejecutar(&o,&c,stderr);
    if (e!=SIM_COMPARADOR_OK) { fprintf(stderr,"comparador: error de construcción %u\n",(unsigned)e);return 1; }
    SimComparadorResumen r;(void)sim_comparador_resumen(c,&r);
    char *reporte=NULL;e=sim_comparador_exportar(c,!strcmp(argv[7],"json")?SIM_METRICAS_JSON:SIM_METRICAS_TEXTO,&reporte);
    int salida=1;
    if (e!=SIM_COMPARADOR_OK) fprintf(stderr,"comparador: error de reporte %u\n",(unsigned)e);
    else {
        /* Un resultado DIFERENTE/INCONCLUSO también tiene un reporte válido.
         * Un fallo de entrega nunca se presenta como comparación exitosa. */
        int escrito=fputs(reporte,stdout);sim_comparador_liberar_reporte(&reporte);
        if (escrito<0 || fflush(stdout)!=0) fputs("comparador: error al escribir reporte\n",stderr);
        else salida=r.resultado==SIM_COMPARADOR_EQUIVALENTE?0:r.resultado==SIM_COMPARADOR_DIFERENTE?2:r.resultado==SIM_COMPARADOR_INCONCLUSO?3:1;
    }
    sim_comparador_liberar(&c);return salida;
uso:
    fputs("uso: comparar_vliw mem|balb referencia mem|balb candidato finita|fisica ciclos texto|json\n",stderr);return 64;
}
