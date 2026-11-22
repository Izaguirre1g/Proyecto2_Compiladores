/* Issue #7: contrato publico USE/DEF sobre las 24 operaciones IrOp. */
#include "ir_defuse.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define INST(OP, D, A, B) { (OP), (char *)(D), (char *)(A), (char *)(B), NULL, 1 }

typedef struct {
    IrInstr i;
    const char *usos[3];
    size_t n_usos;
    const char *defs[1];
    size_t n_defs;
    bool leer_mem, escribir_mem, barrera, control;
    const char *objeto;
} Caso;

static const Caso casos[] = {
    [IR_FUNC] = {INST(IR_FUNC, "main", NULL, NULL),
                 {NULL}, 0, {NULL}, 0, false,false,false,true,NULL},
    [IR_END_FUNC] = {INST(IR_END_FUNC, "main", NULL, NULL),
                 {NULL}, 0, {NULL}, 0, false,false,false,true,NULL},
    [IR_PARAM_DECL] = {INST(IR_PARAM_DECL, "p", NULL, NULL),
                 {NULL}, 0, {"p"}, 1, false,false,false,false,NULL},
    [IR_COPY] = {INST(IR_COPY, "$t1", "42", NULL),
                 {NULL}, 0, {"$t1"}, 1, false,false,false,false,NULL},
    [IR_BIN] = {INST(IR_BIN, "$t3", "$t1", "$t2"),
                 {"$t1", "$t2"}, 2, {"$t3"}, 1, false,false,false,false,NULL},
    [IR_UNARY] = {INST(IR_UNARY, "y", "x", NULL),
                 {"x"}, 1, {"y"}, 1, false,false,false,false,NULL},
    [IR_LABEL] = {INST(IR_LABEL, "$L1", NULL, NULL),
                 {NULL}, 0, {NULL}, 0, false,false,false,true,NULL},
    [IR_JUMP] = {INST(IR_JUMP, "$L1", NULL, NULL),
                 {NULL}, 0, {NULL}, 0, false,false,false,true,NULL},
    [IR_JFALSE] = {INST(IR_JFALSE, "$L1", "declare_false", NULL),
                 {NULL}, 0, {NULL}, 0, false,false,false,true,NULL},
    [IR_JTRUE] = {INST(IR_JTRUE, "$L1", "condition", NULL),
                 {"condition"}, 1, {NULL}, 0, false,false,false,true,NULL},
    [IR_ARG] = {INST(IR_ARG, NULL, "param", NULL),
                 {"param"}, 1, {NULL}, 0, false,false,true,false,NULL},
    [IR_CALL] = {INST(IR_CALL, "$t9", "f", "2"),
                 {NULL}, 0, {"$t9"}, 1, true,true,true,true,NULL},
    [IR_RETURN] = {INST(IR_RETURN, NULL, "$t9", NULL),
                 {"$t9"}, 1, {NULL}, 0, false,false,false,true,NULL},
    [IR_ARRAY_DECL] = {INST(IR_ARRAY_DECL, "mat", "2", NULL),
                 {NULL}, 0, {"mat"}, 1, false,true,false,false,NULL},
    [IR_ARRAY_DIM] = {INST(IR_ARRAY_DIM, "mat", "0", "n"),
                 {"mat", "n"}, 2, {NULL}, 0, true,true,false,false,NULL},
    [IR_INDEX_REF] = {INST(IR_INDEX_REF, "$t10", "mat", "indice"),
                 {"mat", "indice"}, 2, {"$t10"}, 1, false,false,false,false,NULL},
    [IR_LOAD] = {INST(IR_LOAD, "$t11", "$t10", NULL),
                 {"$t10"}, 1, {"$t11"}, 1, true,false,false,false,NULL},
    [IR_STORE] = {INST(IR_STORE, "$t10", "$t11", NULL),
                 {"$t10", "$t11"}, 2, {NULL}, 0, false,true,false,false,NULL},
    [IR_LIST_DECL] = {INST(IR_LIST_DECL, "lista", NULL, NULL),
                 {NULL}, 0, {"lista"}, 1, false,true,false,false,NULL},
    [IR_LIST_ADD] = {INST(IR_LIST_ADD, "lista", "3", NULL),
                 {"lista"}, 1, {NULL}, 0, true,true,false,false,NULL},
    [IR_LIST_REMOVE] = {INST(IR_LIST_REMOVE, "lista", "pos", NULL),
                 {"lista", "pos"}, 2, {NULL}, 0, true,true,false,false,NULL},
    [IR_SIZE] = {INST(IR_SIZE, "$t12", "lista", NULL),
                 {"lista"}, 1, {"$t12"}, 1, true,false,false,false,NULL},
    [IR_SPILL_LOAD] = {INST(IR_SPILL_LOAD, "$r0", "$spill0", NULL),
                 {NULL}, 0, {"$r0"}, 1, true,false,false,false,"$spill0"},
    [IR_SPILL_STORE] = {INST(IR_SPILL_STORE, "$spill0", "$r0", NULL),
                 {"$r0"}, 1, {NULL}, 0, false,true,false,false,"$spill0"}
};

