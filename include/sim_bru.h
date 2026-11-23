/* Semántica combinacional BRU: decide redirección y enlace desde operandos
 * capturados. No altera PC, memoria, banco ni registros del pipeline. */
#ifndef SIM_BRU_H
#define SIM_BRU_H
#include "sim_decode.h"

typedef struct {
    /* tomado activa el flush de IF/ID; enlace solicita un resultado para WB,
     * incluso con destino x0 (el banco descarta esa escritura posteriormente).
     * Sin salto tomado, destino conserva PC+16 como la salida por defecto RTL. */
    bool tomado, enlace;
    uint32_t destino, valor_enlace;
} SimResultadoBru;

/* Recibe una de las seis operaciones de S2, PC original de EX, valores leídos
 * en ID y offset signed ya decodificado. Condicionales/syer admiten 11 bits;
 * sye, 16. No exige alineación estática: esa validación pertenece a S2.
 * Devuelve false ante salida NULL, operación ajena u offset fuera de rango;
 * conserva la salida ante error. Las sumas y máscaras son de 32 bits. */
bool sim_bru_ejecutar(SimOperacion operacion, uint32_t pc, uint32_t a,
                      uint32_t b, int32_t offset, SimResultadoBru *resultado);
#endif
