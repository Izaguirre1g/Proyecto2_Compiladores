/* Extrae metadatos parciales de una palabra ISA física mediante S2.
 * Recibe palabra y slot; devuelve operandos resumidos y efectos mínimos, sin
 * texto, CFG, DAG, ABI ni afirmación de completitud para el calendarizador. */
#ifndef VLIW_METADATOS_H
#define VLIW_METADATOS_H
#include "sim_decode.h"
#include "vliw_scheduler.h"

/* Obligaciones del futuro productor, no efectos ISA deducidos del opcode.
 * PRODUCTOR incluye símbolos, ABI/clobbers, efectos externos y reubicaciones,
 * también en ALU: una constante podría materializar una dirección de código. */
typedef enum {
    VLIW_META_PEND_PRODUCTOR = 1u << 0,
    VLIW_META_PEND_CFG_DAG = 1u << 1,
    VLIW_META_PEND_CONTROL_ABI = 1u << 2,
    VLIW_META_PEND_DESTINOS = 1u << 3
} VliwMetadatosPendientes;

/* Datos por valor, distintos de VliwInstruccion para impedir que una extracción
 * local parezca una entrada S7 completa. Los efectos son mínimos, no totales;
 * pendientes nunca queda en cero y no reemplaza una acreditación externa. */
typedef struct {
    SimOperacion operacion;
    SimFamilia familia;
    VliwSlot slot;
    uint64_t lee, escribe;       /* Bits0..31, sin dependencias ficticias de x0. */
    unsigned efectos_minimos;   /* Bits VliwEfecto, conforme a I1-A1/S8-A. */
    SimSalto salto;             /* Forma física; no clasifica llamada/retorno. */
    bool requiere_auth, requiere_init; /* Requisitos, no valores dinámicos. */
    unsigned pendientes;       /* Bits VliwMetadatosPendientes. */
} VliwMetadatosParciales;

typedef struct {
    SimDecodeEstado estado;
    uint32_t palabra;
    unsigned slot;
    const char *mensaje; /* Literal estático prestado, sin liberación. */
} VliwMetadatosDiagnostico;

/* Recibe palabra por valor, slot0..3 y salida accesible del llamador.
 * Solo SIM_DECODE_OK publica una estructura completa para este nivel parcial.
 * NOP se informa como SIM_DECODE_NOP sin modificar salida; los demás rechazos
 * conservan igualmente todos sus bytes. Se propagan RESERVADA/SLOT/CAMPOS de
 * S2; NULL de salida o slot fuera de rango devuelve ARGUMENTO. Un operando S2
 * internamente fuera de0..31 y distinto de AUSENTE se rechaza como CAMPOS.
 * diagnostico es opcional y debe ocupar almacenamiento separado de salida.
 * No reserva memoria, no retiene punteros y no requiere liberar el resultado.
 * Una salida anterior puede reutilizarse: solo se reemplaza en éxito. El
 * cliente debe comprobar el retorno, pues en error conserva su valor anterior.
 * No crea VliwEntrada ni activa metadatos_completos; no calcula destinos,
 * dependencias, efectos externos ni el estado AUTH/INIT. */
SimDecodeEstado vliw_metadatos_extraer(uint32_t palabra, unsigned slot,
    VliwMetadatosParciales *salida, VliwMetadatosDiagnostico *diagnostico);
#endif
