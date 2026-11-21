/* Configura DCE productivo sin modificar sus pasadas ni el backend BAL.
 * El resultado posee un directorio temporal privado y el fuente a recompilar.
 * Con `bring`, los módulos importados se copian al mismo directorio privado;
 * el DCE reescribe solo el principal. Las llamadas conservan el ABI tradicional. */
#ifndef VLIW_OPTIMIZACION_CLI_H
#define VLIW_OPTIMIZACION_CLI_H
#include <stddef.h>
#include <stdio.h>
typedef struct {
    char *directorio, *ruta;
    size_t eliminadas_ir, borradas_bal, cambiadas_bal, conservadas_bal;
    int cambio_bal;
} VliwOptFuente;
/* Salida vacía, entrada inmutable y diagnóstico opcional. Retorna 0 en éxito,
 * 1 por entrada/capacidad no admitida, 2 por E/S o recursos. No publica parcial.
 * El llamador debe liberar con vliw_opt_fuente_liberar, incluso tras rechazos. */
int vliw_opt_fuente_preparar(const char *entrada, VliwOptFuente *salida, FILE *diag);
/* Libera solo archivos propios del directorio privado; no toca el fuente BAL. */
void vliw_opt_fuente_liberar(VliwOptFuente *fuente);
#endif
