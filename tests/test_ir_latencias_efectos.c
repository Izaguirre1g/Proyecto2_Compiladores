/* Issue #3: contrato de efectos IR y regla N+3 con el scheduler fisico real.
 * Aisla el test de Flex, Bison, tablas semanticas y ELF del compilador. */
#include "ir_latencias_efectos.h"
#include "vliw_scheduler.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define INST(O,D,A,B) ((IrInstr){(O), (char *)(D), (char *)(A), (char *)(B), NULL, 1})
#define R(N) (UINT64_C(1) << (N))

_Static_assert(VLIW_RAW_DISTANCIA == 3, "La ISA exige leer desde N+3");

static IrAmbitoNombre resolver(const char *nombre, void *contexto)
{
    (void)contexto;
    if (!strcmp(nombre, "global") || !strcmp(nombre, "contador"))
        return IR_AMBITO_GLOBAL;
    if (!strcmp(nombre, "local") || !strcmp(nombre, "direccion"))
        return IR_AMBITO_LOCAL;
    return IR_AMBITO_DESCONOCIDO;
}

static IrAmbitoNombre resolver_invalido(const char *nombre, void *contexto)
{
    (void)nombre;
    (void)contexto;
    return (IrAmbitoNombre)99;
}

static void efectos_globales_y_memoria(void)
{
    const IrContextoEfectos ctx = {resolver, NULL};
    IrEfectos e = {0};
    IrInstr x = INST(IR_COPY, "$t0", "42", NULL);
    assert(ir_efectos_consultar(&x, &ctx, &e) == 0);
    assert(!e.lee_memoria && !e.escribe_memoria && !e.barrera);

    x = INST(IR_COPY, "$t0", "global", NULL);
    assert(ir_efectos_consultar(&x, &ctx, &e) == 0);
    assert(e.lee_memoria && e.puede_leer_global && !e.escribe_memoria);
    assert(!e.ambito_incierto);

    x = INST(IR_COPY, "contador", "$t0", NULL);
    assert(ir_efectos_consultar(&x, &ctx, &e) == 0);
    assert(e.escribe_memoria && e.puede_escribir_global);
    assert(!e.lee_memoria);

    x = INST(IR_COPY, "local", "2", NULL);
    assert(ir_efectos_consultar(&x, &ctx, &e) == 0);
    assert(!e.escribe_memoria && !e.puede_escribir_global);

    x = INST(IR_COPY, "sin_resolver", "2", NULL);
    assert(ir_efectos_consultar(&x, &ctx, &e) == 0);
    assert(e.escribe_memoria && e.puede_escribir_global && e.ambito_incierto);
    assert(ir_efectos_consultar(&x, NULL, &e) == 0);
    assert(e.ambito_incierto && e.escribe_memoria);

    x = INST(IR_PARAM_DECL, "param", NULL, NULL);
    assert(ir_efectos_consultar(&x, NULL, &e) == 0);
    assert(!e.escribe_memoria && !e.ambito_incierto);

    x = INST(IR_CALL, "$t4", "externa", "2");
    assert(ir_efectos_consultar(&x, &ctx, &e) == 0);
    assert(e.lee_memoria && e.escribe_memoria && e.barrera && e.control);
    assert(e.puede_leer_global && e.puede_escribir_global && e.ambito_incierto);
    assert(e.defuse.cantidad_usos == 0 && e.defuse.cantidad_definiciones == 1);

    x = INST(IR_ARG, NULL, "global", NULL);
    assert(ir_efectos_consultar(&x, &ctx, &e) == 0);
    assert(e.lee_memoria && e.barrera && !e.escribe_memoria);

    x = INST(IR_SPILL_LOAD, "$r0", "$spill2", NULL);
    assert(ir_efectos_consultar(&x, NULL, &e) == 0);
    assert(e.lee_memoria && !e.escribe_memoria && !e.ambito_incierto);
    assert(!strcmp(e.defuse.objeto_memoria, "$spill2"));

    x = INST(IR_SPILL_STORE, "$spill2", "$r0", NULL);
    assert(ir_efectos_consultar(&x, NULL, &e) == 0);
    assert(e.escribe_memoria && !e.lee_memoria && !e.ambito_incierto);

    x = INST(IR_LOAD, "$t1", "direccion", NULL);
    assert(ir_efectos_consultar(&x, &ctx, &e) == 0);
    assert(e.lee_memoria && !e.escribe_memoria);

    x = INST(IR_STORE, "direccion", "$t1", NULL);
    assert(ir_efectos_consultar(&x, &ctx, &e) == 0);
    assert(e.escribe_memoria && !e.lee_memoria);

    x = INST(IR_ARRAY_DIM, "arreglo", "0", "local");
    assert(ir_efectos_consultar(&x, &ctx, &e) == 0);
    assert(e.lee_memoria && e.escribe_memoria);
    x = INST(IR_LIST_ADD, "lista", "$t1", NULL);
    assert(ir_efectos_consultar(&x, &ctx, &e) == 0);
    assert(e.lee_memoria && e.escribe_memoria);
    x = INST(IR_LIST_DECL, "lista", NULL, NULL);
    assert(ir_efectos_consultar(&x, &ctx, &e) == 0);
    assert(e.escribe_memoria);

    /* Se prohíbe publicar efectos parciales para entradas inválidas. */
    IrContextoEfectos mala = {resolver_invalido, NULL};
    x = INST(IR_COPY, "local", "1", NULL);
    assert(ir_efectos_consultar(&x, &mala, &e) != 0);
    assert(!e.lee_memoria && !e.escribe_memoria && !e.barrera);
    x = INST(IR_SPILL_LOAD, "$r0", "$t1", NULL);
    assert(ir_efectos_consultar(&x, &ctx, &e) != 0);
    assert(ir_efectos_consultar(NULL, &ctx, &e) != 0);
    assert(ir_efectos_consultar(&x, &ctx, NULL) != 0);
}

