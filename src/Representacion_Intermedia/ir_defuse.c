#include "ir_defuse.h"

#include <string.h>

/* El frontend distingue temporales y registros por prefijos reservados.
 * Un literal caracter BAL tiene la forma $c$ y NO es un registro. */
static bool prefijo_numerado(const char *s, const char *prefijo)
{
    size_t n = strlen(prefijo);
    if (!s || strncmp(s, prefijo, n) != 0 || !s[n]) return false;
    for (s += n; *s; ++s)
        if (*s < '0' || *s > '9') return false;
    return true;
}

static bool numero_decimal(const char *s)
{
    if (!s || !*s) return false;
    if (*s == '-' || *s == '+') ++s;
    if (!*s) return false;
    for (; *s; ++s) if (*s < '0' || *s > '9') return false;
    return true;
}

bool ir_es_nombre_valor(const char *s)
{
    if (!s || !*s || numero_decimal(s)) return false;
    if (!strcmp(s, "declare_true") || !strcmp(s, "declare_false")) return false;
    if (s[0] == '"' || s[0] == '\'') return false;
    if (s[0] == '$') {
        /* $L* son etiquetas, $spill* es una ubicacion de memoria. */
        return prefijo_numerado(s, "$t") || prefijo_numerado(s, "$r");
    }
    const unsigned char *p = (const unsigned char *)s;
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          *p == '_' || *p >= 128)) return false;
    for (++p; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p >= 128)) return false;
    return true;
}

static bool texto(const char *s) { return s && *s; }

static bool definir(IrDefUse *r, const char *s)
{
    if (!ir_es_nombre_valor(s) || r->cantidad_definiciones >= 1) return false;
    r->definiciones[r->cantidad_definiciones++] = s;
    return true;
}

static bool leer(IrDefUse *r, const char *s)
{
    if (!texto(s)) return false;
    if (!ir_es_nombre_valor(s)) return true; /* inmediato, no dependencia */
    for (size_t i = 0; i < r->cantidad_usos; ++i)
        if (!strcmp(r->usos[i], s)) return true; /* conjunto, sin duplicados */
    if (r->cantidad_usos >= 3) return false;
    r->usos[r->cantidad_usos++] = s;
    return true;
}

int ir_consultar_def_usos(const IrInstr *x, IrDefUse *out)
{
    if (!out) return 1;
    *out = (IrDefUse){0};
    if (!x) return 1;
    IrDefUse r = {0};
    bool valido = false;
    switch (x->op) {
    case IR_FUNC: case IR_END_FUNC:
        /* Los limites de funcion ya son fijos por CFG/control; no son barreras
         * de efectos que agreguen aristas artificiales al DAG. */
        valido = texto(x->dst); r.control = true;
        break;
    case IR_PARAM_DECL:
        valido = definir(&r, x->dst);
        break;
    case IR_COPY: case IR_UNARY:
        valido = definir(&r, x->dst) && leer(&r, x->a);
        break;
    case IR_BIN: case IR_INDEX_REF:
        valido = definir(&r, x->dst) && leer(&r, x->a) && leer(&r, x->b);
        break;
    case IR_LABEL: case IR_JUMP:
        valido = texto(x->dst); r.control = true;
        break;
    case IR_JFALSE: case IR_JTRUE:
        valido = texto(x->dst) && leer(&r, x->a); r.control = true;
        break;
    case IR_ARG:
        valido = leer(&r, x->a); r.barrera = true;
        break;
    case IR_CALL:
        /* 'a' es funcion y 'b' es cantidad de argumentos, no valores USE.
         * Los parametros se leen en IR_ARG; el callee puede tocar globales. */
        valido = definir(&r, x->dst) && texto(x->a) && texto(x->b);
        r.lee_memoria = r.escribe_memoria = true;
        r.barrera = r.control = true;
        break;
    case IR_RETURN:
        valido = !x->a || leer(&r, x->a); r.control = true;
        break;
    case IR_ARRAY_DECL:
        valido = definir(&r, x->dst) && leer(&r, x->a);
        r.escribe_memoria = true;
        break;
    case IR_ARRAY_DIM:
        /* dst es el arreglo al que se modifica su metadata, NO una def.
         * a es el indice de dimension y b su longitud (ambos evaluados). */
        valido = leer(&r, x->dst) && leer(&r, x->a) && leer(&r, x->b);
        r.lee_memoria = r.escribe_memoria = true;
        break;
    case IR_LOAD: case IR_SIZE:
        valido = definir(&r, x->dst) && leer(&r, x->a);
        r.lee_memoria = true;
        break;
    case IR_STORE:
        valido = leer(&r, x->dst) && leer(&r, x->a);
        r.escribe_memoria = true;
        break;
    case IR_LIST_DECL:
        valido = definir(&r, x->dst); r.escribe_memoria = true;
        break;
    case IR_LIST_ADD: case IR_LIST_REMOVE:
        valido = leer(&r, x->dst) && leer(&r, x->a);
        r.lee_memoria = r.escribe_memoria = true;
        break;
    case IR_SPILL_LOAD:
        valido = prefijo_numerado(x->dst, "$r") &&
                 prefijo_numerado(x->a, "$spill") && definir(&r, x->dst);
        r.lee_memoria = true; r.objeto_memoria = x->a;
        break;
    case IR_SPILL_STORE:
        valido = prefijo_numerado(x->dst, "$spill") &&
                 prefijo_numerado(x->a, "$r") && leer(&r, x->a);
        r.escribe_memoria = true; r.objeto_memoria = x->dst;
        break;
    default:
        return 1;
    }
    if (!valido) return 1;
    *out = r;
    return 0;
}
