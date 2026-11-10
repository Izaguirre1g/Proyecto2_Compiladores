/* Decodificación de palabras y bundles VLIW para el simulador.
 * Expone los operandos presentes sin ejecutar operaciones ni acceder a archivos. */
#ifndef SIM_DECODE_H
#define SIM_DECODE_H

#include "sim_cargador.h"
#include <stdbool.h>

/* Un registro ausente no es x0. Los campos pequeños de cripto y corrimiento
 * utilizan 255 para distinguir ausencia de un valor cero perfectamente válido. */
enum { SIM_REG_AUSENTE = 32, SIM_CAMPO_AUSENTE = 255 };

/* OK reconoce una operación válida; NOP reconoce la palabra cero. RESERVADA
 * indica tipo/ID no definido; SLOT, operación conocida en otra unidad; CAMPOS,
 * operandos fuera del contrato ISA; ARGUMENTO, salida NULL o slot fuera de 0..3. */
typedef enum {
    SIM_DECODE_OK = 0, SIM_DECODE_NOP, SIM_DECODE_RESERVADA,
    SIM_DECODE_SLOT, SIM_DECODE_CAMPOS, SIM_DECODE_ARGUMENTO
} SimDecodeEstado;

typedef enum {
    SIM_FAMILIA_NINGUNA = 0, SIM_FAMILIA_ALU, SIM_FAMILIA_LSU,
    SIM_FAMILIA_BRU, SIM_FAMILIA_CRIPTO, SIM_FAMILIA_NOP
} SimFamilia;

typedef enum {
    SIM_FMT_NINGUNO = 0, SIM_FMT_REG, SIM_FMT_IMM, SIM_FMT_LSU,
    SIM_FMT_COND, SIM_FMT_SYE, SIM_FMT_SYER, SIM_FMT_FEISTEL,
    SIM_FMT_ELL, SIM_FMT_VCR, SIM_FMT_CAMCON, SIM_FMT_SETPWD
} SimFormato;

/* Son operaciones ISA, no los códigos internos ALU_* que recibe la ALU RTL.
 * Las pseudoinstrucciones ya llegaron expandidas: ret se reconoce como syer. */
typedef enum {
    SIM_OP_INVALIDA = 0, SIM_OP_NOP,
    SIM_OP_SUMA, SIM_OP_RESTA, SIM_OP_CADER, SIM_OP_CIZQ,
    SIM_OP_AND, SIM_OP_OR, SIM_OP_XOR, SIM_OP_CDER, SIM_OP_MRQ, SIM_OP_MYQ,
    SIM_OP_SUMAI, SIM_OP_RESTAI, SIM_OP_CIZQI, SIM_OP_CDERI, SIM_OP_CADERI,
    SIM_OP_XORI, SIM_OP_ANDI, SIM_OP_ORI,
    SIM_OP_GUARDAP, SIM_OP_GUARDAB, SIM_OP_CARGAI, SIM_OP_CARGABAI,
    SIM_OP_IGUALSI, SIM_OP_IGUALNO, SIM_OP_MENORA, SIM_OP_MAYOROIGUAL,
    SIM_OP_SYE, SIM_OP_SYER,
    SIM_OP_FSL, SIM_OP_FSLI, SIM_OP_ELL, SIM_OP_VCR, SIM_OP_CAMCON, SIM_OP_SETPWD
} SimOperacion;

typedef enum {
    SIM_SALTO_NINGUNO = 0, SIM_SALTO_COND_PC, SIM_SALTO_PC, SIM_SALTO_REGISTRO
} SimSalto;

typedef struct {
    /* Siempre conserva palabra, slot solicitado, tipo [31:25] e ID [24:21]
     * para diagnosticar un rechazo. estado debe revisarse antes de usar operandos. */
    uint32_t palabra;
    unsigned slot;
    uint8_t tipo, id;
    SimDecodeEstado estado;
    SimFamilia familia;
    SimFormato formato;
    SimOperacion operacion;

    /* Índices, no valores del banco. rd2 solo existe para Feistel; rs2 puede
     * representar el segundo registro del par o el dato de un store.
     * Los cuatro valen SIM_REG_AUSENTE cuando la instrucción no los utiliza. */
    uint8_t rd, rd2, rs1, rs2;

    /* ancho_inmediato=0 significa ausencia y deja ambos valores en cero.
     * bruto conserva bits; inmediato aplica el signo usado por RTL (11/16 bits)
     * o representa la rotación sin signo de camcon (5 bits). */
    uint32_t inmediato_bruto;
    int32_t inmediato;
    uint8_t ancho_inmediato;
    bool inmediato_con_signo;
    /* Solo cizqi/cderi/caderi: cantidad efectiva de cinco bits, incluso cuando
     * el inmediato de 11 bits extendido es negativo. No realiza el corrimiento. */
    uint8_t corrimiento;
    SimSalto salto;

    /* LK/RK solo se usan en Feistel; ell utiliza LK y off_boveda. Los tres
     * usan SIM_CAMPO_AUSENTE si no corresponden al formato. */
    uint8_t lk, rk, off_boveda;
    /* Dirección absoluta sin signo para vcr/camcon. tiene_direccion distingue
     * la dirección cero de un campo ausente, que también almacena cero. */
    uint16_t direccion;
    bool tiene_direccion;
    /* Requisitos estáticos del opcode; no consultan AUTH/INIT ni generan
     * excepciones. El estado del procesador se comprobará en bloques posteriores. */
    bool requiere_auth, requiere_init;
} SimInstruccion;

/* Decodifica según el slot 0 ALU, 1 LSU, 2 BRU o 3 CRIPTO. Palabra cero es NOP.
 * En errores se preserva solo la información de diagnóstico y los campos
 * operandos quedan ausentes; no se aplica el fallback físico de un reservado.
 * Una salida NULL devuelve ARGUMENTO. No necesita estado del procesador. */
SimDecodeEstado sim_decode_palabra(uint32_t palabra, unsigned slot,
                                   SimInstruccion *salida);

/* Acepta directamente imagen->imem[b]. Completa los cuatro resultados incluso
 * si uno es inválido. Devuelve el primer error por orden de slot; sin errores,
 * NOP si los cuatro son NOP, u OK si existe alguna instrucción válida.
 * Entradas/salidas no deben solaparse; un puntero NULL devuelve ARGUMENTO. */
SimDecodeEstado sim_decode_bundle(const uint32_t palabras[SIM_CARGADOR_SLOTS],
                                  SimInstruccion salida[SIM_CARGADOR_SLOTS]);

/* Nombre ISA para diagnósticos; la cadena es estática y no debe liberarse. */
const char *sim_operacion_nombre(SimOperacion operacion);

#endif