static IrDependenciaSimbolica dependencia(IrInstr a, IrInstr b,
                                        const IrContextoEfectos *ctx)
{
    IrDependenciaSimbolica r = {0};
    assert(ir_dependencia_consultar(&a, &b, ctx, &r) == 0);
    return r;
}

static void tipos_dependencias(void)
{
    const IrContextoEfectos ctx = {resolver, NULL};
    IrDependenciaSimbolica r;
    r = dependencia(INST(IR_COPY, "$t0", "1", NULL),
                    INST(IR_BIN, "$t1", "$t0", "2"), &ctx);
    assert(r.tipos == IR_DEP_RAW && r.distancia_min_bundles == 3);

    r = dependencia(INST(IR_COPY, "$t0", "local", NULL),
                    INST(IR_COPY, "local", "2", NULL), &ctx);
    assert(r.tipos == IR_DEP_WAR && r.distancia_min_bundles == 1);

    r = dependencia(INST(IR_COPY, "local", "1", NULL),
                    INST(IR_COPY, "local", "2", NULL), &ctx);
    assert(r.tipos == IR_DEP_WAW && r.distancia_min_bundles == 1);

    r = dependencia(INST(IR_SPILL_STORE, "$spill0", "$r0", NULL),
                    INST(IR_SPILL_LOAD, "$r1", "$spill0", NULL), &ctx);
    assert(r.tipos == IR_DEP_MEMORIA && r.distancia_min_bundles == 1);

    r = dependencia(INST(IR_LOAD, "$t0", "direccion", NULL),
                    INST(IR_LOAD, "$t1", "direccion", NULL), &ctx);
    assert(r.tipos == IR_DEP_MEMORIA && r.distancia_min_bundles == 1);

    r = dependencia(INST(IR_COPY, "global", "1", NULL),
                    INST(IR_LOAD, "$t1", "direccion", NULL), &ctx);
    assert(r.tipos == IR_DEP_MEMORIA && r.distancia_min_bundles == 1);

    /* Ningun efecto inventado para escalares locales e independientes. */
    r = dependencia(INST(IR_COPY, "local", "1", NULL),
                    INST(IR_COPY, "$t1", "2", NULL), &ctx);
    assert(r.tipos == 0 && r.distancia_min_bundles == 0);

    r = dependencia(INST(IR_COPY, "$t1", "1", NULL),
                    INST(IR_CALL, "$t2", "externa", "0"), &ctx);
    assert((r.tipos & IR_DEP_BARRERA) && r.distancia_min_bundles == 3);
    r = dependencia(INST(IR_CALL, "$t2", "externa", "0"),
                    INST(IR_COPY, "$t3", "2", NULL), &ctx);
    assert((r.tipos & IR_DEP_BARRERA) && r.distancia_min_bundles == 3);

    r = dependencia(INST(IR_ARG, NULL, "$t0", NULL),
                    INST(IR_COPY, "$t2", "1", NULL), &ctx);
    assert(r.tipos == IR_DEP_BARRERA && r.distancia_min_bundles == 1);
    r = dependencia(INST(IR_COPY, "$t0", "1", NULL),
                    INST(IR_JFALSE, "L1", "$t0", NULL), &ctx);
    assert((r.tipos & IR_DEP_RAW) && (r.tipos & IR_DEP_CONTROL));
    assert(r.distancia_min_bundles == 3);

    IrInstr invalida = INST(IR_SPILL_STORE, "$t0", "$r0", NULL);
    IrInstr valida = INST(IR_COPY, "$t1", "3", NULL);
    r = (IrDependenciaSimbolica){IR_DEP_RAW, 3};
    assert(ir_dependencia_consultar(&invalida, &valida, &ctx, &r) != 0);
    assert(!r.tipos && !r.distancia_min_bundles);
    assert(ir_dependencia_consultar(&valida, &valida, &ctx, NULL) != 0);
}

