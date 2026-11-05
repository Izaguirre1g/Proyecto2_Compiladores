#include "ir_spilling.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Copia textos para que ir_liberar() pueda liberar todas las instrucciones. */
static char *texto(const char *origen) {
    if (!origen) return NULL;
    size_t tam = strlen(origen) + 1;
    char *copia = malloc(tam);
    assert(copia);
    memcpy(copia, origen, tam);
    return copia;
}

/* Construye programas de prueba sin depender del parser ni del AST. */
static void instruccion(IrPrograma *ir, IrOp op, const char *dst,
                        const char *a, const char *b, const char *operador) {
    assert(ir->cantidad < 128);
    if (!ir->codigo) {
        ir->codigo = calloc(128, sizeof(IrInstr));
        assert(ir->codigo);
        ir->capacidad = 128;
    }
    IrInstr nueva = {op, texto(dst), texto(a), texto(b), texto(operador), 1};
    ir->codigo[ir->cantidad++] = nueva;
}

/* Cuenta las instrucciones de transferencia de datos que introduce la pasada. */
static size_t contar(const IrPrograma *ir, IrOp op) {
    size_t total = 0;
    for (size_t i = 0; i < ir->cantidad; ++i) {
        if (ir->codigo[i].op == op) ++total;
    }
    return total;
}

/* Asegura que un salto siga siendo la ultima instruccion del bloque. */
static void verificar_saltos(const IrPrograma *ir) {
    for (size_t j = 0; j < ir->cantidad_bloques; ++j) {
        const IrBloque *b = &ir->bloques[j];
        for (size_t i = b->inicio; i + 1 < b->fin; ++i) {
            assert(ir->codigo[i].op != IR_JUMP);
            assert(ir->codigo[i].op != IR_JFALSE);
            assert(ir->codigo[i].op != IR_JTRUE);
            assert(ir->codigo[i].op != IR_RETURN);
        }
    }
}

/* Escenario 1: muchos resultados vivos fuerzan desalojos y recargas.
 * No basta con contar almacenamientos: tiene que producirse una recarga. */
static void prueba_presion_de_registros(void) {
    IrPrograma entrada = {0}, salida = {0};
    IrSpillingMetricas m = {0};
    instruccion(&entrada, IR_FUNC, "main", NULL, NULL, NULL);
    instruccion(&entrada, IR_BIN, "$t1", "1", "2", "gauss");
    instruccion(&entrada, IR_BIN, "$t2", "3", "4", "gauss");
    instruccion(&entrada, IR_BIN, "$t3", "5", "6", "gauss");
    instruccion(&entrada, IR_BIN, "$t4", "7", "8", "gauss");
    instruccion(&entrada, IR_BIN, "$t5", "$t1", "$t4", "gauss");
    instruccion(&entrada, IR_BIN, "$t6", "$t2", "$t3", "gauss");
    instruccion(&entrada, IR_BIN, "$t7", "$t5", "$t6", "gauss");
    instruccion(&entrada, IR_COPY, "resultado", "$t7", NULL, NULL);
    instruccion(&entrada, IR_END_FUNC, "main", NULL, NULL, NULL);
    assert(ir_bloques_basicos(&entrada) == 0);
    assert(ir_aplicar_spilling(&entrada, &salida, 3, &m) == 0);
    assert(m.ranuras == 7);
    assert(m.desalojos > 0);
    assert(m.cargas > 0 && m.almacenes > 0);
    assert(contar(&salida, IR_SPILL_LOAD) == m.cargas);
    assert(contar(&salida, IR_SPILL_STORE) == m.almacenes);
    /* Toda la salida utiliza registros, no temporales virtuales. */
    for (size_t i = 0; i < salida.cantidad; ++i) {
        const IrInstr *ins = &salida.codigo[i];
        assert(!ins->dst || strncmp(ins->dst, "$t", 2) != 0);
        assert(!ins->a || strncmp(ins->a, "$t", 2) != 0);
        assert(!ins->b || strncmp(ins->b, "$t", 2) != 0);
    }
    ir_liberar(&salida);
    ir_liberar(&entrada);
}

