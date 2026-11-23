#include "codificacion_binaria.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define CB_MAX_BUNDLES 1024u
#define CB_DISTANCIA_LECTURA 3
#define CB_MAX_ETIQUETAS 4096

#define TIPO_REGISTRO 0x6Au   /* 1101010 */
#define TIPO_INMEDIATO 0x40u  /* 1000000 */
#define TIPO_MEMORIA 0x49u    /* 1001001 */
#define TIPO_CONTROL 0x41u    /* 1000001 */
#define TIPO_SALTO 0x4Bu      /* 1001011 */
#define TIPO_CRIPTO 0x02u     /* 0000010 */

typedef struct {
    uint32_t palabra;
    uint32_t lee;        /* Mascara de registros leidos (sin x0). */
    uint32_t escribe;    /* Mascara de registros escritos (sin x0). */
    int slot;
    int tiene_destino;
    long destino;        /* PC destino de sye y condicionales. */
    int condicional;
    int indirecto;       /* syer: destino en un registro. */
} CbInstr;

typedef struct {
    char nombre[64];
    long pc;
} CbEtiqueta;

typedef struct {
    FILE *diagnosticos;
    unsigned linea;
    CbEtiqueta *etiquetas;
    size_t cantidad_etiquetas;
} CbContexto;

static int error(CbContexto *c, const char *formato, const char *detalle)
{
    if (c->diagnosticos) {
        fprintf(c->diagnosticos, "linea %u: ", c->linea);
        fprintf(c->diagnosticos, formato, detalle ? detalle : "");
        fputc('\n', c->diagnosticos);
    }
    return 0;
}

static char *recortar(char *s)
{
    while (isspace((unsigned char)*s)) ++s;
    char *fin = s + strlen(s);
    while (fin > s && isspace((unsigned char)fin[-1])) *--fin = '\0';
    return s;
}

static void minusculas(char *s)
{
    for (; *s; ++s) *s = (char)tolower((unsigned char)*s);
}

static int registro(CbContexto *c, const char *token, unsigned *r)
{
    char t[16];
    if (strlen(token) >= sizeof(t)) return error(c, "registro invalido '%s'", token);
    strcpy(t, token);
    minusculas(t);
    if (!strcmp(t, "cero") || !strcmp(t, "zero")) { *r = 0; return 1; }
    if (!strcmp(t, "ra")) { *r = 1; return 1; }
    if (!strcmp(t, "sp")) { *r = 2; return 1; }
    if (!strcmp(t, "gp")) { *r = 3; return 1; }
    char *fin;
    if ((t[0] == 't' || t[0] == 's') && isdigit((unsigned char)t[1])) {
        long n = strtol(t + 1, &fin, 10);
        long maximo = t[0] == 't' ? 11 : 15;
        if (!*fin && n <= maximo && (t[1] != '0' || !t[2])) {
            *r = (unsigned)(n + (t[0] == 't' ? 4 : 16));
            return 1;
        }
    }
    if (t[0] == 'x' && isdigit((unsigned char)t[1]) && (t[1] != '0' || !t[2])) {
        long n = strtol(t + 1, &fin, 10);
        if (!*fin && strlen(t) <= 3 && n <= 31) { *r = (unsigned)n; return 1; }
    }
    return error(c, "registro invalido '%s'; use x0-x31 o un alias ABI", token);
}

/* [+-]?(0x hex | 0b bin | dec), como integer() de assembler.py. */
static int entero(CbContexto *c, const char *token, long *valor)
{
    const char *p = token;
    int negativo = 0;
    if (*p == '+' || *p == '-') negativo = *p++ == '-';
    int base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) { base = 2; p += 2; }
    if (!*p) return error(c, "inmediato invalido '%s'; use decimal, 0x o 0b", token);
    long v = 0;
    for (; *p; ++p) {
        int d = isdigit((unsigned char)*p) ? *p - '0' :
                isxdigit((unsigned char)*p) ? tolower((unsigned char)*p) - 'a' + 10 : 99;
        if (d >= base) return error(c, "inmediato invalido '%s'; use decimal, 0x o 0b", token);
        if (v > (1L << 40)) return error(c, "inmediato fuera de rango '%s'", token);
        v = v * base + d;
    }
    *valor = negativo ? -v : v;
    return 1;
}

