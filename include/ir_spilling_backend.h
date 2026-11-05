#ifndef BAL_IR_SPILLING_BACKEND_H
#define BAL_IR_SPILLING_BACKEND_H

#include <stddef.h>
#include <stdio.h>
#include "ir_spilling_marco.h"

/* Reune el marco existente y las ranuras necesarias para una funcion.
 * No altera la tabla de simbolos ni reserva registros fisicos.
 * El llamador debe proporcionar el tamano real del marco de esa funcion. */
typedef struct {
    IrSpillingMarco marco;
    size_t numero_parametros;
} IrSpillingBackend;

/* Comprueba el tamano del marco y la cantidad de parametros.
 * Devuelve 0 si la configuracion es valida; 2 si es incompatible. */
int ir_spilling_backend_preparar(size_t bytes_existentes, size_t ranuras,
                                 size_t numero_parametros,
                                 IrSpillingBackend *backend);

/* Emite el prologo y el epilogo originales usando el marco ampliado.
 * Estas funciones no emiten instrucciones intermedias ni bundles. */
int ir_spilling_backend_prologo(FILE *salida, const IrSpillingBackend *backend);
int ir_spilling_backend_epilogo(FILE *salida, const IrSpillingBackend *backend);

/* Emite una carga o almacenamiento de spilling con un registro fisico
 * elegido por el asignador. El registro no se asigna automaticamente. */
int ir_spilling_backend_acceso(FILE *salida, const IrSpillingBackend *backend,
                               const IrInstr *instruccion,
                               unsigned registro_fisico);

#endif
