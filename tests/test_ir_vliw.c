/* Regresiones reales: IR -> register spilling -> CB/S2 -> CFG/DAG/S7/S8.
 * Este oraculo no confunde un fragmento VLIW con un BAL ejecutable. */
#include "ir_vliw.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    assert(p);
    memcpy(p, s, len);
    return p;
}
static void instr(IrPrograma *p, IrOp op, const char *dst, const char *a,
                  const char *b, const char *operador) {
    assert(p->cantidad < 128);
    if (p->cantidad == p->capacidad) {
        size_t n = p->capacidad ? p->capacidad * 2 : 32;
        IrInstr *v = realloc(p->codigo, n * sizeof *v);
        assert(v);
        p->codigo = v;
        p->capacidad = n;
    }
    p->codigo[p->cantidad++] = (IrInstr){op,dup(dst),dup(a),dup(b),dup(operador),p->cantidad+1};
}
static void base_presion(IrPrograma *p) {
    instr(p,IR_FUNC,"main",NULL,NULL,NULL);
    instr(p,IR_BIN,"$t1","1","2","gauss");
    instr(p,IR_BIN,"$t2","3","4","gauss");
    instr(p,IR_BIN,"$t3","5","6","gauss");
    instr(p,IR_BIN,"$t4","7","8","gauss");
    instr(p,IR_BIN,"$t5","$t1","$t4","gauss");
    instr(p,IR_BIN,"$t6","$t2","$t3","neumann");
    instr(p,IR_BIN,"$t7","$t5","$t6","gauss");
    instr(p,IR_END_FUNC,"main",NULL,NULL,NULL);
    assert(ir_bloques_basicos(p) == 0);
}
static void test_cadena_real(void) {
    IrPrograma p={0};base_presion(&p);
    IrVliwConfiguracion conf={3,16}; IrVliwResultado r={0};
    assert(ir_vliw_desde_ir(&p,&conf,&r,stderr)==IR_VLIW_OK);
    assert(r.metricas.ranuras==7 && r.metricas.desalojos>0);
    assert(r.metricas.cargas>0 && r.metricas.almacenes>0);
    assert(r.backend.marco.bytes_marco==44);
    assert(r.cadena && r.cadena->plan && r.cadena->codificado);
    assert(r.cadena->codigo.cantidad==r.cadena->plan->cantidad_bundles);
    assert(r.cadena->estadisticas.instrucciones_utiles>r.despues_spilling.cantidad);
    assert(strstr(r.cadena->asm_texto,"guardap x2,x1,0"));
    assert(strstr(r.cadena->asm_texto,"restai x2,x2,44"));
    assert(strstr(r.cadena->asm_texto,"sumai x2,x2,44"));
    assert(strstr(r.cadena->asm_texto,"cargai x"));
    assert(strstr(r.cadena->asm_texto,"guardap x2,x"));
    assert(vliw_verificar(&r.cadena->proyeccion->entrada,r.cadena->plan,NULL)==VLIW_OK);
    assert(r.cadena->dag->cantidad_dependencias>0);
    for (size_t i=0; i<r.cadena->codigo.cantidad; ++i)
        for (unsigned j=0;j<4;++j) {
            const uint32_t pal=r.cadena->codigo.bundles[i].slots[j];
            SimInstruccion dec;
            assert(sim_decode_palabra(pal,j,&dec)==(pal?SIM_DECODE_OK:SIM_DECODE_NOP));
        }
    printf("OK cadena IR->spill->VLIW: spills=%zu loads=%zu stores=%zu bundles=%zu deps=%zu\n",
           r.metricas.ranuras,r.metricas.cargas,r.metricas.almacenes,
           r.cadena->codigo.cantidad,r.cadena->dag->cantidad_dependencias);
    ir_vliw_liberar(&r);
    assert(!r.cadena && !r.despues_spilling.codigo);
    ir_liberar(&p);
}
static void test_saltos_y_cfg_fisico(void) {
    IrPrograma p={0};IrVliwResultado r={0};IrVliwConfiguracion cfg={3,16};
    instr(&p,IR_FUNC,"main",NULL,NULL,NULL);
    instr(&p,IR_BIN,"$t1","1","2","gauss");
    instr(&p,IR_JFALSE,"$L1","$t1",NULL,NULL);
    instr(&p,IR_BIN,"$t2","5","6","gauss");
    instr(&p,IR_JUMP,"$L2",NULL,NULL,NULL);
    instr(&p,IR_LABEL,"$L1",NULL,NULL,NULL);
    instr(&p,IR_BIN,"$t3","7","8","neumann");
    instr(&p,IR_LABEL,"$L2",NULL,NULL,NULL);
    instr(&p,IR_END_FUNC,"main",NULL,NULL,NULL);
    assert(ir_bloques_basicos(&p)==0);
    IrVliwEstado rc=ir_vliw_desde_ir(&p,&cfg,&r,stderr);
    if (rc) fprintf(stderr,"error branch cadena: %d\n",rc);
    assert(rc==IR_VLIW_OK);
    assert(r.cadena->cfg->cantidad_destinos==2);
    assert(r.cadena->cfg->cantidad_bloques>=3);
    assert(strstr(r.cadena->asm_texto,"igualsi x"));
    assert(strstr(r.cadena->asm_texto,"sye x0"));
    assert(vliw_verificar(&r.cadena->proyeccion->entrada,r.cadena->plan,NULL)==VLIW_OK);
    printf("OK control: bloques=%zu transferencias=%zu bundles=%zu\n",
           r.cadena->cfg->cantidad_bloques,r.cadena->cfg->cantidad_destinos,
           r.cadena->codigo.cantidad);
    ir_vliw_liberar(&r);ir_liberar(&p);
}
static void test_backedge_y_inmediato(void) {
    IrPrograma p={0}; IrVliwResultado r={0}; IrVliwConfiguracion cfg={3,16};
    instr(&p,IR_FUNC,"main",NULL,NULL,NULL);
    instr(&p,IR_LABEL,"$Ltop",NULL,NULL,NULL);
    instr(&p,IR_BIN,"$t1","10","2","neumann");
    instr(&p,IR_JTRUE,"$Ltop","1",NULL,NULL);
    instr(&p,IR_END_FUNC,"main",NULL,NULL,NULL);
    assert(ir_bloques_basicos(&p)==0);
    IrVliwEstado rc=ir_vliw_desde_ir(&p,&cfg,&r,stderr);
    if (rc) fprintf(stderr,"error backedge: %d\n",rc);
    assert(rc==IR_VLIW_OK);
    assert(r.cadena->cfg->cantidad_destinos==1);
    assert(strstr(r.cadena->asm_texto,"igualno x"));
    ir_vliw_liberar(&r);ir_liberar(&p);
}
/* Este escenario atraviesa el generador IR de AST real, no una IR escrita
 * por el fixture, con un whether semantico de valor booleano. */