static int acotado(CbContexto *c, const char *token, long minimo, long maximo, long *valor)
{
    if (!entero(c, token, valor)) return 0;
    if (*valor < minimo || *valor > maximo) {
        char detalle[96];
        snprintf(detalle, sizeof(detalle), "%s (rango %ld..%ld)", token, minimo, maximo);
        return error(c, "inmediato fuera de rango: %s", detalle);
    }
    return 1;
}

static uint32_t bit(unsigned r) { return r ? 1u << r : 0u; }

static const struct { const char *nombre; unsigned id; int corrimiento; } ALU[] = {
    {"suma", 8, 0}, {"resta", 9, 0}, {"cader", 10, 0}, {"cizq", 11, 0},
    {"and", 12, 0}, {"or", 13, 0}, {"xor", 14, 0}, {"cder", 15, 0},
    {"mrq", 5, 0}, {"myq", 6, 0},
};
static const struct { const char *nombre; unsigned id; int corrimiento; } ALU_I[] = {
    {"sumai", 0, 0}, {"restai", 1, 0}, {"cizqi", 2, 1}, {"cderi", 3, 1},
    {"caderi", 4, 1}, {"xori", 5, 0}, {"andi", 6, 0}, {"ori", 7, 0},
};
static const struct { const char *nombre; unsigned id; } LSU[] = {
    {"guardap", 0}, {"guardab", 1}, {"cargai", 8}, {"cargabai", 9},
};
static const struct { const char *nombre; unsigned id; } CONTROL[] = {
    {"igualsi", 1}, {"igualno", 2}, {"menora", 4}, {"mayoroigual", 8},
};

#define CANTIDAD(a) (sizeof(a) / sizeof((a)[0]))

static int contar(CbContexto *c, const char *op, size_t n, size_t esperado)
{
    if (n == esperado) return 1;
    char detalle[64];
    snprintf(detalle, sizeof(detalle), "%s requiere %zu operandos", op, esperado);
    return error(c, "%s separados por comas", detalle);
}

static long buscar_etiqueta(const CbContexto *c, const char *nombre)
{
    for (size_t i = 0; i < c->cantidad_etiquetas; ++i)
        if (!strcmp(c->etiquetas[i].nombre, nombre)) return c->etiquetas[i].pc;
    return -1;
}

/* Operandos nombrados de cripto: "rd=6, r1=2, LK=0, RK=1". Reordena segun
 * campos y antepone x a los registros escritos como numero. */
static int nombrados(CbContexto *c, char **ops, size_t n, const char *const *campos,
                     const char *const *son_registro, char guardado[][24])
{
    int alguno = 0;
    for (size_t i = 0; i < n; ++i) alguno |= strchr(ops[i], '=') != NULL;
    if (!alguno) return 1;
    int usado[4] = {0};
    for (size_t i = 0; i < n; ++i) {
        char *igual = strchr(ops[i], '=');
        if (!igual || strchr(igual + 1, '=')) return error(c, "operandos nombrados invalidos%s", NULL);
        *igual = '\0';
        char *clave = recortar(ops[i]), *valor = recortar(igual + 1);
        minusculas(clave);
        size_t k = 0;
        while (k < n && strcmp(campos[k], clave)) ++k;
        if (k == n || usado[k]) return error(c, "operandos nombrados invalidos o repetidos%s", NULL);
        usado[k] = 1;
        int es_reg = 0;
        for (const char *const *r = son_registro; *r; ++r) es_reg |= !strcmp(*r, clave);
        int decimal = *valor != '\0';
        for (const char *v = valor; *v; ++v) decimal &= isdigit((unsigned char)*v) != 0;
        snprintf(guardado[k], 24, "%s%s", es_reg && decimal ? "x" : "", valor);
    }
    for (size_t k = 0; k < n; ++k) ops[k] = guardado[k];
    return 1;
}

