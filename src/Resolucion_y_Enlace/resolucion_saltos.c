#include "resolucion_saltos.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define RS_TEXTO 128
#define RS_BUNDLES_IMEM 1023u /* 1024 bundles; 0x3FF0 es el vector de excepciones. */

typedef enum { RS_ETIQUETA, RS_INSTRUCCION } RsTipo;
typedef enum { RS_SIN_SALTO, RS_CONDICIONAL, RS_INCONDICIONAL } RsSalto;

typedef struct {
    RsTipo tipo;
    RsSalto salto;
    unsigned slot;           /* 0 ALU, 1 LSU, 2 BRU, 3 CRIPTO, 4 bundle nop. */
    char op[16];
    char operandos[RS_TEXTO]; /* En saltos, sin el destino. */
    char destino[RS_TEXTO];   /* Etiqueta: su nombre; salto: etiqueta destino. */
    uint32_t pc;
    int relajar;
} RsItem;

typedef struct {
    RsItem *items;
    size_t cantidad;
    FILE *diagnosticos;
    unsigned linea;
} RsPrograma;

static int fallo(RsPrograma *p, const char *mensaje, const char *detalle)
{
    if (p->diagnosticos) {
        if (p->linea)
            fprintf(p->diagnosticos, "linea %u: resolucion de saltos: %s%s%s\n",
                    p->linea, mensaje, detalle ? ": " : "", detalle ? detalle : "");
        else
            fprintf(p->diagnosticos, "resolucion de saltos: %s%s%s\n",
                    mensaje, detalle ? ": " : "", detalle ? detalle : "");
    }
    return 0;
}

static int en_lista(const char *op, const char *const *lista)
{
    for (; *lista; ++lista) if (!strcmp(op, *lista)) return 1;
    return 0;
}

/* Slot segun el contrato de la ISA; -1 si el mnemonico no existe. */
static int slot_de(const char *op)
{
    static const char *const alu[] = {"suma", "resta", "cader", "cizq", "and",
        "or", "xor", "cder", "mrq", "myq", "sumai", "restai", "cizqi", "cderi",
        "caderi", "xori", "andi", "ori", "mov", "not", NULL};
    static const char *const lsu[] = {"cargai", "cargabai", "guardap",
        "guardab", NULL};
    static const char *const bru[] = {"igualsi", "igualno", "menora",
        "mayoroigual", "sye", "syer", "ret", NULL};
    static const char *const cripto[] = {"fsl", "fsli", "ell", "vcr", "camcon",
        "camcom", "setpwd", NULL};
    if (en_lista(op, alu)) return 0;
    if (en_lista(op, lsu)) return 1;
    if (en_lista(op, bru)) return 2;
    if (en_lista(op, cripto)) return 3;
    if (!strcmp(op, "nop")) return 4;
    return -1;
}

static int es_condicional(const char *op)
{
    return !strcmp(op, "igualsi") || !strcmp(op, "igualno") ||
           !strcmp(op, "menora") || !strcmp(op, "mayoroigual");
}

static const char *invertir(const char *op)
{
    if (!strcmp(op, "igualsi")) return "igualno";
    if (!strcmp(op, "igualno")) return "igualsi";
    if (!strcmp(op, "menora")) return "mayoroigual";
    return "menora";
}

static int es_numero(const char *s)
{
    if (*s == '-' || *s == '+') ++s;
    return isdigit((unsigned char)*s);
}

static int es_nombre(const char *s)
{
    if (!*s || isdigit((unsigned char)*s)) return 0;
    for (; *s; ++s)
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '.') return 0;
    return 1;
}

static int agregar(RsPrograma *p, size_t *capacidad, const RsItem *item)
{
    if (p->cantidad == *capacidad) {
        size_t nueva = *capacidad ? *capacidad * 2 : 64;
        RsItem *items = realloc(p->items, nueva * sizeof(*items));
        if (!items) return fallo(p, "sin memoria", NULL);
        p->items = items;
        *capacidad = nueva;
    }
    p->items[p->cantidad++] = *item;
    return 1;
}

/* Copia sin espacios alrededor de las comas: "x4, x0, L" -> "x4,x0,L". */
static int normalizar(const char *origen, char *destino, size_t capacidad)
{
    size_t n = 0;
    for (const char *s = origen; *s; ++s) {
        if (isspace((unsigned char)*s)) {
            const char *sig = s;
            while (isspace((unsigned char)*sig)) ++sig;
            if (!*sig || *sig == ',' ||
                (n && (destino[n - 1] == ',' || destino[n - 1] == ' '))) continue;
        }
        if (n + 1 >= capacidad) return 0;
        destino[n++] = isspace((unsigned char)*s) ? ' ' : *s;
    }
    destino[n] = '\0';
    return 1;
}