static void test_desde_ast(void) {
    Ast arbol={0}; IrPrograma ir={0}; IrVliwResultado res={0};
    IrVliwConfiguracion cfg={3,16};
    AstUbicacion loc={1,1,1,1};
    AstNodo *prog=ast_crear(&arbol,AST_PROGRAMA,NULL,loc); assert(prog); arbol.raiz=prog;
    AstNodo *fn=ast_crear(&arbol,AST_FUNCION,NULL,loc);assert(fn);ast_agregar_hijo(prog,fn);
    AstNodo *id=ast_crear(&arbol,AST_IDENTIFICADOR,"main",loc);
    AstNodo *tipo=ast_crear(&arbol,AST_TIPO,"declare_infinite_void",loc);
    AstNodo *params=ast_crear(&arbol,AST_PARAMETROS,NULL,loc);
    AstNodo *body=ast_crear(&arbol,AST_BLOQUE,NULL,loc);
    assert(id&&tipo&&params&&body);
    ast_agregar_hijo(fn,id);ast_agregar_hijo(fn,tipo);
    ast_agregar_hijo(fn,params);ast_agregar_hijo(fn,body);
    AstNodo *si=ast_crear(&arbol,AST_SI,"whether",loc);
    AstNodo *cond=ast_crear(&arbol,AST_BOOLEANO,"declare_true",loc);
    AstNodo *rama=ast_crear(&arbol,AST_BLOQUE,NULL,loc);
    assert(si&&cond&&rama);
    ast_agregar_hijo(body,si);ast_agregar_hijo(si,cond);ast_agregar_hijo(si,rama);
    assert(ir_desde_ast(&arbol,&ir,stderr)==0);
    assert(ir_vliw_desde_ir(&ir,&cfg,&res,stderr)==IR_VLIW_OK);
    assert(res.cadena->codificado && res.cadena->cfg->cantidad_destinos==2);
    assert(strstr(res.cadena->asm_texto,"igualsi x"));
    ir_vliw_liberar(&res);ir_liberar(&ir);ast_liberar(&arbol);
    puts("OK AST real -> IR -> VLIW fisico");
}
static void test_rechazos_sin_binario(void) {
    IrPrograma p={0};IrVliwResultado r={0};IrVliwConfiguracion cfg={3,16};
    instr(&p,IR_FUNC,"main",NULL,NULL,NULL);
    instr(&p,IR_CALL,"$t1","externa","0",NULL);
    instr(&p,IR_END_FUNC,"main",NULL,NULL,NULL);
    assert(ir_bloques_basicos(&p)==0);
    assert(ir_vliw_desde_ir(&p,&cfg,&r,NULL)==IR_VLIW_NO_SOPORTADO);
    assert(!r.cadena && !r.despues_spilling.codigo);
    ir_liberar(&p);
    base_presion(&p);cfg.bytes_ocupados=1024;
    assert(ir_vliw_desde_ir(&p,&cfg,&r,NULL)!=IR_VLIW_OK);
    assert(!r.cadena && !r.despues_spilling.codigo);
    cfg=(IrVliwConfiguracion){2,16};
    assert(ir_vliw_desde_ir(&p,&cfg,&r,NULL)==IR_VLIW_ARGUMENTO);
    ir_liberar(&p);
}
static void test_campo_fuera_de_isa(void) {
    IrPrograma p={0};IrVliwResultado r={0};IrVliwConfiguracion cfg={3,16};
    instr(&p,IR_FUNC,"main",NULL,NULL,NULL);
    instr(&p,IR_BIN,"$t1","1200","5","gauss");
    instr(&p,IR_END_FUNC,"main",NULL,NULL,NULL);
    assert(ir_bloques_basicos(&p)==0);
    assert(ir_vliw_desde_ir(&p,&cfg,&r,NULL)==IR_VLIW_NO_SOPORTADO);
    assert(!r.cadena && !r.despues_spilling.codigo);
    ir_liberar(&p);
}
int main(void) {
    test_cadena_real();test_saltos_y_cfg_fisico();test_backedge_y_inmediato();test_desde_ast();test_rechazos_sin_binario();test_campo_fuera_de_isa();
    puts("OK: puente de IR/spilling hacia consumidor VLIW fisico (subconjunto)");
    return 0;
}