static int codificar(CbContexto *c, const char *texto, long pc, CbInstr *ins)
{
    *ins = (CbInstr){0};
    char copia[160];
    if (strlen(texto) >= sizeof(copia)) return error(c, "instruccion demasiado larga%s", NULL);
    strcpy(copia, texto);
    char *s = recortar(copia);
    if (!*s) return error(c, "slot vacio; escriba nop explicitamente%s", NULL);
    char *resto = s + strcspn(s, " \t");
    if (*resto) *resto++ = '\0';
    char op[16];
    if (strlen(s) >= sizeof(op)) return error(c, "instruccion desconocida '%s'", s);
    strcpy(op, s);
    minusculas(op);
    char *ops[8];
    size_t n = 0;
    resto = recortar(resto);
    if (*resto) {
        for (char *p = resto;;) {
            char *coma = strchr(p, ',');
            if (coma) *coma = '\0';
            if (n == 8) return error(c, "demasiados operandos%s", NULL);
            ops[n] = recortar(p);
            if (!*ops[n]) return error(c, "hay un operando vacio%s", NULL);
            ++n;
            if (!coma) break;
            p = coma + 1;
        }
    }

    if (!strcmp(op, "nop")) {
        if (n) return error(c, "nop no recibe operandos%s", NULL);
        return 1;
    }
    if (!strcmp(op, "ret")) {
        if (n) return error(c, "ret no recibe operandos%s", NULL);
        return codificar(c, "syer x0, ra, 0", pc, ins);
    }
    if (!strcmp(op, "csi") || !strcmp(op, "csd"))
        return error(c, "%s no existe en el ISA", op);
    if (!strcmp(op, "camcom")) strcpy(op, "camcon");

    unsigned a, b, d;
    long k;
    for (size_t i = 0; i < CANTIDAD(LSU); ++i) {
        if (strcmp(op, LSU[i].nombre)) continue;
        if (!contar(c, op, n, 3) || !registro(c, ops[0], &a) || !registro(c, ops[1], &b) ||
            !acotado(c, ops[2], -1024, 1023, &k)) return 0;
        int carga = LSU[i].id >= 8;
        unsigned base = carga ? b : a, dato = carga ? a : b;
        ins->palabra = (TIPO_MEMORIA << 25) | (LSU[i].id << 21) | (base << 16) |
                       (dato << 11) | ((uint32_t)k & 2047u);
        ins->lee = carga ? bit(base) : bit(base) | bit(dato);
        ins->escribe = carga ? bit(dato) : 0;
        ins->slot = 1;
        return 1;
    }
    if (!strcmp(op, "syer")) {
        if (!contar(c, op, n, 3) || !registro(c, ops[0], &a) || !registro(c, ops[1], &b) ||
            !acotado(c, ops[2], -1024, 1023, &k)) return 0;
        ins->palabra = (TIPO_SALTO << 25) | (1u << 21) | (a << 16) | (b << 11) |
                       ((uint32_t)k & 2047u);
        ins->lee = bit(b);
        ins->escribe = bit(a);
        ins->slot = 2;
        ins->indirecto = 1;
        return 1;
    }
    int es_sye = !strcmp(op, "sye");
    for (size_t i = 0; es_sye || i < CANTIDAD(CONTROL); ++i) {
        if (!es_sye && strcmp(op, CONTROL[i].nombre)) continue;
        if (!contar(c, op, n, es_sye ? 2 : 3)) return 0;
        const char *objetivo = ops[n - 1];
        long etiqueta = buscar_etiqueta(c, objetivo);
        if (etiqueta >= 0) k = etiqueta - pc;
        else if (!entero(c, objetivo, &k)) return 0;
        long limite = es_sye ? 32768 : 1024;
        if (k < -limite || k >= limite) {
            char detalle[16];
            snprintf(detalle, sizeof(detalle), "%d", es_sye ? 16 : 11);
            return error(c, "salto fuera de rango de %s bits", detalle);
        }
        if (k % 16) return error(c, "el salto debe estar alineado a un bundle (16 bytes)%s", NULL);
        ins->slot = 2;
        ins->tiene_destino = 1;
        ins->destino = pc + k;
        if (es_sye) {
            if (!registro(c, ops[0], &a)) return 0;
            ins->palabra = (TIPO_SALTO << 25) | (a << 16) | ((uint32_t)k & 65535u);
            ins->escribe = bit(a);
            return 1;
        }
        if (!registro(c, ops[0], &a) || !registro(c, ops[1], &b)) return 0;
        ins->palabra = (TIPO_CONTROL << 25) | (CONTROL[i].id << 21) | (a << 16) |
                       (b << 11) | ((uint32_t)k & 2047u);
        ins->lee = bit(a) | bit(b);
        ins->condicional = 1;
        return 1;
    }

    /* Cripto */
    static const char *const fsl_campos[] = {"rd", "r1", "lk", "rk"};
    static const char *const ell_campos[] = {"lk", "off", "rs1", "rs2"};
    static const char *const vcr_campos[] = {"dir_cand"};
    static const char *const camcon_campos[] = {"dir", "imm"};
    static const char *const setpwd_campos[] = {"rs"};
    static const char *const registros_cripto[] = {"rd", "r1", "rs1", "rs2", "rs", NULL};
    static const struct { const char *nombre; unsigned id; const char *const *campos; size_t n; } CRIPTO[] = {
        {"fsl", 0, fsl_campos, 4}, {"fsli", 1, fsl_campos, 4}, {"ell", 2, ell_campos, 4},
        {"vcr", 3, vcr_campos, 1}, {"camcon", 4, camcon_campos, 2}, {"setpwd", 5, setpwd_campos, 1},
    };
    for (size_t i = 0; i < CANTIDAD(CRIPTO); ++i) {
        if (strcmp(op, CRIPTO[i].nombre)) continue;
        char guardado[4][24];
        if (!contar(c, op, n, CRIPTO[i].n) ||
            !nombrados(c, ops, n, CRIPTO[i].campos, registros_cripto, guardado)) return 0;
        uint32_t palabra = (TIPO_CRIPTO << 25) | (CRIPTO[i].id << 21);
        ins->slot = 3;
        if (CRIPTO[i].id <= 1) {
            long lk, rk;
            if (!registro(c, ops[0], &a) || !registro(c, ops[1], &b)) return 0;
            if (a < 2 || b < 2 || a % 2 || b % 2)
                return error(c, "Feistel requiere pares de registros desde x2 hasta x30%s", NULL);
            if (!acotado(c, ops[2], 0, 3, &lk) || !acotado(c, ops[3], 0, 3, &rk)) return 0;
            ins->palabra = palabra | ((uint32_t)lk << 19) | ((uint32_t)rk << 17) | (a << 12) | (b << 7);
            ins->lee = bit(b) | bit(b + 1);
            ins->escribe = bit(a) | bit(a + 1);
        } else if (CRIPTO[i].id == 2) {
            long lk, off;
            if (!acotado(c, ops[0], 0, 3, &lk) || !acotado(c, ops[1], 0, 3, &off) ||
                !registro(c, ops[2], &a) || !registro(c, ops[3], &b)) return 0;
            if ((off != 0 && off != 2) || a % 2 || b % 2)
                return error(c, "ell requiere off 0 o 2 y registros pares%s", NULL);
            ins->palabra = palabra | ((uint32_t)lk << 19) | ((uint32_t)off << 17) | (a << 12) | (b << 7);
            ins->lee = bit(a) | bit(b);
        } else if (CRIPTO[i].id == 5) {
            if (!registro(c, ops[0], &a)) return 0;
            ins->palabra = palabra | a;
            ins->lee = bit(a);
        } else {
            long dir, rot = 0;
            if (!acotado(c, ops[0], 0, 65535, &dir) ||
                (CRIPTO[i].id == 4 && !acotado(c, ops[1], 0, 31, &rot))) return 0;
            ins->palabra = palabra | ((uint32_t)dir << 5) | (uint32_t)rot;
        }
        return 1;
    }

    /* ALU y pseudoinstrucciones mov / not */
    if (!strcmp(op, "mov") || !strcmp(op, "not")) {
        if (!contar(c, op, n, 2)) return 0;
        char expandida[96];
        snprintf(expandida, sizeof(expandida), !strcmp(op, "mov") ? "suma %s, %s, x0" :
                 "xori %s, %s, -1", ops[0], ops[1]);
        return codificar(c, expandida, pc, ins);
    }
    for (size_t i = 0; i < CANTIDAD(ALU); ++i) {
        if (strcmp(op, ALU[i].nombre)) continue;
        if (!contar(c, op, n, 3) || !registro(c, ops[0], &a) || !registro(c, ops[1], &b) ||
            !registro(c, ops[2], &d)) return 0;
        ins->palabra = (TIPO_REGISTRO << 25) | (ALU[i].id << 21) | (a << 16) | (b << 11) | (d << 6);
        ins->lee = bit(b) | bit(d);
        ins->escribe = bit(a);
        return 1;
    }
    for (size_t i = 0; i < CANTIDAD(ALU_I); ++i) {
        if (strcmp(op, ALU_I[i].nombre)) continue;
        if (!contar(c, op, n, 3) || !registro(c, ops[0], &a) || !registro(c, ops[1], &b) ||
            !acotado(c, ops[2], ALU_I[i].corrimiento ? 0 : -1024,
                     ALU_I[i].corrimiento ? 2047 : 1023, &k)) return 0;
        ins->palabra = (TIPO_INMEDIATO << 25) | (ALU_I[i].id << 21) | (b << 16) | (a << 11) |
                       ((uint32_t)k & 2047u);
        ins->lee = bit(b);
        ins->escribe = bit(a);
        return 1;
    }
    return error(c, "instruccion desconocida '%s'", op);
}

