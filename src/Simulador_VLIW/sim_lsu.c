/* Semántica LSU de 32 bits y DMEM little-endian de 64 KiB.
 * Preparar y aplicar son operaciones separadas para respetar el flanco. */
#include "sim_lsu.h"

/* Lee la palabra seleccionada por addr[15:2], incluso para direcciones altas
 * o no alineadas. Devuelve false ante punteros nulos, sin alterar salida. */
bool sim_memoria_leer_palabra(const SimMemoria *memoria, uint32_t direccion,
                              uint32_t *resultado)
{
    if (!memoria || !resultado) return false;
    /* memory.sv selecciona addr[15:2]: los bits altos producen aliasing y
     * los bajos no provocan un acceso entre dos palabras físicas. */
    uint32_t p = direccion & UINT32_C(0xfffc);
    *resultado = (uint32_t)memoria->bytes[p] |
        ((uint32_t)memoria->bytes[p+1] << 8) |
        ((uint32_t)memoria->bytes[p+2] << 16) |
        ((uint32_t)memoria->bytes[p+3] << 24);
    return true;
}

/* Prepara carga/store con los operandos capturados en ID y offset signed de
 * S2 (-1024..1023). No modifica memoria. En error conserva la salida recibida.
 * Un load entrega el dato que EX debe capturar para WB; un store entrega BE
 * y dato desplazado que el pipeline aplicará solo al aceptar el flanco. */
bool sim_lsu_preparar(const SimMemoria *memoria, SimOperacion operacion,
                      uint32_t base, uint32_t dato, int32_t offset,
                      SimAccesoLsu *acceso)
{
    if (!memoria || !acceso || offset < -1024 || offset > 1023) return false;
    bool carga = operacion == SIM_OP_CARGAI || operacion == SIM_OP_CARGABAI;
    bool store = operacion == SIM_OP_GUARDAP || operacion == SIM_OP_GUARDAB;
    if (!carga && !store) return false;
    bool byte = operacion == SIM_OP_GUARDAB || operacion == SIM_OP_CARGABAI;
    /* La conversión del offset conserva su complemento a dos; sumar unsigned
     * hace wraparound de 32 bits sin overflow signed del anfitrión. */
    uint32_t direccion = base + (uint32_t)offset;
    unsigned desplazamiento = (direccion & 3u) * 8u;
    SimAccesoLsu t = {.valido = true, .escritura = store, .carga = carga,
        .direccion = direccion, .palabra_fisica = direccion & UINT32_C(0xfffc),
        .byte_enable = byte ? (uint8_t)(1u << (direccion & 3u)) : 15,
        .dato_escritura = byte ? dato << desplazamiento : dato};
    sim_memoria_leer_palabra(memoria, direccion, &t.lectura);
    t.resultado = t.lectura;
    if (operacion == SIM_OP_CARGABAI) {
        uint32_t octeto = (t.lectura >> desplazamiento) & 255u;
        /* Extensión de signo explícita: 80/FF producen FFFFFF80/FFFFFFFF.
         * No se convierte un uint8_t fuera de rango a un entero signed. */
        t.resultado = octeto & 128u ? octeto | UINT32_C(0xffffff00) : octeto;
    }
    *acceso = t;
    return true;
}

/* Publica únicamente los bytes habilitados de una transacción válida.
 * Valida todos sus límites antes de modificar un byte; false no tiene efectos.
 * Para loads es una operación válida sin escritura. No actualiza registros. */
bool sim_lsu_aplicar(SimMemoria *memoria, const SimAccesoLsu *acceso)
{
    if (!memoria || !acceso || !acceso->valido || acceso->byte_enable > 15 ||
        acceso->palabra_fisica > SIM_CARGADOR_DMEM_BYTES - 4 ||
        (acceso->palabra_fisica & 3u)) return false;
    if (acceso->escritura) {
        /* BE selecciona posiciones de la palabra, no bytes consecutivos desde
         * la dirección no alineada. Solo se reemplazan los carriles activos;
         * para guardab los bits bajos del fuente ya están en ese carril. */
        for (unsigned byte = 0; byte < 4; ++byte)
            if (acceso->byte_enable & (1u << byte))
                memoria->bytes[acceso->palabra_fisica + byte] =
                    (uint8_t)(acceso->dato_escritura >> (8u * byte));
    }
    return true;
}
