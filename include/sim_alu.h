/* ALU de 32 bits del simulador: calcula resultados sin conocer el pipeline
 * ni el banco de registros. Recibe operaciones que ya reconoció S2. */
#ifndef SIM_ALU_H
#define SIM_ALU_H

#include "sim_decode.h"

/* Admite las 18 operaciones ALU; b ya contiene el registro o el inmediato
 * extendido elegido por ID. Devuelve false para otra operación o salida NULL,
 * conservando el resultado recibido. No ejecuta NOP ni escribe registros. */
bool sim_alu_ejecutar(SimOperacion operacion, uint32_t a, uint32_t b,
                      uint32_t *resultado);

#endif