/* Linea sin comentario. */
static void quitar_comentario(char *s)
{
    for (char *p = s; *p; ++p)
        if (*p == '#' || *p == ';' || (p[0] == '/' && p[1] == '/')) { *p = '\0'; return; }
}

/* Etiqueta "nombre:" al inicio; devuelve el resto de la linea o NULL. */
static char *etiqueta_inicial(char *s, char *nombre, size_t capacidad)
{
    if (!isalpha((unsigned char)*s) && *s != '_') return NULL;
    char *p = s;
    while (isalnum((unsigned char)*p) || *p == '_') ++p;
    if (*p != ':' || (size_t)(p - s) >= capacidad) return NULL;
    memcpy(nombre, s, (size_t)(p - s));
    nombre[p - s] = '\0';
    return p + 1;
}

typedef struct {
    char *codigo;
    unsigned linea;
} CbLinea;

static size_t sucesores(const CbInstr (*ins)[4], size_t i, size_t cantidad, size_t *out)
{
    const CbInstr *salto = &ins[i][2];
    size_t n = 0;
    if (salto->indirecto) return 0;
    if (salto->tiene_destino) out[n++] = (size_t)(salto->destino / 16);
    if ((!salto->tiene_destino || salto->condicional) && i + 1 < cantidad) out[n++] = i + 1;
    return n;
}

