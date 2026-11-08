#include "formato_binario.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void guardar32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t leer32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Convertir por resta evita depender de casts fuera del rango de int32_t. */
static int64_t firmado(uint32_t v)
{
    return v <= INT32_MAX ? (int64_t)v : (int64_t)v - INT64_C(4294967296);
}

static const SimSimbolo *buscar(const SimTabla *t, const char *nombre)
{
    for (const SimSimbolo *s = t->primero; s; s = s->siguiente_orden)
        if (s->ambito == t->global && !strcmp(s->nombre, nombre)) return s;
    return NULL;
}

/* Se evalua en el mismo orden que las escrituras del arranque. */
static int evaluar(const AstNodo *n, const SimTabla *t, const FbDatos *d,
                   const uint8_t *listos, unsigned nivel, uint32_t *v)
{
    if (!n || nivel >= 256) return 0;
    if (n->tipo == AST_ENTERO && n->texto) {
        char *fin;
        errno = 0;
        unsigned long long x = strtoull(n->texto, &fin, 10);
        if (errno || fin == n->texto || *fin || x > UINT32_MAX) return 0;
        *v = (uint32_t)x;
        return 1;
    }
    if (n->tipo == AST_IDENTIFICADOR && n->texto) {
        const SimSimbolo *s = buscar(t, n->texto);
        if (!s || s->almacen != SIM_ALMACEN_GLOBAL || s->bytes != 4 ||
            s->desplazamiento < 0 || (size_t)s->desplazamiento + 4 > d->cantidad ||
            !listos[s->desplazamiento]) return 0;
        *v = leer32(d->bytes + s->desplazamiento);
        return 1;
    }
    if (n->tipo == AST_UNARIO && n->texto && !strcmp(n->texto, "neumann")) {
        if (!evaluar(n->primer_hijo, t, d, listos, nivel + 1, v)) return 0;
        *v = 0u - *v;
        return 1;
    }
    if (n->tipo != AST_BINARIO || !n->texto || !n->primer_hijo) return 0;
    uint32_t a, b;
    if (!evaluar(n->primer_hijo, t, d, listos, nivel + 1, &a) ||
        !evaluar(n->primer_hijo->siguiente, t, d, listos, nivel + 1, &b)) return 0;
    const char *op = n->texto;
    if (!strcmp(op, "gauss")) *v = a + b;
    else if (!strcmp(op, "neumann")) *v = a - b;
    else if (!strcmp(op, "pitagoras")) *v = a * b;
    else if (!strcmp(op, "euclides") || !strcmp(op, "euler")) {
        if (!b) return 0;
        int64_t x = firmado(a), y = firmado(b);
        *v = (uint32_t)(!strcmp(op, "euclides") ? x / y : x % y);
    } else if (!strcmp(op, "descartes")) {
        if (firmado(b) < 0) return 0;
        *v = 1;
        while (b) {
            if (b & 1u) *v *= a;
            a *= a;
            b >>= 1;
        }
    } else return 0;
    return 1;
}

int fb_preparar_datos(const Ast *ast, const SimTabla *tabla, uint32_t base,
                     size_t cantidad, FbDatos *datos, FILE *diagnosticos)
{
    if (!ast || !ast->raiz || !tabla || !datos || cantidad > FB_MAX_DATOS ||
        base > 65536u || cantidad > 65536u - base) return 1;
    memset(datos, 0, sizeof(*datos));
    datos->base = base;
    datos->cantidad = cantidad;
    datos->flags = 1;
    uint8_t listos[FB_MAX_DATOS] = {0};
    for (const AstNodo *n = ast->raiz->primer_hijo; n; n = n->siguiente) {
        if (n->tipo != AST_CONSTANTE) continue;
        const AstNodo *id = n->primer_hijo;
        const AstNodo *valor = id && id->siguiente ? id->siguiente->siguiente : NULL;
        const SimSimbolo *s = id && id->texto ? buscar(tabla, id->texto) : NULL;
        uint32_t v;
        if (!s || s->clase != SIM_CONSTANTE || s->tipo != SIM_TIPO_INT ||
            s->dimensiones || s->almacen != SIM_ALMACEN_GLOBAL || s->bytes != 4 ||
            s->desplazamiento < 0 || (size_t)s->desplazamiento + 4 > cantidad ||
            !evaluar(valor, tabla, datos, listos, 0, &v)) {
            if (diagnosticos) fprintf(diagnosticos,
                "datos del binario: no se puede evaluar la constante global '%s'\n",
                id && id->texto ? id->texto : "?");
            return 1;
        }
        guardar32(datos->bytes + s->desplazamiento, v);
        listos[s->desplazamiento] = 1;
    }
    return 0;
}

int fb_escribir(const CbPrograma *programa, const FbDatos *datos, FILE *salida)
{
    if (!programa || !programa->bundles || !programa->cantidad ||
        programa->cantidad > 1024 || !datos || !salida ||
        datos->cantidad > FB_MAX_DATOS || datos->base > 65536u ||
        datos->cantidad > 65536u - datos->base || datos->flags > 1) return 1;
    uint32_t codigo = (uint32_t)programa->cantidad * 16;
    uint32_t campos[] = {1, FB_CABECERA_BYTES,
        FB_CABECERA_BYTES + codigo + (uint32_t)datos->cantidad,
        0, FB_CABECERA_BYTES, codigo, 0,
        FB_CABECERA_BYTES + codigo, (uint32_t)datos->cantidad,
        datos->base, datos->flags};
    uint8_t cabecera[FB_CABECERA_BYTES] = {'B', 'A', 'L', 'B'};
    for (size_t i = 0; i < sizeof(campos) / sizeof(*campos); ++i)
        guardar32(cabecera + 4 + i * 4, campos[i]);
    if (fwrite(cabecera, 1, sizeof(cabecera), salida) != sizeof(cabecera) ||
        cb_escribir_bin(programa, salida) != 0 ||
        fwrite(datos->bytes, 1, datos->cantidad, salida) != datos->cantidad) return 1;
    return ferror(salida) ? 1 : 0;
}
