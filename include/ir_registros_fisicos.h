#ifndef BAL_IR_REGISTROS_FISICOS_H
#define BAL_IR_REGISTROS_FISICOS_H

#include <stddef.h>
#include "ir.h"

/* Contrato de registros de trabajo del productor IR -> VLIW.
 * Los temporales virtuales $tN se asignan por ir_aplicar_spilling a $rN.
 * Esta API convierte esos registros logicos a los xN reales de la ISA:
 *   $r0 ... $r7  ->  x16 ... x23.
 * x0 (cero), x1 (retorno), x2 (pila), x28 y x29 (auxiliares del lowering)
 * no se ofrecen al asignador. No resuelve registros vivos entre llamadas.
 */
enum {
    IR_FISICO_PRIMER_REGISTRO = 16,
    IR_FISICO_MIN_REGISTROS = 3,
    IR_FISICO_MAX_REGISTROS = 8,
    IR_FISICO_AUXILIAR_A = 28,
    IR_FISICO_AUXILIAR_B = 29
};

/* 1 si el numero de registros de trabajo es valido, 0 de lo contrario. */
int ir_fisico_configuracion_valida(size_t registros);

/* 0 exito; 2 nombre mal formado, limite invalido o indice fuera de rango.
 * En error NO modifica *fisico. No acepta temporales $tN, alias ABI ni xN. */
int ir_fisico_mapear(const char *registro_logico, size_t registros,
                     unsigned *fisico);

/* Verifica que la IR (ya pasada por spilling) no contenga temporales $tN
 * pendientes ni referencias $rN fuera del banco disponible. Valida tambien
 * la estructura de cada instruccion con ir_consultar_def_usos().
 * Acepta variables BAL como valores simbolicos: este chequeo NO certifica
 * su lowering, offset, ABI, llamadas o ejecutabilidad del programa.
 * 0 exito; 2 entrada invalida. Si se indica error_en, devuelve el indice de
 * la primera instruccion invalida; SIZE_MAX si ninguna instruccion fallo. */
int ir_fisico_validar_programa(const IrPrograma *programa, size_t registros,
                               size_t *error_en);

#endif
