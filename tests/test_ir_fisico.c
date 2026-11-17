/* #23: registro virtual $tN -> registro logico $rN -> registro ISA x16..x23.
 * Ejecuta el spilling REAL sobre bloques basicos, sin emular el asignador. */
#include "ir_registros_fisicos.h"
#include "ir_spilling.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *duplicar(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    assert(p);
    memcpy(p, s, n);
    return p;
}

static void agregar(IrPrograma *p, IrOp op, const char *dst,
                    const char *a, const char *b, const char *operador)
{
    assert(p->cantidad < 64);
    if (p->cantidad == p->capacidad) {
        size_t n = p->capacidad ? p->capacidad * 2 : 16;
        IrInstr *nuevo = realloc(p->codigo, n * sizeof *nuevo);
        assert(nuevo);
        p->codigo = nuevo;
        p->capacidad = n;
    }
    p->codigo[p->cantidad++] = (IrInstr){op, duplicar(dst), duplicar(a),
                                        duplicar(b), duplicar(operador), 1};
}

static void comprobar_mapeo(void)
{
    unsigned fisico = 99;
    assert(!ir_fisico_configuracion_valida(2));
    assert(ir_fisico_configuracion_valida(3));
    assert(ir_fisico_configuracion_valida(8));
    assert(!ir_fisico_configuracion_valida(9));
    for (size_t n = 3; n <= 8; ++n) {
        for (size_t i = 0; i < n; ++i) {
            char nombre[20];
            snprintf(nombre, sizeof nombre, "$r%zu", i);
            assert(ir_fisico_mapear(nombre, n, &fisico) == 0);
            assert(fisico == IR_FISICO_PRIMER_REGISTRO + i);
            assert(fisico >= 16 && fisico <= 23);
            assert(fisico != 28 && fisico != 29 && fisico != 0 &&
                   fisico != 1 && fisico != 2);
        }
        char invalido[20];
        snprintf(invalido, sizeof invalido, "$r%zu", n);
        fisico = 99;
        assert(ir_fisico_mapear(invalido, n, &fisico) == 2);
        assert(fisico == 99);
    }
    const char *invalidos[] = {
        NULL, "", "r0", "x16", "$t0", "$r", "$r-1", "$rA", "$r8",
        "$r99999999999999999999999999999999999999999999999", "$spill0"
    };
    for (size_t i = 0; i < sizeof invalidos / sizeof *invalidos; ++i) {
        fisico = 55;
        assert(ir_fisico_mapear(invalidos[i], 8, &fisico) == 2);
        assert(fisico == 55);
    }
    assert(ir_fisico_mapear("$r0", 2, &fisico) == 2);
    assert(ir_fisico_mapear("$r0", 9, &fisico) == 2);
    assert(ir_fisico_mapear("$r0", 8, NULL) == 2);
    /* La regla acepta ceros iniciales como el parser previo de ir_vliw.c. */
    assert(ir_fisico_mapear("$r007", 8, &fisico) == 0 && fisico == 23);
}

static void construir_presion(IrPrograma *ir)
{
    agregar(ir, IR_FUNC, "main", NULL, NULL, NULL);
    for (size_t i = 0; i < 8; ++i) {
        char tmp[20], val[20];
        snprintf(tmp, sizeof tmp, "$t%zu", i);
        snprintf(val, sizeof val, "%zu", i + 1);
        agregar(ir, IR_COPY, tmp, val, NULL, NULL);
    }
    agregar(ir, IR_BIN, "$t8", "$t0", "$t7", "gauss");
    agregar(ir, IR_BIN, "$t9", "$t1", "$t8", "neumann");
    agregar(ir, IR_END_FUNC, "main", NULL, NULL, NULL);
    assert(ir_bloques_basicos(ir) == 0);
}

static void comprobar_spilling_real(size_t registros)
{
    IrPrograma entrada = {0}, asignada = {0};
    IrSpillingMetricas m = {0};
    construir_presion(&entrada);
    assert(ir_fisico_validar_programa(&entrada, registros, NULL) == 2);
    assert(ir_aplicar_spilling(&entrada, &asignada, registros, &m) == 0);
    assert(m.registros == registros && m.ranuras == 10);
    size_t error_en = 0;
    assert(ir_fisico_validar_programa(&asignada, registros, &error_en) == 0);
    assert(error_en == SIZE_MAX);
    int vio_registro_alto = 0, vio_load = 0, vio_store = 0;
    for (size_t i = 0; i < asignada.cantidad; ++i) {
        const IrInstr *x = &asignada.codigo[i];
        const char *campos[] = {x->dst, x->a, x->b};
        for (size_t j = 0; j < 3; ++j) {
            const char *s = campos[j];
            if (s && s[0] == '$' && s[1] == 'r') {
                unsigned fisico = 0;
                assert(ir_fisico_mapear(s, registros, &fisico) == 0);
                if (fisico == 16u + registros - 1u) vio_registro_alto = 1;
            }
        }
        if (x->op == IR_SPILL_LOAD) vio_load = 1;
        if (x->op == IR_SPILL_STORE) vio_store = 1;
    }
    assert(vio_registro_alto);
    assert(vio_store);
    if (registros == 3) assert(vio_load);
    ir_liberar(&asignada);
    ir_liberar(&entrada);
}

static void comprobar_rechazos(void)
{
    IrInstr instrucciones[] = {
        {IR_COPY, "$r0", "1", NULL, NULL, 10},
        {IR_BIN, "$r1", "$r0", "2", "gauss", 11},
        {IR_SPILL_LOAD, "$r2", "$spill0", NULL, NULL, 12}
    };
    IrPrograma p = {0};
    p.codigo = instrucciones;
    p.cantidad = 3;
    size_t indice = SIZE_MAX;
    assert(ir_fisico_validar_programa(&p, 3, &indice) == 0);
    assert(indice == SIZE_MAX);
    instrucciones[1].a = "$t3";
    assert(ir_fisico_validar_programa(&p, 3, &indice) == 2 && indice == 1);
    instrucciones[1].a = "$r0";
    instrucciones[2].dst = "$r3";
    assert(ir_fisico_validar_programa(&p, 3, &indice) == 2 && indice == 2);
    instrucciones[2].dst = "$r2";
    instrucciones[0].dst = "$rfoo";
    assert(ir_fisico_validar_programa(&p, 3, &indice) == 2 && indice == 0);
    instrucciones[0].dst = "$r0";
    instrucciones[1].a = "$r$"; /* literal de caracter BAL, no registro */
    assert(ir_fisico_validar_programa(&p, 3, &indice) == 0);
    instrucciones[1].a = "$t$";
    assert(ir_fisico_validar_programa(&p, 3, &indice) == 0);
    instrucciones[1].a = "$r0";
    instrucciones[2].a = "bad";
    assert(ir_fisico_validar_programa(&p, 3, &indice) == 2 && indice == 2);
    assert(ir_fisico_validar_programa(NULL, 3, &indice) == 2);
    assert(indice == SIZE_MAX);
    assert(ir_fisico_validar_programa(&p, 9, &indice) == 2);
    assert(indice == SIZE_MAX);
}

int main(void)
{
    comprobar_mapeo();
    comprobar_spilling_real(3);
    comprobar_spilling_real(8);
    comprobar_rechazos();
    puts("OK: #23 mapeo $tN -> $rN -> x16..x23, presion, spills y rechazos");
    return 0;
}