static int leer_linea(RsPrograma *p, size_t *capacidad, char *linea)
{
    char *s = linea;
    while (isspace((unsigned char)*s)) ++s;
    char *fin = s + strlen(s);
    while (fin > s && isspace((unsigned char)fin[-1])) *--fin = '\0';
    if (!*s || *s == '#' || *s == ';' || (s[0] == '/' && s[1] == '/')) return 1;

    RsItem item = {0};
    if (fin[-1] == ':') {
        fin[-1] = '\0';
        if (!es_nombre(s)) return fallo(p, "etiqueta invalida", s);
        if (strlen(s) >= sizeof(item.destino)) return fallo(p, "etiqueta muy larga", s);
        item.tipo = RS_ETIQUETA;
        strcpy(item.destino, s);
        return agregar(p, capacidad, &item);
    }

    size_t largo_op = strcspn(s, " \t");
    if (largo_op >= sizeof(item.op)) return fallo(p, "instruccion desconocida", s);
    memcpy(item.op, s, largo_op);
    item.op[largo_op] = '\0';
    int slot = slot_de(item.op);
    if (slot < 0) return fallo(p, "instruccion desconocida", item.op);
    item.tipo = RS_INSTRUCCION;
    item.slot = (unsigned)slot;
    if (!normalizar(s + largo_op, item.operandos, sizeof(item.operandos)))
        return fallo(p, "operandos muy largos", s);

    int condicional = es_condicional(item.op);
    if (condicional || !strcmp(item.op, "sye")) {
        char *coma = strrchr(item.operandos, ',');
        size_t comas = 0;
        for (const char *c = item.operandos; *c; ++c) comas += *c == ',';
        if (!coma || comas != (condicional ? 2u : 1u))
            return fallo(p, "cantidad de operandos invalida", s);
        const char *objetivo = coma + 1;
        if (!es_numero(objetivo)) {
            if (!es_nombre(objetivo)) return fallo(p, "destino invalido", objetivo);
            strcpy(item.destino, objetivo);
            *coma = '\0';
            item.salto = condicional ? RS_CONDICIONAL : RS_INCONDICIONAL;
        }
    }
    return agregar(p, capacidad, &item);
}

/* Asigna PC a cada elemento. Devuelve la cantidad de bundles. */
static size_t ubicar(RsPrograma *p, uint32_t inicio)
{
    uint32_t pc = inicio;
    int etiqueta_final = 0;
    for (size_t i = 0; i < p->cantidad; ++i) {
        RsItem *it = &p->items[i];
        it->pc = pc;
        if (it->tipo == RS_INSTRUCCION) {
            pc += 3 * 16; /* instruccion + dos nop */
            etiqueta_final = 0;
        } else etiqueta_final = 1;
    }
    return (pc - inicio) / 16 + (size_t)etiqueta_final;
}

/* Inserta, por cada condicional marcado, el salto invertido corto y el sye. */
static int expandir(RsPrograma *p, size_t marcados, unsigned *siguiente)
{
    size_t total = p->cantidad + 2 * marcados;
    RsItem *nuevos = malloc(total * sizeof(*nuevos));
    if (!nuevos) return fallo(p, "sin memoria", NULL);
    size_t n = 0;
    for (size_t i = 0; i < p->cantidad; ++i) {
        RsItem it = p->items[i];
        if (!it.relajar) { nuevos[n++] = it; continue; }
        RsItem cerca = {0};
        cerca.tipo = RS_ETIQUETA;
        snprintf(cerca.destino, sizeof(cerca.destino), "__rs_cerca_%u", (*siguiente)++);

        RsItem corto = it;
        corto.relajar = 0;
        strcpy(corto.op, invertir(it.op));
        strcpy(corto.destino, cerca.destino);

        RsItem largo = {0};
        largo.tipo = RS_INSTRUCCION;
        largo.salto = RS_INCONDICIONAL;
        largo.slot = 2;
        strcpy(largo.op, "sye");
        strcpy(largo.operandos, " x0");
        strcpy(largo.destino, it.destino);

        nuevos[n++] = corto;
        nuevos[n++] = largo;
        nuevos[n++] = cerca;
    }
    free(p->items);
    p->items = nuevos;
    p->cantidad = n;
    return 1;
}

static int emitir(const RsPrograma *p, const RrTabla *tabla, FILE *f,
                  size_t bundles)
{
    size_t emitidos = 0;
    for (size_t i = 0; i < p->cantidad; ++i) {
        const RsItem *it = &p->items[i];
        if (it->tipo == RS_ETIQUETA) {
            if (fprintf(f, "# %s:\n", it->destino) < 0) return 0;
            continue;
        }
        char texto[2 * RS_TEXTO + 32];
        if (it->salto != RS_SIN_SALTO) {
            int32_t d = 0;
            if (rr_desplazamiento(tabla, it->destino, RR_ETIQUETA, it->pc,
                                  it->salto == RS_CONDICIONAL ? RR_CONDICIONAL : RR_SYE,
                                  &d) != RR_OK) return 0;
            snprintf(texto, sizeof(texto), "%s%s,%ld", it->op, it->operandos, (long)d);
        } else snprintf(texto, sizeof(texto), "%s%s", it->op, it->operandos);
        if (it->slot == 4) {
            if (fputs("nop\n", f) < 0) return 0;
        } else {
            for (unsigned s = 0; s < 4; ++s)
                if (fprintf(f, "%s%s", s ? " | " : "", s == it->slot ? texto : "nop") < 0)
                    return 0;
            if (it->salto != RS_SIN_SALTO && fprintf(f, "  # -> %s", it->destino) < 0)
                return 0;
            if (fputc('\n', f) == EOF) return 0;
        }
        if (fputs("nop\nnop\n", f) < 0) return 0;
        emitidos += 3;
    }
    if (emitidos < bundles && fputs("nop\n", f) < 0) return 0;
    return 1;
}

