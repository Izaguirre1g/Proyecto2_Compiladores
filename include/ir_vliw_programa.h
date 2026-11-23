/* Backend productivo complementario al productor de fragmentos existente.
 * Recibe AST/tabla semántica de una unidad sin módulos, aplica pasadas IR y
 * publica programa completo solo tras lowering, enlace, cadena y CB reales.
 * AST/tabla son préstamos inmutables salvo la asignación previa de memoria.
 * La salida posee IR intermedias, cadena y reporte; liberar incluso en error. */
#ifndef BAL_IR_VLIW_PROGRAMA_H
#define BAL_IR_VLIW_PROGRAMA_H
#include "ir_vliw.h"
#include "symbol_table.h"
typedef struct {
    IrPrograma original, optimizada, spilling;
    IrSpillingMetricas metricas;
    VliwCadena *cadena;
    char *reporte_json;
    size_t eliminadas, desenrollados, renombradas, movidas;
    size_t war_eliminadas, waw_eliminadas;
    size_t pc_fin, generaciones;
} IrVliwPrograma;
/* 0 éxito, 1 construcción rechazada (incluye capacidad/recursos/E/S),
 * 2 argumentos inválidos o reserva inicial fallida.
 * Exige salida {0}, no publica resultados parciales ni degrada a AST original.
 * Registros: 3..8, mismo contrato físico del productor ya integrado. */
int ir_vliw_programa(const Ast *ast, const SimTabla *tabla, size_t registros,
                     IrVliwPrograma *salida, FILE *diagnosticos);
void ir_vliw_programa_liberar(IrVliwPrograma *programa);
#endif
