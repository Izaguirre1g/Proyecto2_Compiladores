/* Renombramiento local de valores, independiente del mapeo físico.
 * El consumidor acredita qué nombres representan escalares locales int32
 * sin alias. No atraviesa memoria, llamadas, control ni aritmética peligrosa.
 * Entrada prestada e inmutable; salida {0}, propia, liberada con ir_liberar.
 * 0 éxito, 1 estructura/capacidad, 2 argumentos/recursos; publicación atómica. */
#ifndef BAL_IR_RENOMBRAMIENTO_H
#define BAL_IR_RENOMBRAMIENTO_H
#include "ir.h"
typedef int (*IrNombreRenombrable)(const char *nombre, void *contexto);
typedef struct { size_t versiones, escrituras_publicadas, war_eliminadas, waw_eliminadas; } IrRenombramiento;
int ir_renombrar_local(const IrPrograma *entrada, IrPrograma *salida,
                     IrNombreRenombrable elegible, void *contexto,
                     IrRenombramiento *resumen);
#endif
