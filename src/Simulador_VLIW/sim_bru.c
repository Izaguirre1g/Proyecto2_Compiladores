// MAPA BRU: ID captura la condición -> EX decide salto -> S6 redirige PC y hace flush.
// Los offsets directos se expresan relativos al PC del bundle.

/* Evalúa branches y jumps con la misma aritmética y selección de bru.sv.
 * El controlador decide cuándo publicar el destino y registrar el enlace. */
#include "sim_bru.h"

/* Recibe una de las seis operaciones de S2, PC original de EX, valores leídos
 * en ID y offset signed ya decodificado. Condicionales/syer admiten 11 bits;
 * sye, 16. No exige alineación estática: esa validación pertenece a S2.
 * Devuelve false ante salida NULL, operación ajena u offset fuera de rango;
 * conserva la salida ante error. Las sumas y máscaras son de 32 bits. */
// BRU EN EX: evalua condiciones y calcula destino del salto.
// Si se toma, el pipeline redirige el PC y descarta instrucciones especulativas.
bool sim_bru_ejecutar(SimOperacion operacion, uint32_t pc, uint32_t a,
                      uint32_t b, int32_t offset, SimResultadoBru *resultado)
{
    if (!resultado) return false;
    bool directo = operacion == SIM_OP_SYE;
    if (offset < (directo ? -32768 : -1024) ||
        offset > (directo ? 32767 : 1023)) return false;
    // Si no hay salto tomado, el siguiente PC secuencial es pc+16.
    SimResultadoBru r = {.destino = pc + 16u, .valor_enlace = pc + 16u};
    /* Invertir el bit de signo permite comparar complemento a dos mediante
     * unsigned, sin convertir valores fuera de rango a int32_t del host. */
    // El bit de signo invertido permite orden signed sin conversiones peligrosas del anfitrion.
    uint32_t a_ordenado = a ^ UINT32_C(0x80000000);
    uint32_t b_ordenado = b ^ UINT32_C(0x80000000);
    // Las comparaciones deciden si hay salto; SYE/SYER son saltos incondicionales.
    switch (operacion) {
    case SIM_OP_IGUALSI: r.tomado = a == b; break;
    case SIM_OP_IGUALNO: r.tomado = a != b; break;
    case SIM_OP_MENORA: r.tomado = a_ordenado < b_ordenado; break;
    case SIM_OP_MAYOROIGUAL: r.tomado = a_ordenado >= b_ordenado; break;
    // Las instrucciones de salto incondicional también preparan el valor de enlace.
    case SIM_OP_SYE: case SIM_OP_SYER:
        r.tomado = true;
        r.enlace = true;
        break;
    default: return false;
    }
    // Los saltos directos usan PC+offset; SYER utiliza registro base y fuerza alineacion.
    if (r.tomado) {
        /* Solo syer usa el registro base y fuerza alineación dinámica. Los
         * demás destinos se suman al PC del bundle, nunca al PC global de IF.
         * Convertir offset a unsigned reproduce wraparound sin overflow signed. */
        r.destino = operacion == SIM_OP_SYER
            ? (a + (uint32_t)offset) & UINT32_C(0xfffffff0)
            : pc + (uint32_t)offset;
    }
    // EX entrega la decision; el pipeline hara el flush y publicara el nuevo PC.
    *resultado = r;
    return true;
}