/* N+3 no se inventa en IR: se verifica mediante el calendarizador de Javier.
 * Test con metadatos fisicos completos aportados explicitamente por el fixture. */
static void calendario_fisico(void)
{
    VliwInstruccion operaciones[] = {
        {.id = 1, .slot = VLIW_SLOT_ALU, .escribe = R(16), .bloque = 0},
        {.id = 2, .slot = VLIW_SLOT_LSU, .lee = R(16),
         .efectos = VLIW_EF_LEE_MEMORIA, .bloque = 0}
    };
    VliwDependencia deps[] = {{0, 1, VLIW_DEP_RAW, VLIW_RAW_DISTANCIA}};
    VliwBloque bloque = {0, 2, NULL, 0};
    VliwEntrada entrada = {operaciones, 2, deps, 1, &bloque, 1, true};
    VliwPlan *plan = NULL;
    VliwDiagnostico diagnostico = {0};
    assert(vliw_planificar(&entrada, &plan, &diagnostico) == VLIW_OK);
    assert(plan != NULL);
    assert(plan->bundle_por_instruccion[1] >=
           plan->bundle_por_instruccion[0] + VLIW_RAW_DISTANCIA);
    assert(vliw_verificar(&entrada, plan, &diagnostico) == VLIW_OK);
    vliw_liberar(&plan);

    deps[0].distancia = 2;
    assert(vliw_planificar(&entrada, &plan, &diagnostico) == VLIW_DEPENDENCIA);
    assert(plan == NULL);
    deps[0].distancia = 3;
    entrada.metadatos_completos = false;
    assert(vliw_planificar(&entrada, &plan, &diagnostico) == VLIW_METADATOS);
    assert(plan == NULL);

    /* Una llamada es una BRU con efectos desconocidos y barrera de 3. */
    entrada.metadatos_completos = true;
    operaciones[1] = (VliwInstruccion){.id = 2, .slot = VLIW_SLOT_BRU,
        .efectos = VLIW_EF_DESCONOCIDOS, .control = VLIW_CONTROL_LLAMADA,
        .bloque = 0};
    deps[0] = (VliwDependencia){0, 1, VLIW_DEP_BARRERA, 3};
    assert(vliw_planificar(&entrada, &plan, &diagnostico) == VLIW_OK);
    assert(plan->bundle_por_instruccion[1] >= plan->bundle_por_instruccion[0] + 3);
    assert(vliw_verificar(&entrada, plan, &diagnostico) == VLIW_OK);
    vliw_liberar(&plan);

    /* Ni una llamada ni un retorno se pueden clasificar como operación pura. */
    operaciones[1].efectos = VLIW_EF_NINGUNO;
    assert(vliw_planificar(&entrada, &plan, &diagnostico) == VLIW_METADATOS);
    assert(plan == NULL);
}

int main(void)
{
    efectos_globales_y_memoria();
    tipos_dependencias();
    calendario_fisico();
    puts("OK: #3 N+3 real, RAW/WAR/WAW, llamadas, globales, memoria, spills y rechazos");
    return 0;
}
