/* Pruebas C y sonda de campos para el decodificador del simulador.
 * La sonda se usa solo desde los tests Python; no agrega opciones a balc. */
#include "sim_decode.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static size_t comprobaciones;
#define COMPROBAR(c) do { \
    ++comprobaciones; \
    if (!(c)) { fprintf(stderr, "FAIL línea %d: %s\n", __LINE__, #c); exit(1); } \
} while (0)

/* Todos los operandos de un rechazo deben seguir ausentes. Esto detecta
 * campos de una decodificación previa que pudieran sobrevivir al error. */
static void comprobar_ausentes(const SimInstruccion *i)
{
    COMPROBAR(i->operacion == SIM_OP_INVALIDA && i->familia == SIM_FAMILIA_NINGUNA);
    COMPROBAR(i->formato == SIM_FMT_NINGUNO);
    COMPROBAR(i->rd == 32 && i->rd2 == 32 && i->rs1 == 32 && i->rs2 == 32);
    COMPROBAR(i->ancho_inmediato == 0 && i->inmediato == 0 && i->inmediato_bruto == 0);
    COMPROBAR(!i->inmediato_con_signo && i->corrimiento == 255);
    COMPROBAR(i->lk == 255 && i->rk == 255 && i->off_boveda == 255);
    COMPROBAR(i->salto == SIM_SALTO_NINGUNO && !i->tiene_direccion && i->direccion == 0);
    COMPROBAR(!i->requiere_auth && !i->requiere_init);
}

static void casos_unitarios(void)
{
    /* Las máscaras indican IDs ISA reconocidos, no ALU_* internos. Se barren
     * los 128 tipos, 16 IDs y cuatro slots: una operación conocida fuera de
     * su unidad debe ser SLOT y un ID desconocido siempre RESERVADA. */
    const struct { unsigned tipo, ids, slot; } familias[] = {
        {0x6a, 0xff60, 0}, {0x40, 0x00ff, 0}, {0x49, 0x0303, 1},
        {0x41, 0x0116, 2}, {0x4b, 0x0003, 2}, {0x02, 0x003f, 3}
    };
    SimInstruccion i;
    unsigned reconocidas = 0;
    for (unsigned tipo = 0; tipo < 128; ++tipo) {
        for (unsigned id = 0; id < 16; ++id) {
            unsigned slot_real = 4;
            for (size_t f = 0; f < sizeof familias / sizeof *familias; ++f)
                if (familias[f].tipo == tipo && (familias[f].ids & (1u << id)))
                    slot_real = familias[f].slot;
            if (slot_real < 4) ++reconocidas;
            /* Payload legal en cada formato: registros pares en cripto y
             * offsets directos alineados. No es palabra cero, ni para tipo 0. */
            uint32_t palabra = (tipo << 25) | (id << 21) | 0x42200u;
            for (unsigned slot = 0; slot < 4; ++slot) {
                SimDecodeEstado esperado = slot_real == 4 ? SIM_DECODE_RESERVADA
                    : slot_real != slot ? SIM_DECODE_SLOT : SIM_DECODE_OK;
                COMPROBAR(sim_decode_palabra(palabra, slot, &i) == esperado);
                COMPROBAR(i.estado == esperado && i.palabra == palabra && i.slot == slot);
                COMPROBAR(i.tipo == tipo && i.id == id);
                if (esperado != SIM_DECODE_OK) comprobar_ausentes(&i);
            }
        }
    }
    COMPROBAR(reconocidas == 34);
    /* NOP se reconoce por la palabra completa, en cualquiera de los slots,
     * sin transformarlo en suma ni inventar una lectura de x0. */
    for (unsigned slot = 0; slot < 4; ++slot) {
        COMPROBAR(sim_decode_palabra(0, slot, &i) == SIM_DECODE_NOP);
        COMPROBAR(i.operacion == SIM_OP_NOP && i.familia == SIM_FAMILIA_NOP);
        COMPROBAR(i.rs1 == SIM_REG_AUSENTE && i.rd == SIM_REG_AUSENTE);
    }
    COMPROBAR(sim_decode_palabra(0, 0, NULL) == SIM_DECODE_ARGUMENTO);
    COMPROBAR(sim_decode_palabra(0, 4, &i) == SIM_DECODE_ARGUMENTO);
    comprobar_ausentes(&i);
    COMPROBAR(sim_decode_palabra(UINT32_MAX, UINT_MAX, &i) == SIM_DECODE_ARGUMENTO);
    comprobar_ausentes(&i);

    /* Se reusa la misma salida: tras una instrucción válida, un error de
     * operandos debe borrar sus campos, conservando solo palabra/tipo/ID/slot. */
    COMPROBAR(sim_decode_palabra(0x80042807u, 0, &i) == SIM_DECODE_OK);
    COMPROBAR(sim_decode_palabra(0x04001000u, 3, &i) == SIM_DECODE_CAMPOS);
    comprobar_ausentes(&i);
    uint32_t palabras[4] = {0};
    SimInstruccion bundle[4];
    COMPROBAR(sim_decode_bundle(palabras, bundle) == SIM_DECODE_NOP);
    COMPROBAR(sim_decode_bundle(NULL, bundle) == SIM_DECODE_ARGUMENTO);
    COMPROBAR(sim_decode_bundle(palabras, NULL) == SIM_DECODE_ARGUMENTO);
    /* El primer slot es reservado, el segundo válido y el último está en
     * unidad incorrecta. Debe devolverse el primer error y completar el resto. */
    palabras[0] = 1;
    palabras[1] = 0x93020800u;
    palabras[3] = 0x80042807u;
    COMPROBAR(sim_decode_bundle(palabras, bundle) == SIM_DECODE_RESERVADA);
    COMPROBAR(bundle[1].estado == SIM_DECODE_OK && bundle[1].operacion == SIM_OP_CARGAI);
    COMPROBAR(bundle[2].estado == SIM_DECODE_NOP && bundle[3].estado == SIM_DECODE_SLOT);
    COMPROBAR(strcmp(sim_operacion_nombre(SIM_OP_INVALIDA), "invalida") == 0);
    printf("OK: %zu comprobaciones C de S2\n", comprobaciones);
}

/* Expone todos los campos públicos como JSON. La comparación Python puede
 * comprobar ausencias además de los operandos presentes, sin replicar la
 * distribución binaria de la estructura mediante ctypes. */
static void mostrar(const SimInstruccion *i, size_t b, SimDecodeEstado estado_bundle)
{
    printf("{\"bundle\":%zu,\"bundle_estado\":%u,\"word\":%u,\"slot\":%u,"
           "\"estado\":%u,\"tipo\":%u,\"id\":%u,\"operacion\":\"%s\","
           "\"familia\":%u,\"formato\":%u,\"rd\":%u,\"rd2\":%u,\"rs1\":%u,\"rs2\":%u,"
           "\"bruto\":%u,\"inmediato\":%ld,\"ancho\":%u,\"signo\":%u,\"corrimiento\":%u,"
           "\"salto\":%u,\"lk\":%u,\"rk\":%u,\"off_boveda\":%u,\"direccion\":%u,"
           "\"tiene_direccion\":%u,\"auth\":%u,\"init\":%u}\n",
           b, (unsigned)estado_bundle, (unsigned)i->palabra, i->slot,
           (unsigned)i->estado, i->tipo, i->id, sim_operacion_nombre(i->operacion),
           (unsigned)i->familia, (unsigned)i->formato, i->rd, i->rd2, i->rs1, i->rs2,
           (unsigned)i->inmediato_bruto, (long)i->inmediato, i->ancho_inmediato,
           i->inmediato_con_signo, i->corrimiento, (unsigned)i->salto,
           i->lk, i->rk, i->off_boveda, i->direccion, i->tiene_direccion,
           i->requiere_auth, i->requiere_init);
}

/* Los argumentos pertenecen al protocolo interno de pruebas. No se modifica
 * la CLI del compilador. imagen conecta S1→S2 para ambos formatos de archivo. */
int main(int argc, char **argv)
{
    if (argc == 1) { casos_unitarios(); return 0; }
    if (argc == 4 && strcmp(argv[1], "palabra") == 0) {
        SimInstruccion i;
        sim_decode_palabra((uint32_t)strtoul(argv[3], NULL, 16),
                           (unsigned)strtoul(argv[2], NULL, 10), &i);
        mostrar(&i, 0, i.estado);
        return 0;
    }
    if (argc == 6 && strcmp(argv[1], "bundle") == 0) {
        uint32_t palabras[4]; SimInstruccion bundle[4];
        for (unsigned s = 0; s < 4; ++s) palabras[s] = (uint32_t)strtoul(argv[s + 2], NULL, 16);
        SimDecodeEstado estado = sim_decode_bundle(palabras, bundle);
        for (unsigned s = 0; s < 4; ++s) mostrar(&bundle[s], 0, estado);
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "imagen") == 0) {
        SimImagen *imagen = NULL;
        SimCargaEstado estado = strcmp(argv[2], "mem") == 0
            ? sim_cargar_mem(argv[3], &imagen, stderr)
            : sim_cargar_balb(argv[3], &imagen, stderr);
        if (estado != SIM_CARGA_OK) return 1;
        for (size_t b = 0; b < imagen->cantidad_bundles; ++b) {
            SimInstruccion bundle[4];
            SimDecodeEstado dec = sim_decode_bundle(imagen->imem[b], bundle);
            for (unsigned s = 0; s < 4; ++s) mostrar(&bundle[s], b, dec);
        }
        sim_liberar_imagen(&imagen);
        return 0;
    }
    return 2;
}