/* Escenario 2: un resultado se define antes de una bifurcacion y se usa
 * en ambos caminos. Debe guardarse antes del salto y recuperarse despues. */
static void prueba_bifurcacion_y_cfg(void) {
    IrPrograma entrada = {0}, salida = {0};
    IrSpillingMetricas m = {0};
    instruccion(&entrada, IR_FUNC, "main", NULL, NULL, NULL);
    instruccion(&entrada, IR_BIN, "$t1", "4", "8", "gauss");
    instruccion(&entrada, IR_JFALSE, "$L1", "condicion", NULL, NULL);
    instruccion(&entrada, IR_BIN, "$t2", "$t1", "1", "gauss");
    instruccion(&entrada, IR_JUMP, "$L2", NULL, NULL, NULL);
    instruccion(&entrada, IR_LABEL, "$L1", NULL, NULL, NULL);
    instruccion(&entrada, IR_BIN, "$t3", "$t1", "2", "gauss");
    instruccion(&entrada, IR_LABEL, "$L2", NULL, NULL, NULL);
    instruccion(&entrada, IR_END_FUNC, "main", NULL, NULL, NULL);
    assert(ir_bloques_basicos(&entrada) == 0);
    assert(ir_aplicar_spilling(&entrada, &salida, 3, &m) == 0);
    assert(m.cargas >= 2);
    assert(m.almacenes >= 1);
    assert(salida.cantidad_bloques == entrada.cantidad_bloques);
    verificar_saltos(&salida);
    ir_liberar(&salida);
    ir_liberar(&entrada);
}

/* Escenario 3: una llamada invalida el cache de registros, pero los valores
 * anteriores a la llamada deben seguir disponibles al volver. */
static void prueba_llamada_y_preservacion(void) {
    IrPrograma entrada = {0}, salida = {0};
    IrSpillingMetricas m = {0};
    instruccion(&entrada, IR_FUNC, "main", NULL, NULL, NULL);
    instruccion(&entrada, IR_BIN, "$t1", "10", "20", "gauss");
    instruccion(&entrada, IR_ARG, NULL, "1", NULL, NULL);
    instruccion(&entrada, IR_CALL, "$t2", "externa", "1", NULL);
    instruccion(&entrada, IR_BIN, "$t3", "$t1", "$t2", "gauss");
    instruccion(&entrada, IR_RETURN, NULL, "$t3", NULL, NULL);
    instruccion(&entrada, IR_END_FUNC, "main", NULL, NULL, NULL);
    assert(ir_bloques_basicos(&entrada) == 0);
    assert(ir_aplicar_spilling(&entrada, &salida, 3, &m) == 0);
    assert(m.almacenes >= 2);
    assert(m.cargas >= 1);
    verificar_saltos(&salida);
    /* El temporal anterior a la llamada debe almacenarse antes de CALL. */
    size_t llamada = 0, guardado = 0;
    for (size_t i = 0; i < salida.cantidad; ++i) {
        if (salida.codigo[i].op == IR_CALL) llamada = i;
        if (salida.codigo[i].op == IR_SPILL_STORE &&
            strcmp(salida.codigo[i].dst, "$spill0") == 0) guardado = i;
    }
    assert(guardado < llamada);
    ir_liberar(&salida);
    ir_liberar(&entrada);
}

/* Rechaza configuraciones inseguras sin modificar la salida recibida. */
static void prueba_argumentos_invalidos(void) {
    IrPrograma entrada = {0}, salida = {0};
    IrSpillingMetricas m = {0};
    instruccion(&entrada, IR_FUNC, "main", NULL, NULL, NULL);
    assert(ir_bloques_basicos(&entrada) == 0);
    assert(ir_aplicar_spilling(&entrada, &salida, 2, &m) == 2);
    assert(!salida.codigo);
    ir_liberar(&entrada);
}

/* Ejecuta los tres escenarios obligatorios y una prueba de rechazo. */
int main(void) {
    prueba_presion_de_registros();
    prueba_bifurcacion_y_cfg();
    prueba_llamada_y_preservacion();
    prueba_argumentos_invalidos();
    puts("OK: spilling - presion, CFG, llamadas y argumentos invalidos");
    return 0;
}