static int copiar(FILE *origen, FILE *destino)
{
    if (fflush(origen) != 0 || fseek(origen, 0, SEEK_SET) != 0) return 0;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), origen)) != 0)
        if (fwrite(buf, 1, n, destino) != n) return 0;
    return !ferror(origen) && fflush(destino) == 0;
}

int rs_resolver(const char *lineal, uint32_t inicio, FILE *salida,
                FILE *diagnosticos, RsResumen *resumen)
{
    return rs_resolver_etiquetas(lineal, inicio, salida, diagnosticos, resumen, NULL);
}

int rs_resolver_etiquetas(const char *lineal, uint32_t inicio, FILE *salida,
                          FILE *diagnosticos, RsResumen *resumen,
                          RrTabla *etiquetas)
{
    RsPrograma p = {NULL, 0, diagnosticos, 0};
    if (!lineal || !salida || inicio % 16 || inicio / 16 >= RS_BUNDLES_IMEM) {
        fallo(&p, "argumentos invalidos", NULL);
        return 1;
    }
    size_t largo = strlen(lineal);
    char *copia = malloc(largo + 1);
    if (!copia) { fallo(&p, "sin memoria", NULL); return 1; }
    memcpy(copia, lineal, largo + 1);

    int ok = 1;
    size_t capacidad = 0;
    for (char *linea = copia; ok && linea;) {
        char *fin = strchr(linea, '\n');
        if (fin) *fin = '\0';
        ++p.linea;
        ok = leer_linea(&p, &capacidad, linea);
        linea = fin ? fin + 1 : NULL;
    }
    free(copia);
    p.linea = 0;

    RrTabla tabla;
    rr_inicializar(&tabla);
    size_t bundles = 0, saltos = 0, relajados = 0;
    unsigned siguiente = 0;
    /* Cada vuelta solo agrega codigo, asi que los saltos solo se alargan:
     * cada condicional se relaja a lo sumo una vez y el ciclo termina. */
    while (ok) {
        bundles = ubicar(&p, inicio);
        if (bundles > RS_BUNDLES_IMEM - inicio / 16) {
            ok = fallo(&p, "el programa no cabe en la memoria de instrucciones", NULL);
            break;
        }
        rr_liberar(&tabla);
        rr_inicializar(&tabla);
        for (size_t i = 0; ok && i < p.cantidad; ++i) {
            if (p.items[i].tipo != RS_ETIQUETA) continue;
            RrEstado e = rr_definir(&tabla, p.items[i].destino, RR_ETIQUETA,
                                    p.items[i].pc);
            if (e != RR_OK) ok = fallo(&p, rr_nombre_estado(e), p.items[i].destino);
        }
        size_t marcados = 0;
        saltos = 0;
        for (size_t i = 0; ok && i < p.cantidad; ++i) {
            RsItem *it = &p.items[i];
            if (it->tipo != RS_INSTRUCCION || it->salto == RS_SIN_SALTO) continue;
            int32_t d;
            RrEstado e = rr_desplazamiento(&tabla, it->destino, RR_ETIQUETA, it->pc,
                                           it->salto == RS_CONDICIONAL ? RR_CONDICIONAL : RR_SYE,
                                           &d);
            if (e == RR_FUERA_DE_RANGO && it->salto == RS_CONDICIONAL) {
                it->relajar = 1;
                ++marcados;
            } else if (e != RR_OK) ok = fallo(&p, rr_nombre_estado(e), it->destino);
            ++saltos;
        }
        if (!ok || !marcados) break;
        relajados += marcados;
        ok = expandir(&p, marcados, &siguiente);
    }

    /* Antes de escribir: un error aqui tampoco deja salida parcial. */
    for (size_t i = 0; ok && etiquetas && i < p.cantidad; ++i) {
        if (p.items[i].tipo != RS_ETIQUETA) continue;
        RrEstado e = rr_definir(etiquetas, p.items[i].destino, RR_ETIQUETA,
                                p.items[i].pc);
        if (e != RR_OK) ok = fallo(&p, rr_nombre_estado(e), p.items[i].destino);
    }
    if (ok) {
        FILE *temporal = tmpfile();
        if (!temporal) ok = fallo(&p, "no se pudo crear el temporal", NULL);
        else {
            ok = emitir(&p, &tabla, temporal, bundles) && copiar(temporal, salida);
            if (!ok) fallo(&p, "error de escritura", NULL);
            fclose(temporal);
        }
    }
    if (ok && resumen) *resumen = (RsResumen){bundles, saltos, relajados};
    rr_liberar(&tabla);
    free(p.items);
    return ok ? 0 : 1;
}
