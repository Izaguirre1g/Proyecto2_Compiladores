/* Ejecuta la semántica ALU del RTL con aritmética definida por C.
 * Los operandos y el resultado conservan exactamente 32 bits. */
#include "sim_alu.h"

bool sim_alu_ejecutar(SimOperacion operacion, uint32_t a, uint32_t b,
                      uint32_t *resultado)
{
    if (!resultado) return false;
    uint32_t valor;
    /* RTL limita el corrimiento a operand_b[4:0]. Así 32 produce cero
     * posiciones y 0xffffffff, incluido un inmediato -1, produce 31. */
    unsigned cantidad = b & 31u;
    switch (operacion) {
    case SIM_OP_SUMA: case SIM_OP_SUMAI:
        /* La suma/resta unsigned se reduce módulo 2^32, incluso al cruzar
         * los extremos. No se provoca overflow de enteros signed en C. */
        valor = a + b;
        break;
    case SIM_OP_RESTA: case SIM_OP_RESTAI:
        valor = a - b;
        break;
    case SIM_OP_AND: case SIM_OP_ANDI: valor = a & b; break;
    case SIM_OP_OR: case SIM_OP_ORI: valor = a | b; break;
    case SIM_OP_XOR: case SIM_OP_XORI: valor = a ^ b; break;
    case SIM_OP_CIZQ: case SIM_OP_CIZQI: valor = a << cantidad; break;
    case SIM_OP_CDER: case SIM_OP_CDERI: valor = a >> cantidad; break;
    case SIM_OP_CADER: case SIM_OP_CADERI:
        /* Primero se desplaza sin signo. Si el bit 31 era uno se rellenan
         * los bits altos con una máscara. cantidad=0 evita desplazar por 32;
         * no se depende del resultado de desplazar un signed negativo en C. */
        valor = a >> cantidad;
        if (cantidad && (a & UINT32_C(0x80000000)))
            valor |= UINT32_MAX << (32u - cantidad);
        break;
    case SIM_OP_MRQ:
        /* Invertir el bit de signo reordena los valores para comparar unsigned:
         * INT_MIN pasa a 0 y cero a 0x80000000. Conserva el orden signed de
         * complemento a dos sin una conversión uint32_t→int32_t no portable. */
        valor = (a ^ UINT32_C(0x80000000)) < (b ^ UINT32_C(0x80000000));
        break;
    case SIM_OP_MYQ:
        valor = (a ^ UINT32_C(0x80000000)) > (b ^ UINT32_C(0x80000000));
        break;
    default:
        return false;
    }
    *resultado = valor;
    return true;
}