int cb_ensamblar(const char *fuente, CbPrograma *programa, FILE *diagnosticos)
{
    CbContexto c = {diagnosticos, 0, NULL, 0};
    if (!fuente || !programa) return 1;
    *programa = (CbPrograma){0};
    char *texto = malloc(strlen(fuente) + 1);
    CbLinea *lineas = NULL;
    CbInstr (*ins)[4] = NULL;
    c.etiquetas = malloc(CB_MAX_ETIQUETAS * sizeof(*c.etiquetas));
    int ok = texto && c.etiquetas;
    if (!ok) error(&c, "sin memoria%s", NULL);
    size_t cantidad = 0, capacidad = 0;

    /* Pasada 1: etiquetas y lineas con codigo. */
    if (ok) strcpy(texto, fuente);
    for (char *l = ok ? texto : NULL; ok && l;) {
        char *fin = strchr(l, '\n');
        if (fin) *fin = '\0';
        ++c.linea;
        quitar_comentario(l);
        char *codigo = recortar(l), nombre[64];
        char *despues = etiqueta_inicial(codigo, nombre, sizeof(nombre));
        if (despues) {
            if (buscar_etiqueta(&c, nombre) >= 0) ok = error(&c, "etiqueta repetida '%s'", nombre);
            else if (c.cantidad_etiquetas == CB_MAX_ETIQUETAS) ok = error(&c, "demasiadas etiquetas%s", NULL);
            else {
                strcpy(c.etiquetas[c.cantidad_etiquetas].nombre, nombre);
                c.etiquetas[c.cantidad_etiquetas++].pc = (long)cantidad * 16;
            }
            codigo = recortar(despues);
        }
        if (ok && *codigo) {
            if (cantidad == capacidad) {
                capacidad = capacidad ? capacidad * 2 : 256;
                CbLinea *nuevas = realloc(lineas, capacidad * sizeof(*nuevas));
                if (!nuevas) { ok = error(&c, "sin memoria%s", NULL); break; }
                lineas = nuevas;
            }
            lineas[cantidad++] = (CbLinea){codigo, c.linea};
        }
        l = fin ? fin + 1 : NULL;
    }
    c.linea = 0;
    if (ok && !cantidad) ok = error(&c, "el programa no contiene bundles%s", NULL);
    if (ok && cantidad > CB_MAX_BUNDLES) {
        char detalle[16];
        snprintf(detalle, sizeof(detalle), "%u", CB_MAX_BUNDLES);
        ok = error(&c, "la memoria admite como maximo %s bundles", detalle);
    }

    /* Pasada 2: codificar cada slot. */
    if (ok) {
        programa->bundles = calloc(cantidad, sizeof(*programa->bundles));
        ins = calloc(cantidad, sizeof(*ins));
        if (!programa->bundles || !ins) ok = error(&c, "sin memoria%s", NULL);
    }
    for (size_t i = 0; ok && i < cantidad; ++i) {
        c.linea = lineas[i].linea;
        CbBundle *bundle = &programa->bundles[i];
        bundle->linea = lineas[i].linea;
        char *slots[4];
        char nop_completo[] = "nop | nop | nop | nop";
        char *codigo = lineas[i].codigo;
        char minus[8] = "";
        if (strlen(codigo) < sizeof(minus)) { strcpy(minus, codigo); minusculas(minus); }
        if (!strcmp(minus, "nop")) codigo = nop_completo;
        size_t n = 0;
        for (char *p = codigo;;) {
            char *barra = strchr(p, '|');
            if (barra) *barra = '\0';
            if (n < 4) slots[n] = p;
            ++n;
            if (!barra) break;
            p = barra + 1;
        }
        if (n != 4) { ok = error(&c, "un bundle requiere 4 slots: ALU | LSU | BRU | CRIPTO%s", NULL); break; }
        for (int s = 0; ok && s < 4; ++s) {
            char *t = recortar(slots[s]);
            ok = codificar(&c, t, (long)i * 16, &ins[i][s]);
            if (!ok) break;
            snprintf(bundle->texto[s], sizeof(bundle->texto[s]), "%s", t);
            bundle->slots[s] = ins[i][s].palabra;
            if (ins[i][s].palabra && ins[i][s].slot != s) {
                char detalle[96];
                snprintf(detalle, sizeof(detalle), "'%s' corresponde al slot %d, no al %d",
                         t, ins[i][s].slot, s);
                ok = error(&c, "%s", detalle);
            } else if (ins[i][s].tiene_destino &&
                       (ins[i][s].destino < 0 || ins[i][s].destino >= (long)cantidad * 16))
                ok = error(&c, "el destino del salto esta fuera del programa%s", NULL);
        }
        for (int s = 0; ok && s < 4; ++s)
            for (int o = s + 1; ok && o < 4; ++o) {
                if (ins[i][s].escribe & ins[i][o].escribe)
                    ok = error(&c, "conflicto WAW entre slots del mismo bundle%s", NULL);
                else if ((ins[i][s].escribe & ins[i][o].lee) || (ins[i][o].escribe & ins[i][s].lee))
                    ok = error(&c, "dependencia RAW entre slots del mismo bundle%s", NULL);
            }
    }

    /* Latencia: lo escrito en N se lee desde N+3, por ambas rutas de los saltos. */
    for (size_t p = 0; ok && p < cantidad; ++p) {
        uint32_t escritos = 0;
        for (int s = 0; s < 4; ++s) escritos |= ins[p][s].escribe;
        size_t frontera[CB_MAX_BUNDLES], siguiente[CB_MAX_BUNDLES];
        size_t nf = sucesores((const CbInstr (*)[4])ins, p, cantidad, frontera);
        for (int distancia = 1; ok && distancia < CB_DISTANCIA_LECTURA; ++distancia) {
            size_t ns = 0;
            for (size_t f = 0; ok && f < nf; ++f) {
                size_t q = frontera[f];
                uint32_t leidos = 0;
                for (int s = 0; s < 4; ++s) leidos |= ins[q][s].lee;
                if (escritos & leidos) {
                    unsigned r = 0;
                    while (!((escritos & leidos) & (1u << r))) ++r;
                    char detalle[160];
                    snprintf(detalle, sizeof(detalle),
                             "dependencia RAW en x%u: el bundle %zu lo escribe y se lee a "
                             "distancia %d; deje dos bundles independientes o nop", r, p, distancia);
                    c.linea = programa->bundles[q].linea;
                    ok = error(&c, "%s", detalle);
                }
                size_t extra[2];
                size_t ne = sucesores((const CbInstr (*)[4])ins, q, cantidad, extra);
                for (size_t e = 0; e < ne; ++e) {
                    size_t j = 0;
                    while (j < ns && siguiente[j] != extra[e]) ++j;
                    if (j == ns) siguiente[ns++] = extra[e];
                }
            }
            memcpy(frontera, siguiente, ns * sizeof(*frontera));
            nf = ns;
        }
    }

    if (ok) programa->cantidad = cantidad;
    else cb_liberar(programa);
    free(ins);
    free(lineas);
    free(texto);
    free(c.etiquetas);
    return ok ? 0 : 1;
}

