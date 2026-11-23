/* Evalúa branches y jumps con la misma aritmética y selección de bru.sv.
 * El controlador decide cuándo publicar el destino y registrar el enlace. */
#include "sim_bru.h"

/* Recibe una de las seis operaciones de S2, PC original de EX, valores leídos
 * en ID y offset signed ya decodificado. Condicionales/syer admiten 11 bits;
 * sye, 16. No exige alineación estática: esa validación pertenece a S2.
 * Devuelve false ante salida NULL, operación ajena u offset fuera de rango;
 * conserva la salida ante error. Las sumas y máscaras son de 32 bits. */
bool sim_bru_ejecutar(SimOperacion operacion, uint32_t pc, uint32_t a,
                      uint32_t b, int32_t offset, SimResultadoBru *resultado)
{
    if (!resultado) return false;
    bool directo = operacion == SIM_OP_SYE;
    if (offset < (directo ? -32768 : -1024) ||
        offset > (directo ? 32767 : 1023)) return false;
    SimResultadoBru r = {.destino = pc + 16u, .valor_enlace = pc + 16u};
    /* Invertir el bit de signo permite comparar complemento a dos mediante
     * unsigned, sin convertir valores fuera de rango a int32_t del host. */
    uint32_t a_ordenado = a ^ UINT32_C(0x80000000);
    uint32_t b_ordenado = b ^ UINT32_C(0x80000000);
    switch (operacion) {
    case SIM_OP_IGUALSI: r.tomado = a == b; break;
    case SIM_OP_IGUALNO: r.tomado = a != b; break;
    case SIM_OP_MENORA: r.tomado = a_ordenado < b_ordenado; break;
    case SIM_OP_MAYOROIGUAL: r.tomado = a_ordenado >= b_ordenado; break;
    case SIM_OP_SYE: case SIM_OP_SYER:
        r.tomado = true;
        r.enlace = true;
        break;
    default: return false;
    }
    if (r.tomado) {
        /* Solo syer usa el registro base y fuerza alineación dinámica. Los
         * demás destinos se suman al PC del bundle, nunca al PC global de IF.
         * Convertir offset a unsigned reproduce wraparound sin overflow signed. */
        r.destino = operacion == SIM_OP_SYER
            ? (a + (uint32_t)offset) & UINT32_C(0xfffffff0)
            : pc + (uint32_t)offset;
    }
    *resultado = r;
    return true;
}
