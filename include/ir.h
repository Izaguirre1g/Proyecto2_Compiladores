#ifndef BAL_IR_H
#define BAL_IR_H
#include <stddef.h>
#include <stdio.h>
#include "ast.h"

/* IR de tres direcciones, independiente de la ISA. Cada operando es una
 * cadena propiedad de IrPrograma; los temporales usan $tN y las etiquetas $LN.
 * Nunca se debe optimizar una instruccion con efectos sin comprobarlos. */
typedef enum {
    IR_FUNC, IR_END_FUNC, IR_PARAM_DECL, IR_COPY, IR_BIN, IR_UNARY,
    IR_LABEL, IR_JUMP, IR_JFALSE, IR_JTRUE, IR_ARG, IR_CALL, IR_RETURN,
    IR_ARRAY_DECL, IR_ARRAY_DIM, IR_INDEX_REF, IR_LOAD, IR_STORE,
    IR_LIST_DECL, IR_LIST_ADD, IR_LIST_REMOVE, IR_SIZE,
    IR_SPILL_LOAD, IR_SPILL_STORE
} IrOp;
/* Cada instruccion identifica sus operandos por nombre. Las referencias
 * de indice son valores de direccion logica y no registros de CPU. */
typedef struct {
    IrOp op;
    char *dst, *a, *b, *operador;
    size_t linea;
} IrInstr;
typedef struct {
    size_t inicio, fin; /* intervalo [inicio, fin) en instrucciones */
    size_t *sucesores;
    size_t cantidad_sucesores;
} IrBloque;
typedef struct {
    IrInstr *codigo;
    size_t cantidad, capacidad;
    unsigned temporales, etiquetas;
    IrBloque *bloques;
    size_t cantidad_bloques;
} IrPrograma;

/* Devuelve 0 en exito, 1 si hay un nodo aun no implementado, 2 si
 * hay error de memoria/argumentos. No modifica el AST ni la tabla semantica.
 * Un error deja el programa vacio; el mensaje incluye ubicacion del nodo. */
int ir_desde_ast(const Ast *ast, IrPrograma *ir, FILE *diagnosticos);
int ir_bloques_basicos(IrPrograma *ir);
int ir_imprimir(const IrPrograma *ir, FILE *salida);
void ir_liberar(IrPrograma *ir);
#endif
