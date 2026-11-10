/* Pruebas ALU con resultados conocidos y sonda para contraste con alu.sv.
 * No se leen registros ni se ejecuta el pipeline desde esta prueba. */
#include "sim_alu.h"
#include <stdlib.h>

/* El índice es solo el protocolo del test: evita suponer que los valores
 * del enum público coinciden con los códigos ALU_* internos del RTL. */
static const SimOperacion operaciones[] = {
    SIM_OP_SUMA, SIM_OP_RESTA, SIM_OP_CADER, SIM_OP_CIZQ, SIM_OP_AND, SIM_OP_OR,
    SIM_OP_XOR, SIM_OP_CDER, SIM_OP_MRQ, SIM_OP_MYQ, SIM_OP_SUMAI, SIM_OP_RESTAI,
    SIM_OP_CIZQI, SIM_OP_CDERI, SIM_OP_CADERI, SIM_OP_XORI, SIM_OP_ANDI, SIM_OP_ORI
};

#define COMPROBAR(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL ALU línea %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static void casos_conocidos(void)
{
    /* Cada instrucción tiene un oráculo literal. Incluye wraparound de suma
     * y resta, negativos, comparación signed e inmediatos ya extendidos. */
    const struct { unsigned op; uint32_t a, b, esperado; } casos[] = {
        {0, UINT32_MAX, 1, 0}, {1, 0, 1, UINT32_MAX},
        {2, 0x80000000u, 31, UINT32_MAX}, {3, 1, 33, 2},
        {4, 0xf0, 0x0f, 0}, {5, 0xf0, 0x0f, 0xff},
        {6, UINT32_MAX, UINT32_MAX, 0}, {7, 0x80000000u, 31, 1},
        {8, 0x80000000u, 0, 1}, {9, 0x7fffffffu, UINT32_MAX, 1},
        {10, 8, 0xfffffc00u, 0xfffffc08u}, {11, 0, UINT32_MAX, 1},
        {12, 1, UINT32_MAX, 0x80000000u}, {13, 0x80000000u, 31, 1},
        {14, 0xfffffff8u, 1, 0xfffffffcu}, {15, UINT32_MAX, UINT32_MAX, 0},
        {16, 0x12345678, UINT32_MAX, 0x12345678}, {17, 0, UINT32_MAX, UINT32_MAX},
        /* Cero posiciones evita formar máscaras que desplacen por 32.
         * Los valores 32/33 comprueban el recorte al campo de cinco bits. */
        {2, 0x80000000u, 0, 0x80000000u}, {2, 0x80000000u, 32, 0x80000000u},
        {2, 0xfffffff8u, 33, 0xfffffffcu}, {3, 7, 0, 7}, {7, 7, 32, 7},
        {0, 0x7fffffffu, 1, 0x80000000u}, {1, 0x80000000u, 1, 0x7fffffffu},
        {8, UINT32_MAX, 0, 1}, {9, UINT32_MAX, 0, 0}, {8, 0, 0, 0}, {9, 0, 0, 0}
    };
    for (size_t n = 0; n < sizeof casos / sizeof *casos; ++n) {
        uint32_t resultado = 123;
        COMPROBAR(sim_alu_ejecutar(operaciones[casos[n].op], casos[n].a, casos[n].b, &resultado));
        COMPROBAR(resultado == casos[n].esperado);
    }
    uint32_t resultado = 123;
    COMPROBAR(!sim_alu_ejecutar(SIM_OP_NOP, 0, 0, &resultado));
    COMPROBAR(!sim_alu_ejecutar(SIM_OP_CARGAI, 0, 0, &resultado));
    COMPROBAR(!sim_alu_ejecutar(SIM_OP_INVALIDA, 0, 0, &resultado));
    COMPROBAR(resultado == 123);
    COMPROBAR(!sim_alu_ejecutar(SIM_OP_SUMA, 0, 0, NULL));
    printf("OK: 18 operaciones ALU y %zu casos conocidos\n", sizeof casos / sizeof *casos);
}

int main(int argc, char **argv)
{
    if (argc == 1) { casos_conocidos(); return 0; }
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "r");
    COMPROBAR(f != NULL);
    unsigned indice, codigo_rtl, a, b;
    int leidos;
    /* Python genera índice, opcode RTL y operandos. Se ignora el opcode RTL
     * para calcular en C: la referencia SystemVerilog lo utiliza por separado. */
    while ((leidos = fscanf(f, "%u %u %x %x", &indice, &codigo_rtl, &a, &b)) == 4) {
        uint32_t resultado;
        COMPROBAR(indice < sizeof operaciones / sizeof *operaciones);
        COMPROBAR(sim_alu_ejecutar(operaciones[indice], a, b, &resultado));
        printf("ALU %u %08x %08x %08x\n", indice, a, b, (unsigned)resultado);
    }
    COMPROBAR(leidos == EOF && !ferror(f));
    COMPROBAR(fclose(f) == 0);
    return 0;
}