void cb_liberar(CbPrograma *programa)
{
    if (!programa) return;
    free(programa->bundles);
    *programa = (CbPrograma){0};
}

int cb_escribir_mem(const CbPrograma *programa, FILE *salida)
{
    for (size_t i = 0; i < programa->cantidad; ++i) {
        const uint32_t *s = programa->bundles[i].slots;
        if (fprintf(salida, "%08x%08x%08x%08x\n", (unsigned)s[3], (unsigned)s[2],
                    (unsigned)s[1], (unsigned)s[0]) < 0) return 1;
    }
    return fflush(salida) != 0;
}

int cb_escribir_bin(const CbPrograma *programa, FILE *salida)
{
    for (size_t i = 0; i < programa->cantidad; ++i)
        for (int s = 0; s < 4; ++s) {
            uint32_t w = programa->bundles[i].slots[s];
            unsigned char bytes[4] = {(unsigned char)w, (unsigned char)(w >> 8),
                                      (unsigned char)(w >> 16), (unsigned char)(w >> 24)};
            if (fwrite(bytes, 1, 4, salida) != 4) return 1;
        }
    return fflush(salida) != 0;
}

/* Anchos de campo segun el formato de la palabra, de bit 31 a bit 0. */
static const int *campos(uint32_t w)
{
    static const int registro[] = {7, 4, 5, 5, 5, 6, 0};
    static const int tres[] = {7, 4, 5, 5, 11, 0};       /* inmediato, memoria, control, syer */
    static const int sye[] = {7, 4, 5, 16, 0};
    static const int feistel[] = {7, 4, 2, 2, 5, 5, 7, 0}; /* fsl, fsli, ell */
    static const int direccion[] = {7, 4, 16, 5, 0};       /* vcr, camcon, setpwd */
    static const int crudo[] = {32, 0};
    unsigned tipo = w >> 25, id = (w >> 21) & 15u;
    if (tipo == TIPO_REGISTRO) return registro;
    if (tipo == TIPO_INMEDIATO || tipo == TIPO_MEMORIA || tipo == TIPO_CONTROL) return tres;
    if (tipo == TIPO_SALTO) return id == 0 ? sye : tres;
    if (tipo == TIPO_CRIPTO) return id <= 2 ? feistel : direccion;
    return crudo;
}

