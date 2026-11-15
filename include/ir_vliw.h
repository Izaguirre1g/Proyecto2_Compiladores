#ifndef BAL_IR_VLIW_H
#define BAL_IR_VLIW_H

#include "ir_spilling_backend.h"
#include "ir_spilling.h"
#include "vliw_cadena_fisica.h"

/* Productor conservador de fragmentos rectilineos, no backend BAL completo.
 * Los valores permitidos son temporales $tN (antes de spilling), $rN
 * (despues), e inmediatos representables con sumai. Sin variables BAL,
 * llamadas, arreglos ni retorno: se rechazan explicitamente.
 * JUMP/JTRUE/JFALSE/LABEL se convierten en BRU con simbolos reubicables.
 * Se acepta opcionalmente un solo par IR_FUNC/IR_END_FUNC como delimitadores.
 * El marco de la funcion se reserva y libera; NO se emite una instruccion
 * de finalizacion/retorno, de modo que el fragmento no es ejecutable solo. */
typedef struct {
    size_t registros;          /* entre 3 y 8: se mapean a x16..x23 */
    size_t bytes_ocupados;     /* marco previo COMPLETO, incluyendo ra */
} IrVliwConfiguracion;

typedef struct {
    IrPrograma despues_spilling;
    IrSpillingMetricas metricas;
    IrSpillingBackend backend;
    VliwCadena *cadena;      /* contiene plan, ASM y CbPrograma reales */
} IrVliwResultado;

typedef enum {
    IR_VLIW_OK = 0, IR_VLIW_NO_SOPORTADO = 1,
    IR_VLIW_ARGUMENTO = 2, IR_VLIW_CADENA_FALLO = 3
} IrVliwEstado;

/* salida inicialmente {0}; publicacion atomica en exito. Los errores dejan
 * salida intacta y no alteran la IR original. diagnosticos puede ser NULL. */
IrVliwEstado ir_vliw_desde_ir(const IrPrograma *original,
    const IrVliwConfiguracion *configuracion,
    IrVliwResultado *salida, FILE *diagnosticos);
void ir_vliw_liberar(IrVliwResultado *resultado);

#endif
