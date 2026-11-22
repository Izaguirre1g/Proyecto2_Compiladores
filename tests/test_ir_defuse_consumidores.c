/* Issue #7: uso real de la API IR en DCE y reordenamiento, sin Flex/Bison. */
#include "ir_defuse.h"
#include "dce.h"
#include "reordenamiento.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define I(OP,D,A,B) { (OP), (char *)(D), (char *)(A), (char *)(B), NULL, 1 }

static int contiene(const DceVivas *v, const char *nombre)
{
    for (size_t i = 0; i < v->cantidad_variables; ++i)
        if (!strcmp(v->variables[i], nombre)) return 1;
    return 0;
}

static void vivacidad_en_spills(void)
{
    IrInstr codigo[] = {
        I(IR_FUNC, "aux", NULL, NULL),
        I(IR_SPILL_STORE, "$spill1", "$r0", NULL), /* uso antes de definir */
        I(IR_SPILL_LOAD, "$r0", "$spill0", NULL),
        I(IR_BIN, "$t1", "$r0", "p"),
        I(IR_RETURN, NULL, "$t1", NULL),
        I(IR_END_FUNC, "aux", NULL, NULL)
    };
    IrBloque bloque = {.inicio = 0, .fin = 6};
    IrPrograma ir = {.codigo = codigo, .cantidad = 6,
                     .bloques = &bloque, .cantidad_bloques = 1};
    DceVivas v = {0};
    assert(dce_calcular_vivas(&ir, &v) == 0);
    assert(contiene(&v, "$r0") && contiene(&v, "$t1") && contiene(&v, "p"));
    assert(!contiene(&v, "$spill0") && !contiene(&v, "$spill1"));
    assert(dce_esta_viva(&v, 0, "$r0", 0));
    assert(dce_esta_viva(&v, 0, "p", 0));
    assert(!dce_esta_viva(&v, 0, "$t1", 0));
    dce_liberar_vivas(&v);
}

static void colecciones_y_invalidos(void)
{
    IrInstr codigo[] = {
        I(IR_FUNC, "aux", NULL, NULL),
        I(IR_ARRAY_DECL, "mat", "2", NULL),
        I(IR_ARRAY_DIM, "mat", "0", "longitud"),
        I(IR_LIST_DECL, "lista", NULL, NULL),
        I(IR_LIST_ADD, "lista", "valor", NULL),
        I(IR_RETURN, NULL, "valor", NULL),
        I(IR_END_FUNC, "aux", NULL, NULL)
    };
    IrBloque bloque = {.inicio = 0, .fin = 7};
    IrPrograma ir = {.codigo = codigo, .cantidad = 7,
                     .bloques = &bloque, .cantidad_bloques = 1};
    DceVivas v = {0};
    assert(dce_calcular_vivas(&ir, &v) == 0);
    assert(contiene(&v, "mat") && contiene(&v, "lista"));
    assert(contiene(&v, "valor") && contiene(&v, "longitud"));
    assert(dce_esta_viva(&v, 0, "longitud", 0));
    assert(dce_esta_viva(&v, 0, "valor", 0));
    assert(!dce_esta_viva(&v, 0, "mat", 0)); /* se define antes de usar */
    dce_liberar_vivas(&v);

    /* Un operando invalido no se interpreta como ausencia de uso. */
    codigo[2] = (IrInstr)I(IR_SPILL_LOAD, "$r0", "$t9", NULL);
    assert(dce_calcular_vivas(&ir, &v) == 2);
}

/* El DCE debe conservar CALL y la escritura de spilling aunque su
 * resultado no este vivo, y quitar la copia escalar inutil. */
static char *duplicar(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    assert(p);
    memcpy(p, s, n);
    return p;
}

static void eliminacion_con_efectos(void)
{
    const IrInstr origen[] = {
        I(IR_FUNC, "aux", NULL, NULL),
        I(IR_COPY, "basura", "9", NULL),
        I(IR_COPY, "v", "1", NULL),
        I(IR_ARG, NULL, "v", NULL),
        I(IR_CALL, "$t2", "externa", "1"),
        I(IR_SPILL_LOAD, "$r0", "$spill0", NULL),
        I(IR_SPILL_STORE, "$spill1", "$r0", NULL),
        I(IR_RETURN, NULL, "v", NULL),
        I(IR_END_FUNC, "aux", NULL, NULL)
    };
    const size_t n = sizeof origen / sizeof *origen;
    IrPrograma ir = {0};
    ir.codigo = calloc(n, sizeof *ir.codigo);
    assert(ir.codigo);
    ir.cantidad = ir.capacidad = n;
    for (size_t i = 0; i < n; ++i) {
        ir.codigo[i] = origen[i];
        ir.codigo[i].dst = duplicar(origen[i].dst);
        ir.codigo[i].a = duplicar(origen[i].a);
        ir.codigo[i].b = duplicar(origen[i].b);
    }
    assert(ir_bloques_basicos(&ir) == 0);
    DceResultado r = {0};
    assert(dce_eliminar(&ir, &r) == 0);
    bool basura = false, llamada = false, guardado = false, cargado = false;
    for (size_t i = 0; i < ir.cantidad; ++i) {
        IrInstr *x = &ir.codigo[i];
        if (x->op == IR_COPY && x->dst && !strcmp(x->dst, "basura")) basura = true;
        if (x->op == IR_CALL) llamada = true;
        if (x->op == IR_SPILL_STORE) guardado = true;
        if (x->op == IR_SPILL_LOAD) cargado = true;
    }
    assert(!basura && llamada && guardado && cargado);
    dce_liberar_resultado(&r);
    ir_liberar(&ir);
}