/* Por paquete: numero, PC y slots en ensamblador; la palabra completa en
 * hexadecimal y, por cada slot que no es nop, su binario separado por campos. */
int cb_escribir_listado(const CbPrograma *programa, FILE *salida)
{
    static const char *const unidad[] = {"ALU", "LSU", "BRU", "CRIPTO"};
    if (fputs("# paquete  PC          ensamblador: ALU | LSU | BRU | CRIPTO\n"
              "#                       binario: tipo id campos (bit 31 a 0)\n", salida) < 0)
        return 1;
    for (size_t i = 0; i < programa->cantidad; ++i) {
        const CbBundle *b = &programa->bundles[i];
        int vacio = !(b->slots[0] | b->slots[1] | b->slots[2] | b->slots[3]);
        if (vacio ? fprintf(salida, "%04zu       0x%08zx  nop\n", i, i * 16) < 0 :
                    fprintf(salida, "%04zu       0x%08zx  %s | %s | %s | %s\n", i, i * 16,
                            b->texto[0], b->texto[1], b->texto[2], b->texto[3]) < 0)
            return 1;
        if (fprintf(salida, "           hex         %08x %08x %08x %08x\n",
                    (unsigned)b->slots[3], (unsigned)b->slots[2],
                    (unsigned)b->slots[1], (unsigned)b->slots[0]) < 0) return 1;
        for (int s = 0; s < 4; ++s) {
            uint32_t w = b->slots[s];
            if (!w) continue;
            char bits[48];
            size_t n = 0;
            int bit = 31;
            for (const int *c = campos(w); *c; ++c) {
                for (int k = 0; k < *c; ++k, --bit) bits[n++] = (char)('0' + ((w >> bit) & 1u));
                bits[n++] = ' ';
            }
            bits[n - 1] = '\0';
            if (fprintf(salida, "           %-6s      %s\n", unidad[s], bits) < 0) return 1;
        }
    }
    return fflush(salida) != 0;
}
