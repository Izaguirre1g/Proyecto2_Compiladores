#ifndef BAL_IR_SPILLING_MARCO_H
#define BAL_IR_SPILLING_MARCO_H

#include <stddef.h>
#include <stdio.h>
#include "ir.h"

/* El backend actual utiliza sp descendente y reserva sp+0 para ra.
 * Los espacios de parametros y variables deben estar incluidos en
 * bytes_ocupados antes de solicitar nuevas ranuras de spilling. */
typedef struct {
    size_t bytes_ocupados;
    size_t inicio_spilling;
    size_t ranuras;
    size_t bytes_marco;
} IrSpillingMarco;

/* Calcula una ampliacion del marco sin tocar el marco original.
 * Devuelve 0 si es valido y 2 si hay limites incompatibles.
 * Las ranuras son palabras de 4 bytes y deben caber en el LSU directo.
 * Es responsabilidad del integrador incluir todas las reservas previas. */
int ir_spilling_calcular_marco(size_t bytes_ocupados, size_t ranuras,
                              IrSpillingMarco *marco);

/* Convierte $spillN en un desplazamiento real respecto de sp.
 * Rechaza nombres invalidos y ranuras fuera del marco reservado. */
int ir_spilling_offset(const IrSpillingMarco *marco,
                       const char *ranura, size_t *offset);

/* Emite una instruccion de carga o escritura del ISA BAL.
 * El registro fisico xN lo selecciona el backend; esta funcion no
 * resuelve automaticamente la asignacion de $rN a registros fisicos.
 * No escribe nada cuando los argumentos no son validos. */
int ir_spilling_emitir_acceso(FILE *salida, const IrSpillingMarco *marco,
                              const IrInstr *instruccion, unsigned registro_fisico);

#endif