static int hay_dependencia(const RoGrafoBloque *g, size_t de, size_t a, unsigned tipo)
{
    for (size_t i = 0; i < g->cantidad_aristas; ++i)
        if (g->aristas[i].de == de && g->aristas[i].a == a &&
            (g->aristas[i].tipos & tipo)) return 1;
    return 0;
}

static void dag_spills_memoria_llamada(void)
{
    IrInstr codigo[] = {
        I(IR_FUNC, "aux", NULL, NULL),
        I(IR_SPILL_STORE, "$spill0", "$r0", NULL),
        I(IR_SPILL_LOAD, "$r1", "$spill0", NULL),
        I(IR_BIN, "$t1", "$r1", "3"),
        I(IR_ARG, NULL, "$t1", NULL),
        I(IR_CALL, "$t2", "f", "1"),
        I(IR_LOAD, "$t3", "dir", NULL),
        I(IR_END_FUNC, "aux", NULL, NULL)
    };
    IrBloque bloque = {.inicio = 0, .fin = 8};
    IrPrograma ir = {.codigo = codigo, .cantidad = 8,
                     .bloques = &bloque, .cantidad_bloques = 1};
    RoGrafoBloque g = {0};
    assert(ro_analizar_bloque(&ir, 0, 3, &g) == 0);
    assert(g.primero == 1 && g.cantidad_nodos == 7);
    assert(hay_dependencia(&g, 0, 1, RO_DEP_MEMORIA)); /* store -> load */
    assert(hay_dependencia(&g, 1, 2, RO_DEP_RAW));     /* $r1 -> $t1 */
    assert(hay_dependencia(&g, 3, 4, RO_DEP_EFECTO)); /* arg -> call */
    assert(hay_dependencia(&g, 4, 5, RO_DEP_MEMORIA));/* call -> load */
    assert(g.nodos[0].nlee == 1 && !strcmp(g.nodos[0].lee[0], "$r0"));
    assert(g.nodos[1].nlee == 0); /* no se confunde $spill0 con registro */
    ro_grafo_liberar(&g);

    codigo[1] = (IrInstr)I(IR_SPILL_STORE, "$spill0", "entrada_incorrecta", NULL);
    assert(ro_analizar_bloque(&ir, 0, 3, &g) == 2);
}

/* Los limites de funcion son control fijo, no barreras con efectos.
 * Un uso repetido (x+x) representa un solo recurso USE y una arista RAW. */
static void limites_y_usos_repetidos(void)
{
    IrInstr codigo[] = {
        I(IR_FUNC, "aux", NULL, NULL),
        I(IR_COPY, "x", "3", NULL),
        I(IR_BIN, "$t1", "x", "x"),
        I(IR_COPY, "y", "$t1", NULL),
        I(IR_END_FUNC, "aux", NULL, NULL)
    };
    IrBloque bloque = {.inicio = 0, .fin = 5};
    IrPrograma ir = {.codigo = codigo, .cantidad = 5,
                     .bloques = &bloque, .cantidad_bloques = 1};
    IrDefUse info = {0};
    assert(ir_consultar_def_usos(&codigo[0], &info) == 0);
    assert(info.control && !info.barrera);
    assert(ir_consultar_def_usos(&codigo[4], &info) == 0);
    assert(info.control && !info.barrera);
    assert(ir_consultar_def_usos(&codigo[2], &info) == 0);
    assert(info.cantidad_usos == 1 && !strcmp(info.usos[0], "x"));
    RoResumen r = {0};
    RoOpciones opciones = {.latencia = 3};
    assert(ro_reordenar(&ir, &opciones, &r) == 0);
    assert(r.raw == 2 && r.efecto == 0 && r.war == 0 && r.waw == 0);
    ro_resumen_liberar(&r);
}

int main(void)
{
    vivacidad_en_spills();
    colecciones_y_invalidos();
    eliminacion_con_efectos();
    dag_spills_memoria_llamada();
    limites_y_usos_repetidos();
    puts("OK: consumidores USE/DEF DCE + reordenamiento, spilling, memoria, llamadas y errores");
    return 0;
}
