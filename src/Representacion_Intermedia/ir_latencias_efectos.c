#include "ir_latencias_efectos.h"
#include "vliw_scheduler.h" /* Fuente unica de la separacion fisica N+3. */

#include <string.h>

/* Solo nombres BAL pueden pertenecer a la tabla de simbolos. Los prefijos
 * reservados son valores internos, no ambitos BAL. */
static bool nombre_bal(const char *nombre)
{
    return nombre && nombre[0] != '$';
}

static int clasificar(const char *nombre, const IrContextoEfectos *contexto,
                     bool lectura, IrEfectos *r)
{
    if (!nombre_bal(nombre)) return 0;
    IrAmbitoNombre ambito = IR_AMBITO_DESCONOCIDO;
    if (contexto && contexto->resolver)
        ambito = contexto->resolver(nombre, contexto->contexto);
    if (ambito != IR_AMBITO_LOCAL && ambito != IR_AMBITO_GLOBAL &&
        ambito != IR_AMBITO_DESCONOCIDO) return 1;
    if (ambito == IR_AMBITO_LOCAL) return 0;
    r->ambito_incierto |= ambito == IR_AMBITO_DESCONOCIDO;
    if (lectura) {
        r->puede_leer_global = true;
        r->lee_memoria = true;
    } else {
        r->puede_escribir_global = true;
        r->escribe_memoria = true;
    }
    return 0;
}

int ir_efectos_consultar(const IrInstr *inst,
    const IrContextoEfectos *contexto, IrEfectos *resultado)
{
    if (!resultado) return 1;
    *resultado = (IrEfectos){0};
    IrEfectos r = {0};
    if (ir_consultar_def_usos(inst, &r.defuse)) return 1;
    r.lee_memoria = r.defuse.lee_memoria;
    r.escribe_memoria = r.defuse.escribe_memoria;
    r.barrera = r.defuse.barrera;
    r.control = r.defuse.control;

    /* Los parametros son definiciones de alcance local de la funcion. */
    if (inst->op != IR_PARAM_DECL) {
        for (size_t i = 0; i < r.defuse.cantidad_usos; ++i)
            if (clasificar(r.defuse.usos[i], contexto, true, &r)) return 1;
        for (size_t i = 0; i < r.defuse.cantidad_definiciones; ++i)
            if (clasificar(r.defuse.definiciones[i], contexto, false, &r)) return 1;
    }
    if (inst->op == IR_CALL) {
        /* CALL puede modificar globales que no figuran en USE/DEF explicitos. */
        r.puede_leer_global = r.puede_escribir_global = true;
        r.ambito_incierto = true;
    }
    *resultado = r;
    return 0;
}

static bool coinciden(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static bool interseccion(const char *const *a, size_t na,
                         const char *const *b, size_t nb)
{
    for (size_t i = 0; i < na; ++i)
        for (size_t j = 0; j < nb; ++j)
            if (coinciden(a[i], b[j])) return true;
    return false;
}

int ir_dependencia_consultar(const IrInstr *anterior, const IrInstr *posterior,
    const IrContextoEfectos *contexto, IrDependenciaSimbolica *resultado)
{
    if (!resultado) return 1;
    *resultado = (IrDependenciaSimbolica){0};
    IrEfectos a, b;
    if (ir_efectos_consultar(anterior, contexto, &a) ||
        ir_efectos_consultar(posterior, contexto, &b)) return 1;
    IrDependenciaSimbolica r = {0};
    const IrDefUse *x = &a.defuse, *y = &b.defuse;
    if (interseccion(x->definiciones, x->cantidad_definiciones,
                      y->usos, y->cantidad_usos)) {
        r.tipos |= IR_DEP_RAW;
        r.distancia_min_bundles = VLIW_RAW_DISTANCIA;
    }
    if (interseccion(x->usos, x->cantidad_usos,
                      y->definiciones, y->cantidad_definiciones))
        r.tipos |= IR_DEP_WAR;
    if (interseccion(x->definiciones, x->cantidad_definiciones,
                      y->definiciones, y->cantidad_definiciones))
        r.tipos |= IR_DEP_WAW;

    /* Sin analisis de alias se preserva incluso el orden load/load. Una
     * ranura $spillN no demuestra que otro acceso no se le superponga. */
    if ((a.lee_memoria || a.escribe_memoria) &&
        (b.lee_memoria || b.escribe_memoria))
        r.tipos |= IR_DEP_MEMORIA;
    if (a.barrera || b.barrera) {
        r.tipos |= IR_DEP_BARRERA;
        /* IR_ARG es una anotacion de parametros, no una instruccion ISA. */
        if (anterior->op == IR_CALL || posterior->op == IR_CALL) {
            if (r.distancia_min_bundles < VLIW_RAW_DISTANCIA)
                r.distancia_min_bundles = VLIW_RAW_DISTANCIA;
        }
    }
    if (a.control || b.control) r.tipos |= IR_DEP_CONTROL;
    if (r.tipos && !r.distancia_min_bundles) r.distancia_min_bundles = 1;
    *resultado = r;
    return 0;
}
