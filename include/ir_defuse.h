#ifndef BAL_IR_DEFUSE_H
#define BAL_IR_DEFUSE_H

#include <stdbool.h>
#include <stddef.h>
#include "ir.h"

/* Consulta simbolica de UNA instruccion de tres direcciones, antes o despues
 * del register spilling. No estima latencias ni reemplaza el DAG fisico VLIW.
 *
 * 'usos' y 'definiciones' contienen exclusivamente nombres de valores:
 * variables BAL, temporales $tN y registros logicos $rN. Nunca contienen
 * inmediatos, etiquetas, nombres de funciones ni ranuras $spillN.
 * Las referencias apuntan a la propia IrInstr: NO deben liberarse ni usarse
 * despues de modificar/liberar la instruccion.
 *
 * Los efectos sobre memoria se expresan aparte. Un 'objeto_memoria' no nulo
 * identifica una ranura logica de spilling; NULL significa que la region
 * o su aliasing NO esta determinado. No asumir disjuncion de regiones por
 * este campo. Para CALL se asumen efectos desconocidos de lectura/escritura.
 */
typedef struct {
    const char *usos[3];
    size_t cantidad_usos;
    const char *definiciones[1];
    size_t cantidad_definiciones;
    bool lee_memoria;
    bool escribe_memoria;
    bool barrera;
    bool control;
    const char *objeto_memoria;
} IrDefUse;

/* 0: exito, 1: argumento/opcode/estructura de instruccion invalidos.
 * Es una consulta sin reservas de memoria ni efectos laterales. En error
 * deja resultado en cero y nunca publica un analisis parcial. */
int ir_consultar_def_usos(const IrInstr *instruccion, IrDefUse *resultado);

/* Reconoce exclusivamente nombres de valores de IR; no nombres de funciones,
 * etiquetas ni literales. No valida identidad de symbol table/ambitos. */
bool ir_es_nombre_valor(const char *texto);

#endif
