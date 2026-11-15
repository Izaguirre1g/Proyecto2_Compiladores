#include "ir_spilling_marco.h"
#include <stdint.h>
#include <string.h>

/* Los desplazamientos LSU directos del backend existente llegan a 1020.
 * El marco puede ser mayor, pero estas operaciones necesitan acceso directo. */
#define SPILL_OFFSET_MAXIMO 1020u
#define SPILL_MARCO_MAXIMO 65536u

/* Redondea al siguiente multiplo de cuatro evitando desbordamientos. */
static int alinear_palabra(size_t bytes, size_t *alineados) {
    if (bytes > SIZE_MAX - 3) return 2;
    *alineados = (bytes + 3) & ~(size_t)3;
    return 0;
}

/* Reserva espacios despues de la memoria ocupada por el backend.
 * El caller tiene que pasar el consumo completo de ra, parametros y locales. */
int ir_spilling_calcular_marco(size_t bytes_ocupados, size_t ranuras,
                              IrSpillingMarco *marco) {
    if (!marco || bytes_ocupados < 4) return 2;
    size_t inicio = 0;
    if (alinear_palabra(bytes_ocupados, &inicio)) return 2;
    if (inicio > SPILL_MARCO_MAXIMO) return 2;
    if (ranuras > (SIZE_MAX - inicio) / 4) return 2;
    size_t total = inicio + ranuras * 4;
    if (total > SPILL_MARCO_MAXIMO) return 2;
    /* Cada ranura debe ser accesible mediante un desplazamiento inmediato.
     * Si se supera, el integrador debe emitir calculo de direccion aparte. */
    if (ranuras && (inicio > SPILL_OFFSET_MAXIMO ||
                    ranuras - 1 > (SPILL_OFFSET_MAXIMO - inicio) / 4)) return 2;
    IrSpillingMarco nuevo = {bytes_ocupados, inicio, ranuras, total};
    *marco = nuevo;
    return 0;
}

/* Lee exclusivamente nombres de la forma $spillN, con N decimal.
 * Verifica el rango antes de multiplicar por cuatro. */
int ir_spilling_offset(const IrSpillingMarco *marco,
                       const char *ranura, size_t *offset) {
    if (!marco || !ranura || !offset || strncmp(ranura, "$spill", 6) != 0 ||
        ranura[6] < '0' || ranura[6] > '9') return 2;
    size_t indice = 0;
    for (const char *p = ranura + 6; *p; ++p) {
        if (*p < '0' || *p > '9') return 2;
        unsigned digito = (unsigned)(*p - '0');
        if (indice > (SIZE_MAX - digito) / 10) return 2;
        indice = indice * 10 + digito;
    }
    if (indice >= marco->ranuras ||
        indice > (SIZE_MAX - marco->inicio_spilling) / 4) return 2;
    size_t resultado = marco->inicio_spilling + indice * 4;
    if (resultado > SPILL_OFFSET_MAXIMO ||
        resultado + 4 > marco->bytes_marco) return 2;
    *offset = resultado;
    return 0;
}

/* Convierte una operacion logica en una instruccion de memoria real.
 * La asignacion y preservacion de registros fisicos sigue siendo externa. */
int ir_spilling_emitir_acceso(FILE *salida, const IrSpillingMarco *marco,
                              const IrInstr *instruccion, unsigned registro_fisico) {
    if (!salida || !marco || !instruccion || registro_fisico > 31 ||
        registro_fisico == 0 || registro_fisico == 2) return 2;
    const char *ranura;
    if (instruccion->op == IR_SPILL_LOAD) {
        ranura = instruccion->a;
    } else if (instruccion->op == IR_SPILL_STORE) {
        ranura = instruccion->dst;
    } else {
        return 2;
    }
    size_t offset = 0;
    if (ir_spilling_offset(marco, ranura, &offset)) return 2;
    /* cargai recibe destino y base; guardap recibe base y dato. */
    if (instruccion->op == IR_SPILL_LOAD) {
        return fprintf(salida, "cargai x%u, x2, %zu\n", registro_fisico,
                       offset) < 0 ? 2 : 0;
    }
    return fprintf(salida, "guardap x2, x%u, %zu\n", registro_fisico,
                   offset) < 0 ? 2 : 0;
}
