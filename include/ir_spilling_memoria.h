#ifndef BAL_IR_SPILLING_MEMORIA_H
#define BAL_IR_SPILLING_MEMORIA_H

#include "ir_spilling_backend.h"
#include "symbol_table.h"

/* Contrato MEM -> SPILLING (marco de UNA funcion):
 *
 * La tabla debe haber pasado satisfactoriamente por mem_asignar_marco o
 * mem_asignar_proyecto. Se leen los offsets ya asignados, sin modificar
 * simbolos ni el mapa global. La funcion se identifica por su simbolo global
 * y su ambito directo; se incluyen TODOS los subambitos de esa funcion.
 *
 * Se verifican, en orden de declaracion, la reserva [sp+0,sp+4) para RA,
 * los home slots de parametros de cuatro bytes, los locales y su padding.
 * Las ranuras de $spillN empiezan despues del marco original alineado a 4.
 * La pasada ir_aplicar_spilling actual numera $spillN en todo IrPrograma:
 * para una IR con varias funciones, reservar metricas.ranuras en CADA
 * funcion hasta que exista una asignacion particionada/rebasada por funcion.
 * Nunca se reutilizan offsets entre locales/spills ni entre funciones.
 *
 * Si alguna ubicacion no coincide con la ABI asignada, el tipo es
 * indeterminado, hay desbordamiento o el offset LSU de un spill supera 1020,
 * devuelve 2 sin alterar 'salida'. Devuelve 0 en exito.
 *
 * Esta API solo PREPARA memoria; no traduce variables BAL a LSU, no hace
 * lowering de CALL/RET, ni asigna registros fisicos. El backend debe usar
 * salida.marco.bytes_marco en prologo, epilogo y mem_abi_argumento para
 * los argumentos 4+ recibidos desde la pila.
 */
int ir_spilling_backend_desde_tabla(const SimTabla *tabla,
                                    const char *nombre_funcion,
                                    size_t ranuras,
                                    IrSpillingBackend *salida);

/* Contrasta el layout optativo mem_asignar_proyecto_referencias: cada
 * parámetro de arreglo 1D ocupa una dirección de 32 bits. No acredita
 * longitud ni ausencia de alias; el lowering debe preservar esas garantías. */
int ir_spilling_backend_desde_tabla_referencias(const SimTabla *tabla,
    const char *nombre_funcion, size_t ranuras, IrSpillingBackend *salida);

#endif