static void verificar(const Caso *c)
{
    IrDefUse r;
    assert(ir_consultar_def_usos(&c->i, &r) == 0);
    assert(r.cantidad_usos == c->n_usos);
    assert(r.cantidad_definiciones == c->n_defs);
    for (size_t i = 0; i < r.cantidad_usos; ++i)
        assert(!strcmp(r.usos[i], c->usos[i]));
    for (size_t i = 0; i < r.cantidad_definiciones; ++i)
        assert(!strcmp(r.definiciones[i], c->defs[i]));
    assert(r.lee_memoria == c->leer_mem);
    assert(r.escribe_memoria == c->escribir_mem);
    assert(r.barrera == c->barrera);
    assert(r.control == c->control);
    if (c->objeto) assert(r.objeto_memoria && !strcmp(r.objeto_memoria, c->objeto));
    else assert(r.objeto_memoria == NULL);
}

static void nombres_y_literales(void)
{
    const char *no[] = {NULL, "", "0", "+3", "-456", "1234567890123456789012345678901234567890",
        "declare_true", "declare_false", "\"hola\"", "'c'", "$ñ$", "$L1", "$spill0", "12foo", "$tA"};
    const char *si[] = {"x", "_x9", "largo", "$t1", "$r0", "m2", "área"};
    for (size_t i = 0; i < sizeof no / sizeof *no; ++i)
        assert(!ir_es_nombre_valor(no[i]));
    for (size_t i = 0; i < sizeof si / sizeof *si; ++i)
        assert(ir_es_nombre_valor(si[i]));

    IrDefUse r;
    IrInstr i = INST(IR_COPY, "a", "$ñ$", NULL);
    assert(ir_consultar_def_usos(&i, &r) == 0 && r.cantidad_usos == 0);
    i.a = (char *)"declare_true";
    assert(ir_consultar_def_usos(&i, &r) == 0 && r.cantidad_usos == 0);
    i.a = (char *)"a";
    assert(ir_consultar_def_usos(&i, &r) == 0 && r.cantidad_usos == 1);
    assert(!strcmp(r.usos[0], "a") && !strcmp(r.definiciones[0], "a"));

    i = (IrInstr)INST(IR_BIN, "$t3", "$t1", "$t1");
    assert(ir_consultar_def_usos(&i, &r) == 0 && r.cantidad_usos == 1);
    i = (IrInstr)INST(IR_ARRAY_DIM, "mat", "dimension", "largo");
    assert(ir_consultar_def_usos(&i, &r) == 0 && r.cantidad_usos == 3);
    i = (IrInstr)INST(IR_RETURN, NULL, NULL, NULL);
    assert(ir_consultar_def_usos(&i, &r) == 0 && r.cantidad_usos == 0 && r.control);
}

static void entradas_invalidas(void)
{
    IrDefUse r = {0};
    assert(ir_consultar_def_usos(NULL, &r) == 1);
    assert(r.cantidad_usos == 0 && r.cantidad_definiciones == 0);
    IrInstr i = INST(IR_BIN, "x", "y", NULL);
    assert(ir_consultar_def_usos(&i, &r) == 1 && r.cantidad_definiciones == 0);
    i = (IrInstr)INST(IR_SPILL_STORE, "$spill0", "$t0", NULL);
    assert(ir_consultar_def_usos(&i, &r) == 1 && !r.escribe_memoria);
    i = (IrInstr)INST(IR_SPILL_LOAD, "$r0", "$spillx", NULL);
    assert(ir_consultar_def_usos(&i, &r) == 1 && !r.lee_memoria);
    i = (IrInstr)INST(IR_COPY, "5", "x", NULL);
    assert(ir_consultar_def_usos(&i, &r) == 1 && r.cantidad_usos == 0);
    i = (IrInstr)INST((IrOp)999, "x", "y", NULL);
    assert(ir_consultar_def_usos(&i, &r) == 1 && r.cantidad_definiciones == 0);
    assert(ir_consultar_def_usos(&i, NULL) == 1);
}

int main(void)
{
    assert(sizeof casos / sizeof *casos == (size_t)IR_SPILL_STORE + 1);
    for (size_t i = 0; i < sizeof casos / sizeof *casos; ++i) {
        assert(casos[i].i.op == (IrOp)i);
        verificar(&casos[i]);
    }
    nombres_y_literales();
    entradas_invalidas();
    puts("OK: API USE/DEF 24 operaciones, literales, memoria, llamadas, spilling y rechazos");
    return 0;
}
