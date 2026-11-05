#ifndef BAL_IR_SPILLING_H
#define BAL_IR_SPILLING_H

#include "ir.h"

/* La pasada de spilling traduce temporales virtuales $tN a registros $rN.
 * Las ranuras $spillN representan lugares logicos del marco de pila.
 * El backend debe asignarles offsets seguros antes de emitir codigo maquina.
 */
typedef struct {
    size_t registros;
    size_t ranuras;
    size_t cargas;
    size_t almacenes;
    size_t desalojos;
} IrSpillingMetricas;

/* Genera un programa nuevo sin alterar la IR original.
 * Devuelve 0 en exito y 2 ante argumentos invalidos o falta de memoria.
 * Exige al menos tres registros de trabajo para operaciones binarias.
 * La salida debe estar inicializada a cero y se libera con ir_liberar().
 */
int ir_aplicar_spilling(const IrPrograma *origen, IrPrograma *salida,
                       size_t registros, IrSpillingMetricas *metricas);

#endif
