#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "formato_binario.h"
#include "cli.h"
#include "lexer.h"

/*Se incluye interfaz del parser sintáctico, permite utilizar la funcion analizar_sintaxis() desde cli.c*/
#include "sintactico.h"
#include "semantic_modules.h"
#include "memory.h"
#include "enlace_funciones.h"
#include "codificacion_binaria.h"
#include "diagnosticos.h"
#include "clones.h"
#include "ir.h"
#include "ir_spilling.h"
#include "ir_vliw.h"
#include "dce.h"
#include "loop_unrolling.h"
#include "ro_json.h"
#include "lu_reporte.h"
#include "reordenamiento.h"
#include "ro_reporte.h"
#include "../../tools/vliw/optimizacion_cli.h"
#include "ir_vliw_programa.h"

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Opciones del modo interno --frontend. */
typedef struct {
    const char *input;
    int verbose;

    /*Bandera de sintaxis guarda si se pidio analizar sintaxis: 0 es desactivo y 1 es activado*/
    int syntax;
} Options;

static void usage_frontend(FILE *out) {
    fputs("Uso: balc --frontend [-v] [-t] <archivo_fuente>\n"
          "Modo de pruebas del frontend: sin -t solo analiza tokens; -v los lista;\n"
          "-t valida sintaxis e imprime el AST. No genera archivos.\n", out);
}

static int parse_args(int argc, char **argv, Options *opt) {
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!positional && strcmp(arg, "--") == 0) { positional = 1; continue; }
        if (!positional && strcmp(arg, "-v") == 0) { opt->verbose = 1; continue; }
        /*Se agrega la opcion -t para validar sintaxis */
        if (!positional && strcmp(arg, "-t") == 0) { opt->syntax = 1; continue; }
        if (!positional && strcmp(arg, "-h") == 0) { usage_frontend(stdout); return 1; }
        if (!positional && arg[0] == '-') {
            fprintf(stderr, "Opción desconocida: %s\n", arg);
            return -1;
        }
        if (opt->input) {
            fputs("Se admite un único archivo fuente\n", stderr);
            return -1;
        }
        opt->input = arg;
    }
    if (!opt->input) { usage_frontend(stderr); return -1; }
    return 0;
}

static char *read_source(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    size_t cap = 4096, used = 0;
    char *buffer = malloc(cap);
    if (!buffer) { fclose(f); fputs("Memoria insuficiente\n", stderr); return NULL; }
    for (;;) {
        if (used == cap) {
            if (cap > SIZE_MAX / 2) {
                fputs("Archivo demasiado grande\n", stderr);
                free(buffer); fclose(f); return NULL;
            }
            size_t next = cap * 2;
            char *grown = realloc(buffer, next);
            if (!grown) {
                fputs("Memoria insuficiente\n", stderr);
                free(buffer); fclose(f); return NULL;
            }
            buffer = grown;
            cap = next;
        }
        size_t n = fread(buffer + used, 1, cap - used, f);
        used += n;
        if (!n) {
            if (ferror(f)) {
                perror(path); free(buffer); fclose(f); return NULL;
            }
            break;
        }
    }
    fclose(f);
    *size = used;
    return buffer;
}

static void print_escaped(const Token *t) {
    putchar('"');
    for (size_t i = 0; i < t->length; ++i) {
        unsigned char c = (unsigned char)t->lexeme[i];
        if (c == '\n') fputs("\\n", stdout);
        else if (c == '\r') fputs("\\r", stdout);
        else if (c == '\t') fputs("\\t", stdout);
        else if (c == '"') fputs("\\\"", stdout);
        else if (c == '\\') fputs("\\\\", stdout);
        else if (c < 0x20 || c == 0x7f) printf("\\x%02X", c);
        else putchar(c);
    }
    putchar('"');
}

/* El AST se muestra solo cuando el programa completo es sintacticamente valido. */
static int mostrar_ast(const char *fuente, size_t longitud, const char *nombre) {
    Ast arbol = {0};
    int estado = construir_ast(fuente, longitud, nombre, stderr, &arbol);
    if (estado == 0) {
        puts("[AST]");
        ast_imprimir(&arbol, stdout);
        puts("Análisis sintáctico correcto. Generación de código pendiente.");
    }
    ast_liberar(&arbol);
    return estado;
}

/* Etapa 3B-D3F: reporte de datos, no mapa de codigo. */
static const char *clase_sim(SimClase c)
{
    switch (c) {
        case SIM_VARIABLE: return "variable";
        case SIM_CONSTANTE: return "constante";
        case SIM_FUNCION: return "funcion";
        case SIM_PARAMETRO: return "parametro";
        case SIM_LISTA: return "lista";
        case SIM_MODULO: return "modulo";
        case SIM_ETIQUETA: return "etiqueta";
    }
    return "desconocida";
}

static const char *tipo_sim(SimTipo t)
{
    switch (t) {
        case SIM_TIPO_INT: return "declare_int";
        case SIM_TIPO_BOOLEAN: return "declare_boolean";
        case SIM_TIPO_TEXT: return "declare_text";
        case SIM_TIPO_CHAR: return "declare_char";
        case SIM_TIPO_VOID: return "declare_infinite_void";
        case SIM_TIPO_ERROR: return "error";
    }
    return "desconocido";
}

static const char *almacen_sim(SimAlmacen a)
{
    switch (a) {
        case SIM_ALMACEN_SIN_ASIGNAR: return "sin_asignar";
        case SIM_ALMACEN_GLOBAL: return "global(gp)";
        case SIM_ALMACEN_LOCAL: return "local(sp)";
        case SIM_ALMACEN_PARAMETRO: return "parametro(sp)";
        case SIM_ALMACEN_CODIGO: return "codigo";
    }
    return "desconocido";
}

/* El mapa de memoria (CLI_GP_BASE, CLI_FIN_GLOBALES, CLI_TOPE_PILA y
 * CLI_CAPACIDAD_MARCO) esta en include/cli.h: lo comparte con el IDE. */

/* Opciones de la especificacion. Todas agregan salidas a la compilacion. */
typedef struct {
    const char *entrada;
    const char *salida;     /* -o */
    int detallado;          /* -v */
    int ensamblador;        /* -s */
    int ast;                /* -t */
    int ast_json;           /* exportacion sin compilacion */
    int diagnosticos;       /* --diagnosticos: solo revisa errores y los da en JSON */
    int simbolos;           /* -m */
    int desde_ensamblador;  /* -x */
    int paquetes;           /* -p */
    const char *fuente_optimizada; /* Préstamo de la copia privada de -O1. */
    int nivel, nivel_explicito; /* Sin opción: ruta histórica; O2 utiliza IR. */
    int ir;                 /* --ir: IR de tres direcciones */
    int ir_spilling;        /* --ir-spilling: IR con registros limitados */
    int vliw_ir;            /* --vliw-ir: vista previa del productor fisico */
    unsigned vliw_regs;    /* --vliw-ir=N: registros temporales (3..8) */
    int dce;                /* --dce: IR + variables vivas + eliminacion de codigo muerto */
    int unroll;             /* --unroll[=k]: IR antes/despues del loop unrolling */
    unsigned unroll_factor; /* k de --unroll=k (0: valor por defecto) */
    int reorder;            /* --reorder[=L]: reordenamiento seguro en bloques */
    unsigned reorder_lat;   /* L de --reorder=L (0: valor por defecto) */
    int reorder_json;       /* --reorder-json[=L]: grafo de dependencias en JSON */
} Compilacion;

typedef struct {
    const Compilacion *opciones;
    char *ensamblador;      /* Bundles en texto, propiedad del llamador. */
    EfResumen resumen;
    FbDatos datos;
} Backend;

static void usage(FILE *out) {
    fputs("Uso: balc [-o <archivo>] [-v] [-s] [-t] [-m] [-p] <archivo_fuente.bal>\n"
          "     balc -x [-o <archivo>] [-v] [-p] <archivo.asm>\n"
          "Compila el fuente y genera el binario (<fuente>.bin) y la imagen de IMEM (.mem).\n"
          "-o <archivo>  nombre del binario de salida (por defecto, el del fuente con .bin)\n"
          "-O0           referencia tradicional, sin pasadas opcionales\n"
          "-O1           DCE real con BAL reescrito y backend tradicional (un archivo)\n"
          "-O2           IR optimizada, spilling y cadena VLIW de programa completo\n"
          "-v            muestra cada fase del compilador y las decisiones importantes\n"
          "-s            genera tambien el ensamblador (.asm)\n"
          "-t            imprime el arbol sintactico (AST)\n"
          "-m            imprime la tabla de simbolos y genera <fuente>.simbolos.md\n"
          "-x            toma un archivo de ensamblador y genera el binario\n"
          "-p            muestra los paquetes generados en ensamblador y en binario\n"
          "--ast-json    exporta solamente el AST como JSON por stdout\n"
          "--ir          valida la semantica y muestra la IR y sus bloques basicos\n"
          "--ir-spilling  muestra la IR con spilling usando 3 registros\n"
          "--vliw-ir[=N] genera fragmento fisico desde IR/spilling (N=3..8; defecto 3)\n"
          "              solo el subconjunto soportado; NO es binario BAL ejecutable\n"
          "              -s guarda .vliw.asm y .vliw.mem; -p muestra bundles\n"
          "--dce         como --ir, y ademas calcula las variables vivas y elimina\n"
          "              el codigo muerto (muestra la IR antes y despues)\n"
          "--unroll[=k]  muestra la IR antes y despues del loop unrolling (factor k,\n"
          "              por defecto 4); no genera ningun archivo\n"
          "--reorder[=L] reordena cada bloque basico respetando RAW/WAR/WAW y muestra\n"
          "              la IR antes y despues (latencia L, por defecto 3); se puede\n"
          "              combinar con --unroll (primero desenrolla y luego reordena)\n"
          "--reorder-json[=L]  exporta el grafo de dependencias de cada bloque (ruta\n"
          "              critica y orden nuevo) como JSON por stdout; admite --unroll\n"
          "--diagnosticos  revisa el fuente y escribe sus errores como JSON por\n"
          "              stdout; no genera ningun archivo\n"
          "--clones [--json] [--minimo <tokens>] <archivo.bal>...\n"
          "              busca codigo copiado (clones tipo I y II) entre los archivos;\n"
          "              no genera ningun archivo\n", out);
}

static int parse_compilacion(int argc, char **argv, Compilacion *c)
{
    int posicional = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        /* Los niveles no se mezclan ni seleccionan una degradación implícita. */
        if (!posicional && !strncmp(arg,"-O",2)) {
            if (c->nivel_explicito || strlen(arg)!=3 || arg[2]<'0' || arg[2]>'2') {
                fputs("nivel inválido o repetido: use -O0, -O1 o -O2\n",stderr);return -1;
            }
            c->nivel_explicito=1;c->nivel=arg[2]-'0';continue;
        }
        if (!posicional && !strcmp(arg, "--ast-json")) { c->ast_json = 1; continue; }
        if (!posicional && !strcmp(arg, "--diagnosticos")) { c->diagnosticos = 1; continue; }
        if (!posicional && !strcmp(arg, "--ir")) { c->ir = 1; continue; }
        if (!posicional && !strcmp(arg, "--ir-spilling")) { c->ir_spilling = 1; continue; }
        if (!posicional && (!strcmp(arg, "--vliw-ir") || !strncmp(arg, "--vliw-ir=", 10))) {
            c->vliw_ir = 1;
            c->vliw_regs = 3;
            if (arg[9] == '=') {
                char *fin = NULL;
                unsigned long n = strtoul(arg + 10, &fin, 10);
                if (!arg[10] || *fin || n < 3 || n > 8) {
                    fputs("--vliw-ir=N necesita entre 3 y 8 registros\n", stderr);
                    return -1;
                }
                c->vliw_regs = (unsigned)n;
            }
            continue;
        }
        /* --dce trabaja sobre la IR: cuenta como --ir (y tiene sus mismas reglas). */
        if (!posicional && !strcmp(arg, "--dce")) { c->ir = 1; c->dce = 1; continue; }
        if (!posicional && !strncmp(arg, "--unroll", 8) && (arg[8] == '\0' || arg[8] == '=')) {
            c->ir = c->unroll = 1;
            if (arg[8] == '=') {
                char *fin = NULL;
                unsigned long k = strtoul(arg + 9, &fin, 10);
                if (!arg[9] || *fin || k < 2 || k > 64) {
                    fputs("--unroll=k necesita un factor entero entre 2 y 64\n", stderr);
                    return -1;
                }
                c->unroll_factor = (unsigned)k;
            }
            continue;
        }
        if (!posicional && !strncmp(arg, "--reorder-json", 14) && (arg[14] == '\0' || arg[14] == '=')) {
            c->ir = c->reorder_json = 1;
            if (arg[14] == '=') {
                char *fin = NULL;
                unsigned long l = strtoul(arg + 15, &fin, 10);
                if (!arg[15] || *fin || l < 1 || l > 16) {
                    fputs("--reorder-json=L necesita una latencia entera entre 1 y 16\n", stderr);
                    return -1;
                }
                c->reorder_lat = (unsigned)l;
            }
            continue;
        }
        if (!posicional && !strncmp(arg, "--reorder", 9) && (arg[9] == '\0' || arg[9] == '=')) {
            c->ir = c->reorder = 1;
            if (arg[9] == '=') {
                char *fin = NULL;
                unsigned long l = strtoul(arg + 10, &fin, 10);
                if (!arg[10] || *fin || l < 1 || l > 16) {
                    fputs("--reorder=L necesita una latencia entera entre 1 y 16\n", stderr);
                    return -1;
                }
                c->reorder_lat = (unsigned)l;
            }
            continue;
        }
        if (!posicional && arg[0] == '-' && arg[1] && !arg[2]) {
            switch (arg[1]) {
                case 'h': usage(stdout); return 1;
                case 'v': c->detallado = 1; continue;
                case 's': c->ensamblador = 1; continue;
                case 't': c->ast = 1; continue;
                case 'm': c->simbolos = 1; continue;
                case 'x': c->desde_ensamblador = 1; continue;
                case 'p': c->paquetes = 1; continue;
                case 'o':
                    if (++i >= argc) { fputs("-o necesita un archivo de salida\n", stderr); return -1; }
                    c->salida = argv[i];
                    continue;
                default: break;
            }
        }
        if (!posicional && strcmp(arg, "--") == 0) { posicional = 1; continue; }
        if (!posicional && arg[0] == '-') {
            fprintf(stderr, "Opción desconocida: %s\n", arg);
            return -1;
        }
        if (c->entrada) { fputs("Se admite un único archivo fuente\n", stderr); return -1; }
        c->entrada = arg;
    }
    if (!c->entrada) { usage(stderr); return -1; }
    /* O0 conserva la compilación normal; O1 nunca convierte vistas IR/JSON
     * ni ensamblador existente en una supuesta optimización productiva. */
    if (c->nivel_explicito && (c->ir || c->ir_spilling || c->vliw_ir || c->ast_json ||
        c->diagnosticos || c->desde_ensamblador)) {
        fputs("-O requiere compilación BAL normal; incompatible con vistas o -x\n",stderr);return -1;
    }
    /* El producto VLIW de IR es un fragmento para inspeccion, no un .bin
     * ejecutable. Prohibir salidas que impliquen enlazado o ABI completos. */
    if (c->vliw_ir && (c->ir || c->ir_spilling || c->dce || c->unroll ||
                       c->reorder || c->reorder_json || c->desde_ensamblador ||
                       c->ast_json || c->diagnosticos || c->simbolos || c->salida)) {
        fputs("--vliw-ir solo admite -v, -t, -s, -p y un fuente .bal; "
              "no admite -o/-x ni otras pasadas\n", stderr);
        return -1;
    }
    /* Los modos de visualizacion de IR son excluyentes. */
    if (c->ir && c->ir_spilling) {
        fputs("--ir y --ir-spilling no se pueden combinar\n", stderr);
        return -1;
    }
    if ((c->ir || c->ir_spilling) && (c->ast_json || c->diagnosticos || c->detallado || c->ensamblador ||
                  c->ast || c->simbolos || c->desde_ensamblador || c->paquetes || c->salida)) {
        fputs("--ir y --ir-spilling se ejecutan de forma independiente (sin otras banderas)\n", stderr);
        fputs("--ir, --dce, --unroll y --reorder se ejecutan de forma independiente (sin otras banderas)\n", stderr);
        return -1;
    }
    if (c->dce && (c->unroll || c->reorder || c->reorder_json)) {
        fputs("--dce no se combina con --unroll, --reorder ni --reorder-json\n", stderr);
        return -1;
    }
    if (c->reorder_json && c->reorder) {
        fputs("--reorder-json ya incluye el reordenamiento: no se combina con --reorder\n", stderr);
        return -1;
    }
    if (c->ast_json && (c->detallado || c->ensamblador || c->simbolos ||
                        c->desde_ensamblador || c->paquetes || c->salida)) {
        fputs("--ast-json solo admite -t; no se combina con -v, -m, -s, -x, -p ni -o\n", stderr);
        return -1;
    }
    /* --diagnosticos solo revisa errores, asi que no se mezcla con opciones que generan salidas. */
    if (c->diagnosticos && (c->ast_json || c->detallado || c->ensamblador || c->ast ||
                            c->simbolos || c->desde_ensamblador || c->paquetes || c->salida)) {
        fputs("--diagnosticos no se combina con otras opciones\n", stderr);
        return -1;
    }
    if (c->desde_ensamblador && (c->ensamblador || c->ast || c->simbolos)) {
        fputs("-x toma ensamblador: no se combina con -s, -t ni -m\n", stderr);
        return -1;
    }
    return 0;
}

static int termina_en(const char *s, const char *sufijo)
{
    size_t a = strlen(s), b = strlen(sufijo);
    return a >= b && strcmp(s + a - b, sufijo) == 0;
}

/* base sin la extension quitar (si la tiene) + extension nueva. */
static char *cambiar_extension(const char *base, const char *quitar, const char *nueva)
{
    size_t largo = strlen(base);
    if (termina_en(base, quitar)) largo -= strlen(quitar);
    char *nombre = malloc(largo + strlen(nueva) + 1);
    if (!nombre) return NULL;
    memcpy(nombre, base, largo);
    strcpy(nombre + largo, nueva);
    return nombre;
}

/* Contenido completo de un temporal; puede contener bytes 0. */
static char *leer_temporal(FILE *f, long *largo_out)
{
    long largo = ftell(f);
    if (largo < 0 || fseek(f, 0, SEEK_SET) != 0) return NULL;
    char *texto = malloc((size_t)largo + 1);
    if (texto && fread(texto, 1, (size_t)largo, f) != (size_t)largo) {
        free(texto);
        return NULL;
    }
    if (texto) texto[largo] = '\0';
    if (texto && largo_out) *largo_out = largo;
    return texto;
}

/* El archivo se crea solo cuando el contenido ya esta completo. */
static int escribir_archivo(const char *ruta, const CbPrograma *programa,
                            int (*escribir)(const CbPrograma *, FILE *),
                            const char *texto, const FbDatos *datos)
{
    FILE *temporal = tmpfile();
    if (!temporal) { perror("tmpfile"); return 0; }
    int ok = texto ? fputs(texto, temporal) >= 0 :
             datos ? fb_escribir(programa, datos, temporal) == 0 :
             escribir(programa, temporal) == 0;
    long largo = 0;
    char *contenido = ok ? leer_temporal(temporal, &largo) : NULL;
    fclose(temporal);
    FILE *salida = contenido ? fopen(ruta, "wb") : NULL;
    ok = salida && fwrite(contenido, 1, (size_t)largo, salida) == (size_t)largo;
    if (salida && fclose(salida) != 0) ok = 0;
    free(contenido);
    if (!ok) perror(ruta);
    return ok;
}

static void imprimir_simbolos(const SimTabla *tabla, const MemProyectoMapa *mapa,
                              const RrTabla *funciones)
{
    puts("[SIMBOLOS]");
    puts("nombre | clase | tipo | ambito | nivel | almacen | bytes | desplazamiento | direccion_codigo");
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden) {
        const SimAmbito *a = s->ambito;
        uint32_t direccion = 0;
        int con_direccion = funciones && s->clase == SIM_FUNCION &&
                            a == tabla->global &&
                            rr_buscar(funciones, s->nombre, RR_FUNCION, &direccion) == RR_OK;
        printf("%s | %s | %s | %s | %u | %s | %zu | ",
               s->nombre ? s->nombre : "?", clase_sim(s->clase),
               tipo_sim(s->tipo), a && a->nombre ? a->nombre : "?",
               a ? a->nivel : 0, con_direccion ? "codigo" : almacen_sim(s->almacen), s->bytes);
        if (s->almacen == SIM_ALMACEN_GLOBAL ||
            s->almacen == SIM_ALMACEN_LOCAL ||
            s->almacen == SIM_ALMACEN_PARAMETRO)
            printf("%ld", s->desplazamiento);
        else
            fputs("pendiente", stdout);
        if (con_direccion) printf(" | 0x%04x\n", (unsigned)direccion);
        else puts(" | pendiente");
    }
    printf("[MEMORIA] funciones=%zu marco_maximo_bytes=%zu\n",
           mapa->funciones, mapa->marco_maximo_bytes);
    printf("[MEMORIA] codigo desde 0x0000; gp=0x%04zx (globales: %zu bytes); sp=0x%05x\n",
           mapa->globales.gp_base, mapa->globales.bytes_globales, CLI_TOPE_PILA);
}

/* Exporta la misma informacion de -m en una tabla Markdown. */
static int exportar_simbolos_md(const char *fuente, const SimTabla *tabla,
                               const MemProyectoMapa *mapa, const RrTabla *funciones)
{
    char *ruta = cambiar_extension(fuente, ".bal", ".simbolos.md");
    if (!ruta) {
        fputs("Memoria insuficiente para crear el reporte de simbolos\n", stderr);
        return 0;
    }
    FILE *f = fopen(ruta, "w");
    if (!f) {
        perror(ruta);
        free(ruta);
        return 0;
    }
    fputs("# Tabla de simbolos\n\n", f);
    fputs("| Nombre | Clase | Tipo | Ambito | Nivel | Almacen | Bytes | Desplazamiento | Direccion de codigo |\n", f);
    fputs("|---|---|---|---|---:|---|---:|---:|---|\n", f);
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden) {
        const SimAmbito *a = s->ambito;
        uint32_t direccion = 0;
        int con_direccion = funciones && s->clase == SIM_FUNCION &&
                            a == tabla->global &&
                            rr_buscar(funciones, s->nombre, RR_FUNCION, &direccion) == RR_OK;
        fprintf(f, "| %s | %s | %s | %s | %u | %s | %zu | ",
                s->nombre ? s->nombre : "?", clase_sim(s->clase),
                tipo_sim(s->tipo), a && a->nombre ? a->nombre : "?",
                a ? a->nivel : 0,
                con_direccion ? "codigo" : almacen_sim(s->almacen), s->bytes);
        if (s->almacen == SIM_ALMACEN_GLOBAL ||
            s->almacen == SIM_ALMACEN_LOCAL ||
            s->almacen == SIM_ALMACEN_PARAMETRO)
            fprintf(f, "%ld", s->desplazamiento);
        else
            fputs("pendiente", f);
        if (con_direccion) fprintf(f, " | 0x%04x |\n", (unsigned)direccion);
        else fputs(" | pendiente |\n", f);
    }
    fprintf(f, "\n## Memoria\n\n"
               "- Funciones: %zu\n"
               "- Marco maximo: %zu bytes\n"
               "- Codigo desde: `0x0000`\n"
               "- GP: `0x%04zx`\n"
               "- Memoria global: %zu bytes\n"
               "- SP: `0x%05x`\n",
            mapa->funciones, mapa->marco_maximo_bytes,
            mapa->globales.gp_base, mapa->globales.bytes_globales, CLI_TOPE_PILA);
    int ok = !ferror(f);
    if (fclose(f) != 0) ok = 0;
    if (!ok) perror(ruta);
    else printf("[SIMBOLOS] Reporte Markdown: %s\n", ruta);
    free(ruta);
    return ok;
}

/* Visitante: memoria, -m, generacion, enlace con arranque y bundles. */
static int compilar_proyecto(void *ctx, const Ast *arbol, SimTabla *tabla, FILE *diagnosticos)
{
    Backend *b = ctx;
    const Compilacion *op = b->opciones;
    if (op->detallado) puts("[fase 3] semántico: declaraciones, tipos, ámbitos y llamadas válidos");
    MemProyectoMapa mapa = {0};
    MemProyectoResultado r = mem_asignar_proyecto_referencias(tabla, CLI_GP_BASE, CLI_FIN_GLOBALES,
                                                  CLI_CAPACIDAD_MARCO, &mapa);
    if (r != MEM_PROYECTO_OK) {
        fprintf(diagnosticos, "asignacion de memoria no disponible (codigo %d)\n", (int)r);
        return 1;
    }
    if (op->detallado)
        printf("[fase 3] memoria: globales desde gp=0x%04zx (%zu bytes), pila desde sp=0x%05x "
               "hacia abajo; %zu funciones, marco maximo %zu bytes (ra en sp+0)\n",
               mapa.globales.gp_base, mapa.globales.bytes_globales, CLI_TOPE_PILA,
               mapa.funciones, mapa.marco_maximo_bytes);

    const EfArranque arranque = {(uint32_t)mapa.globales.gp_base, CLI_TOPE_PILA};
    RrTabla funciones;
    rr_inicializar(&funciones);
    FILE *temporal = tmpfile();
    if (!temporal) { perror("tmpfile"); return 2; }
    int ok = ef_enlazar_referencias(arbol, tabla, 0, &arranque, &funciones, temporal,
                        diagnosticos, &b->resumen) == 0;
    if (ok) ok = fb_preparar_datos(arbol, tabla, (uint32_t)mapa.globales.gp_base,
                                   mapa.globales.bytes_globales, &b->datos, diagnosticos) == 0;
    if (ok) b->ensamblador = leer_temporal(temporal, NULL);
    fclose(temporal);

    /* -m se muestra aunque la generacion falle; con exito incluye las direcciones. */
    int estado = 0;
    if (op->simbolos) {
        imprimir_simbolos(tabla, &mapa, ok ? &funciones : NULL);
        if (!exportar_simbolos_md(op->entrada, tabla, &mapa,
                                  ok ? &funciones : NULL)) estado = 2;
    }
    if (!ok) {
        fputs("no se pudo generar el ensamblador\n", diagnosticos);
        estado = 1;
    } else if (!b->ensamblador) {
        fputs("Memoria insuficiente\n", stderr);
        estado = 2;
    } else if (op->detallado) {
        printf("[fase 4] ensamblador: %zu funciones; arranque en 0x0000 llama a main\n",
               b->resumen.funciones);
        for (const AstNodo *f = arbol->raiz->primer_hijo; f; f = f->siguiente) {
            uint32_t d;
            if (f->tipo == AST_FUNCION &&
                rr_buscar(&funciones, f->primer_hijo->texto, RR_FUNCION, &d) == RR_OK)
                printf("           %s en 0x%04x\n", f->primer_hijo->texto, (unsigned)d);
        }
        printf("[fase 5] saltos: %zu resueltos, %zu reescritos como condicional + sye por "
               "alcance; %zu vueltas de enlace; %zu bundles (1 instruccion + 2 nop)\n",
               b->resumen.saltos.saltos, b->resumen.saltos.relajados,
               b->resumen.vueltas, b->resumen.saltos.bundles);
    }
    rr_liberar(&funciones);
    return estado;
}

static int contar_tokens(const char *fuente, size_t largo, const char *nombre,
                         size_t *tokens, size_t *errores)
{
    Lexer lexer;
    lexer_init(&lexer, fuente, largo, nombre);
    *tokens = *errores = 0;
    for (;;) {
        Token t = lexer_next(&lexer);
        if (t.error == TOKEN_OUT_OF_MEMORY) {
            fprintf(stderr, "%s:%zu:%zu: memoria insuficiente\n", nombre, t.line, t.column);
            token_dispose(&t);
            return 0;
        }
        if (t.type == TOKEN_ERROR) {
            fprintf(stderr, "%s:%zu:%zu: error léxico: %s\n",
                    nombre, t.line, t.column, token_error_message(t.error));
            ++*errores;
        }
        TokenType tipo = t.type;
        if (tipo != TOKEN_EOF) ++*tokens;
        token_dispose(&t);
        if (tipo == TOKEN_EOF) return 1;
    }
}

/* Fases 1 y 2 con -v o -t. Sin ellas, el analisis semantico vuelve a
 * leer y analizar el fuente por su cuenta. */
static int frontend(const Compilacion *op)
{
    size_t largo = 0;
    char *fuente = read_source(op->fuente_optimizada?op->fuente_optimizada:op->entrada, &largo);
    if (!fuente) return 2;
    int estado = 0;
    if (op->detallado) {
        size_t tokens, errores;
        if (!contar_tokens(fuente, largo, op->entrada, &tokens, &errores)) estado = 2;
        else {
            printf("[fase 1] léxico: %zu tokens, %zu errores\n", tokens, errores);
            if (errores) estado = 1;
        }
    }
    if (estado == 0) {
        Ast arbol = {0};
        if (construir_ast(fuente, largo, op->entrada, stderr, &arbol) != 0) estado = 1;
        else {
            if (op->detallado) puts("[fase 2] sintáctico: AST construido");
            if (op->ast) {
                puts("[AST]");
                ast_imprimir(&arbol, stdout);
            }
        }
        ast_liberar(&arbol);
    }
    free(fuente);
    return estado;
}

static int compilar(const Compilacion *op)
{
    int es_asm = termina_en(op->entrada, ".asm");
    if (es_asm && !op->desde_ensamblador) {
        fputs("la entrada es ensamblador: use -x para generar el binario\n", stderr);
        return 2;
    }
    if (op->detallado) printf("[balc] %s\n", op->entrada);

    Backend datos = {0};
    datos.opciones = op;
    datos.datos.base = CLI_GP_BASE;
    int estado = 0;
    if (op->desde_ensamblador) {
        size_t largo = 0;
        datos.ensamblador = read_source(op->entrada, &largo);
        if (!datos.ensamblador) return 2;
        char *texto = realloc(datos.ensamblador, largo + 1);
        if (!texto) { free(datos.ensamblador); fputs("Memoria insuficiente\n", stderr); return 2; }
        texto[largo] = '\0';
        datos.ensamblador = texto;
    } else {
        if (op->detallado || op->ast) estado = frontend(op);
        if (estado == 0)
            estado = sem_visitar_proyecto(op->fuente_optimizada?op->fuente_optimizada:op->entrada, stderr, compilar_proyecto, &datos);
    }

    if (estado == 0 && op->ensamblador) {
        char *destino = cambiar_extension(op->entrada, ".bal", ".asm");
        if (!destino || !escribir_archivo(destino, NULL, NULL, datos.ensamblador, NULL)) estado = 2;
        else printf("[ensamblador] %s: %zu bundles, %zu funciones, %zu saltos (%zu reescritos)\n",
                    destino, datos.resumen.saltos.bundles, datos.resumen.funciones,
                    datos.resumen.saltos.saltos, datos.resumen.saltos.relajados);
        free(destino);
    }

    CbPrograma programa = {0};
    if (estado == 0) {
        if (cb_ensamblar(datos.ensamblador, &programa, stderr) != 0) {
            fputs("el ensamblador no es valido para la ISA\n", stderr);
            estado = 1;
        } else if (op->detallado)
            printf("[fase 6] binario: %zu bundles codificados; slots, rangos, saltos y "
                   "latencias (N+3) validados\n", programa.cantidad);
    }
    if (estado == 0) {
        char *bin = op->salida ? cambiar_extension(op->salida, "", "") :
                    cambiar_extension(op->entrada, es_asm ? ".asm" : ".bal", ".bin");
        char *mem = bin ? cambiar_extension(bin, ".bin", ".mem") : NULL;
        if (!bin || !mem) estado = 2;
        else if (!strcmp(bin, mem) || !strcmp(bin, op->entrada) || !strcmp(mem, op->entrada)) {
            fputs("la entrada y las salidas deben tener rutas distintas\n", stderr);
            estado = 2;
        } else if (!escribir_archivo(bin, &programa, NULL, NULL, &datos.datos) ||
                   !escribir_archivo(mem, &programa, cb_escribir_mem, NULL, NULL)) estado = 2;
        else printf("[binario] %s y %s: %zu bundles, %zu bytes\n",
                    bin, mem, programa.cantidad, FB_CABECERA_BYTES + 16 * programa.cantidad + datos.datos.cantidad);
        free(bin);
        free(mem);
    }
    if (estado == 0 && op->paquetes && cb_escribir_listado(&programa, stdout) != 0) estado = 2;
    cb_liberar(&programa);
    free(datos.ensamblador);
    return estado != 0 ? estado : ferror(stdout) ? 2 : 0;
}

/* JSON limpio para el visualizador, sin ejecutar las fases de generacion. */
static int exportar_ast_json(const char *ruta)
{
    size_t largo = 0;
    char *fuente = read_source(ruta, &largo);
    if (!fuente) return 2;
    Ast arbol = {0};
    int estado = construir_ast(fuente, largo, ruta, stderr, &arbol);
    if (!estado && ast_exportar_json(&arbol, stdout) != 0) {
        fputs("No se pudo escribir el AST en JSON\n", stderr);
        estado = 2;
    }
    ast_liberar(&arbol);
    free(fuente);
    return estado;
}

/*
 * --diagnosticos: revisa el archivo (lexico, sintactico y semantico,
 * incluidos los archivos que importa) SIN generar codigo.
 *
 * Como funciona:
 *   1. Las fases normalmente escriben sus errores en la pantalla (stderr).
 *      Aqui les damos un archivo temporal para que escriban ahi.
 *   2. Leemos ese archivo y separamos cada error en sus partes.
 *   3. Imprimimos todos los errores como JSON.
 *
 * Valor de salida: 0 = sin errores, 1 = el codigo tiene errores,
 *                  2 = fallo interno (memoria o archivos).
 */
static int exportar_diagnosticos(const char *ruta)
{
    /* Paso 1: analizar el fuente escribiendo los errores en un archivo temporal. */
    FILE *temporal = tmpfile();
    if (!temporal) { perror("tmpfile"); return 2; }
    int estado = sem_analizar_proyecto(ruta, temporal);

    /* Paso 2: volver al inicio del archivo temporal y leer los errores. */
    DiagLista lista = {0};
    rewind(temporal);
    if (diag_leer_flujo(temporal, &lista) != 0) {
        fputs("Memoria insuficiente\n", stderr);
        estado = 2;
    }
    /* Paso 3: imprimir los errores como JSON. */
    else if (diag_escribir_json(ruta, &lista, stdout) != 0) {
        estado = 2;
    }

    diag_liberar(&lista);
    fclose(temporal);
    return estado;
}

/*
 * --clones: busca codigo copiado entre varios archivos (ver clones.c).
 *
 *   balc --clones a.bal b.bal ...            listado para leer
 *   balc --clones --json a.bal b.bal ...     JSON para el IDE
 *   balc --clones --minimo 6 a.bal ...       cambia el tamano minimo de un clon
 *
 * A diferencia de las otras opciones, recibe VARIOS archivos, por eso se
 * atiende aparte. Valor de salida: 0 = se pudo revisar (haya o no clones),
 * 2 = error en las opciones, al leer un archivo o falta de memoria.
 */
static int buscar_clones(int argc, char **argv)
{
    int json = 0;
    size_t minimo = CLONES_MINIMO_TOKENS;
    ClonFuente *fuentes = calloc((size_t)argc, sizeof *fuentes);
    size_t cantidad = 0;
    int estado = 0;
    if (!fuentes) { fputs("Memoria insuficiente\n", stderr); return 2; }

    /* Paso 1: leer las opciones y los archivos (argv[1] es "--clones"). */
    for (int i = 2; i < argc && estado == 0; ++i) {
        if (!strcmp(argv[i], "--json")) { json = 1; continue; }
        if (!strcmp(argv[i], "--minimo")) {
            char *fin = NULL;
            long valor = (i + 1 < argc) ? strtol(argv[i + 1], &fin, 10) : 0;
            if (i + 1 >= argc || *fin != '\0' || valor < 1) {
                fputs("--minimo necesita un numero de tokens mayor que 0\n", stderr);
                estado = 2;
                break;
            }
            minimo = (size_t)valor;
            i++;
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "Opción desconocida: %s\n", argv[i]);
            estado = 2;
            break;
        }
        /* Paso 2: leer el texto de cada archivo. */
        size_t largo = 0;
        char *texto = read_source(argv[i], &largo);
        if (!texto) { estado = 2; break; }
        ClonFuente f = {argv[i], texto, largo};
        fuentes[cantidad++] = f;
    }
    if (estado == 0 && cantidad == 0) {
        fputs("--clones necesita al menos un archivo .bal\n", stderr);
        estado = 2;
    }

    /* Paso 3: buscar los clones y mostrarlos. */
    ClonLista lista = {0};
    if (estado == 0 && clones_buscar(fuentes, cantidad, minimo, &lista) != 0) {
        fputs("Memoria insuficiente\n", stderr);
        estado = 2;
    }
    if (estado == 0) {
        int escrito = json ? clones_escribir_json(&lista, cantidad, stdout)
                           : clones_escribir_texto(&lista, cantidad, stdout);
        if (escrito != 0) estado = 2;
    }

    clones_liberar(&lista);
    for (size_t i = 0; i < cantidad; ++i) free((char *)fuentes[i].texto);
    free(fuentes);
    return estado;
}

/* Solo para visualizar IR validada: no genera ensamblador ni binario.
 * "ctx" apunta a la Compilacion. Con --dce tambien muestra las variables
 * vivas de cada bloque, elimina el codigo muerto y muestra la IR resultante.
 * Con --unroll / --reorder / --reorder-json aplica esas optimizaciones. */
static int visitante_ir(void *ctx, const Ast *arbol, SimTabla *tabla, FILE *diag)
{
    const Compilacion *op = ctx;
    (void)tabla;
    int con_dce = op && op->dce;
    IrPrograma ir = {0};
    int estado = ir_desde_ast(arbol, &ir, diag);
    if (estado == 0 && op && op->reorder_json) {
        /* --reorder-json: solo JSON por stdout (el unrolling, si se pidio, va en silencio). */
        LuOpciones lu;
        lu_opciones_defecto(&lu);
        if (op->unroll_factor) lu.factor = op->unroll_factor;
        if (op->unroll) estado = lu_desenrollar(&ir, &lu, NULL, NULL);
        RoOpciones ro;
        ro_opciones_defecto(&ro);
        if (op->reorder_lat) ro.latencia = op->reorder_lat;
        if (estado == 0) estado = ro_exportar_json(stdout, op->entrada, &ir, ro.latencia,
                                                   op->unroll ? lu.factor : 0);
    } else if (estado == 0 && op && (op->unroll || op->reorder)) {
      if (op->unroll) {
        /* --unroll: foto de la IR, desenrollado, otra foto y reporte visual. */
        LuOpciones opciones;
        LuResumen resumen = {0};
        LuVista antes = {0}, despues = {0};
        lu_opciones_defecto(&opciones);
        if (op->unroll_factor) opciones.factor = op->unroll_factor;
        estado = lu_vista_crear(&ir, NULL, &antes);
        if (estado == 0) estado = lu_desenrollar(&ir, &opciones, &resumen, NULL);
        if (estado == 0) estado = lu_vista_crear(&ir, &antes, &despues);
        if (estado == 0) lu_reporte(stdout, op->entrada, opciones.factor, &antes, &despues,
                                    &resumen, lu_usar_color(stdout));
        lu_vista_liberar(&antes);
        lu_vista_liberar(&despues);
        lu_resumen_liberar(&resumen);
      }
      if (estado == 0 && op->reorder) {
        /* --reorder: reordena (despues del unrolling si tambien se pidio). */
        RoOpciones ro;
        RoResumen rr = {0};
        ro_opciones_defecto(&ro);
        if (op->reorder_lat) ro.latencia = op->reorder_lat;
        estado = ro_reordenar(&ir, &ro, &rr);
        if (estado == 0) ro_reporte(stdout, op->entrada, &ir, &rr, ro.latencia, ro_usar_color(stdout));
        ro_resumen_liberar(&rr);
      }
    } else if (estado == 0) {
        puts("[IR de tres direcciones]");
        estado = ir_imprimir(&ir, stdout);
    }
    if (estado == 0 && con_dce) {
        DceVivas vivas;
        estado = dce_calcular_vivas(&ir, &vivas);
        if (estado == 0) {
            puts("\n[Analisis de variables vivas]");
            estado = dce_imprimir_vivas(&vivas, stdout);
        }
        dce_liberar_vivas(&vivas);

        DceResultado resultado = {0};
        if (estado == 0) estado = dce_eliminar(&ir, &resultado);
        if (estado == 0) {
            puts("\n[Eliminacion de codigo muerto]");
            estado = dce_imprimir_resultado(&resultado, stdout);
        }
        if (estado == 0) {
            puts("\n[IR despues del DCE]");
            estado = ir_imprimir(&ir, stdout);
        }
        dce_liberar_resultado(&resultado);
    }
    ir_liberar(&ir);
    return estado;
}

/* Ruta explicita de inspeccion del productor fisico. Solo se aceptan
 * fragmentos para los que ir_vliw_desde_ir acredita ISA, CFG/DAG y ABI acotada.
 * A diferencia de compilar_proyecto, no se afirma enlazado ni ejecutabilidad. */
static int visitante_vliw_ir(void *ctx, const Ast *arbol, SimTabla *tabla,
                            FILE *diag)
{
    const Compilacion *op = ctx;
    IrPrograma ir = {0};
    IrVliwResultado resultado = {0};
    MemProyectoMapa mapa = {0};
    int estado = 1;
    if (mem_asignar_proyecto(tabla, CLI_GP_BASE, CLI_FIN_GLOBALES,
                             CLI_CAPACIDAD_MARCO, &mapa) != MEM_PROYECTO_OK) {
        fputs("--vliw-ir: no se pudo calcular el marco de memoria\n", diag);
        return 1;
    }
    /* La funcion aislada necesita reservar al menos RA en sp+0. Aun
     * con marco real, variables/parametros se rechazan en el lowering. */
    size_t marco = mapa.marco_maximo_bytes > 4 ? mapa.marco_maximo_bytes : 4;
    estado = ir_desde_ast(arbol, &ir, diag);
    if (estado) goto fin;
    /* IR valida no implica que el productor fisico pueda traducirla.
     * Mantener error por defecto hasta que todos los pasos VLIW terminen. */
    estado = 1;
    IrVliwConfiguracion cfg = {op->vliw_regs, marco};
    IrVliwEstado rc = ir_vliw_desde_ir(&ir, &cfg, &resultado, diag);
    if (rc != IR_VLIW_OK) {
        fprintf(diag, "--vliw-ir: productor fisico rechazado (estado %d); "
                      "no se genero codigo. El modo tradicional sigue intacto.\n",
                (int)rc);
        goto fin;
    }
    if (!resultado.cadena || !resultado.cadena->codificado) {
        fputs("--vliw-ir: sin bundles codificados\n", diag);
        goto fin;
    }
    if (op->detallado) {
        puts("[IR despues del register spilling]");
        if (ir_imprimir(&resultado.despues_spilling, stdout)) { estado=2; goto fin; }
    }
    printf("[VLIW IR] registros=%u, marco=%zu bytes, "
           "spills=%zu, loads=%zu, stores=%zu, bundles=%zu\n",
           op->vliw_regs, resultado.backend.marco.bytes_marco,
           resultado.metricas.ranuras, resultado.metricas.cargas,
           resultado.metricas.almacenes, resultado.cadena->codigo.cantidad);
    puts("[ADVERTENCIA] Fragmento fisico: sin arranque, retorno ni fin BAL. "
         "NO es un ejecutable autocontenido.");
    if (op->paquetes) {
        puts("[bundles VLIW / ensamblador]");
        if (fputs(resultado.cadena->asm_texto, stdout) == EOF ||
            cb_escribir_listado(&resultado.cadena->codigo, stdout)) {
            estado=2;
            goto fin;
        }
    }
    if (op->ensamblador) {
        char *asm_ruta = cambiar_extension(op->entrada, ".bal", ".vliw.asm");
        char *mem_ruta = cambiar_extension(op->entrada, ".bal", ".vliw.mem");
        if (!asm_ruta || !mem_ruta ||
            !strcmp(asm_ruta, op->entrada) || !strcmp(mem_ruta, op->entrada)) {
            fputs("--vliw-ir: nombres de salida invalidos\n", diag);
            estado=2;
        } else if (!escribir_archivo(asm_ruta, NULL, NULL,
                                     resultado.cadena->asm_texto, NULL) ||
                   !escribir_archivo(mem_ruta, &resultado.cadena->codigo,
                                     cb_escribir_mem, NULL, NULL)) {
            fputs("--vliw-ir: no se pudieron guardar los fragmentos\n", diag);
            estado=2;
        } else {
            printf("[VLIW IR] fragmentos %s y %s (no ejecutables)\n",
                   asm_ruta, mem_ruta);
        }
        free(asm_ruta);
        free(mem_ruta);
        if (estado == 2) goto fin;
    }
    estado = ferror(stdout) ? 2 : 0;
fin:
    ir_vliw_liberar(&resultado);
    ir_liberar(&ir);
    return estado;
}

/* Ejecuta la pasada de registros limitados solo para inspeccionar la IR.
 * No reemplaza la ruta AST -> ensamblador del compilador principal. */
static int visitante_ir_spilling(void *ctx, const Ast *arbol,
                                SimTabla *tabla, FILE *diag)
{
    (void)ctx;
    (void)tabla;
    IrPrograma original = {0};
    IrPrograma reescrita = {0};
    IrSpillingMetricas metricas = {0};
    int estado = ir_desde_ast(arbol, &original, diag);
    if (estado == 0)
        estado = ir_aplicar_spilling(&original, &reescrita, 3, &metricas);
    if (estado == 0) {
        puts("[IR con register spilling: 3 registros]");
        estado = ir_imprimir(&reescrita, stdout);
        if (estado == 0)
            printf("[spilling] ranuras=%zu cargas=%zu almacenes=%zu desalojos=%zu\n",
                   metricas.ranuras, metricas.cargas, metricas.almacenes,
                   metricas.desalojos);
    } else {
        fputs("No se pudo generar la IR con spilling\n", diag);
    }
    ir_liberar(&reescrita);
    ir_liberar(&original);
    return estado;
}

/* Identidad de una ruta antes de publicar: la ruta resuelta permite comparar
 * salidas nuevas; dispositivo/inodo reconoce también enlaces duros existentes.
 * Cada ruta pertenece a esta validación y se libera o transfiere al llamador. */
typedef struct {
    char *ruta;
    struct stat archivo;
    int existe;
} DestinoO2;

/* Resuelve el padre de una salida inexistente sin exigir realpath del archivo.
 * Los destinos existentes deben ser archivos regulares: un enlace colgante,
 * directorio o dispositivo no es un artefacto publicable. No crea archivos.
 * En éxito, el llamador posee destino->ruta; en error no conserva reservas. */
static int resolver_destino_o2(const char *ruta, int salida, DestinoO2 *destino)
{
    if (!ruta || !*ruta) return 0;
    struct stat enlace;
    if (lstat(ruta, &enlace) == 0) {
        if (stat(ruta, &destino->archivo) != 0 ||
            !S_ISREG(destino->archivo.st_mode)) return 0;
        destino->existe = 1;
        destino->ruta = realpath(ruta, NULL);
        if (!destino->ruta) return 0;
    } else {
        if (errno != ENOENT || !salida) return 0;
        char *copia = strdup(ruta);
        if (!copia) return 0;
        char *barra = strrchr(copia, '/');
        const char *nombre = barra ? barra + 1 : copia;
        char *padre;
        if (!*nombre) { free(copia); return 0; }
        if (barra) {
            *barra = '\0';
            padre = realpath(barra == copia ? "/" : copia, NULL);
        } else padre = realpath(".", NULL);
        if (!padre) { free(copia); return 0; }
        size_t a = strlen(padre), b = strlen(nombre);
        if (a > SIZE_MAX - b - 2) { free(padre); free(copia); return 0; }
        destino->ruta = malloc(a + b + 2);
        if (destino->ruta) snprintf(destino->ruta, a + b + 2, "%s/%s", padre, nombre);
        free(padre); free(copia);
        if (!destino->ruta) return 0;
    }
    if (salida) {
        /* Resolver el enlace final evita volver a seguirlo al publicar. El padre
         * debe existir y admitir escritura/búsqueda; los errores posteriores de
         * E/S siguen siendo posibles, sin prometer una transacción multiarquivo. */
        char *padre = strdup(destino->ruta);
        if (!padre) { free(destino->ruta); destino->ruta = NULL; return 0; }
        char *barra = strrchr(padre, '/');
        *barra = '\0';
        int valido = access(*padre ? padre : "/", W_OK | X_OK) == 0 &&
            (!destino->existe || access(destino->ruta, W_OK) == 0);
        free(padre);
        if (!valido) { free(destino->ruta); destino->ruta = NULL; return 0; }
    }
    return 1;
}

/* Contrasta todas las salidas y la fuente antes de cualquier fopen de salida.
 * Solo tras éxito transfiere las rutas canónicas al visitante; ante rechazo
 * deja sus cadenas intactas y libera lo adquirido. Los padres deben permanecer
 * estables durante la compilación: este preflight no bloquea el filesystem. */
static int validar_destinos_o2(const char *fuente, char **rutas, size_t cantidad, FILE *diag)
{
    DestinoO2 destinos[8] = {{0}};
    int valido = 0;
    if (cantidad != 7 || !resolver_destino_o2(fuente, 0, &destinos[0])) {
        fputs("O2: ruta fuente inválida para validar destinos\n", diag);
        goto fin;
    }
    for (size_t i = 0; i < cantidad; ++i) {
        if (!resolver_destino_o2(rutas[i], 1, &destinos[i + 1])) {
            fprintf(diag, "O2: destino inválido: %s\n", rutas[i] ? rutas[i] : "(sin memoria)");
            goto fin;
        }
        for (size_t j = 0; j <= i; ++j) {
            const DestinoO2 *a = &destinos[j], *b = &destinos[i + 1];
            if (!strcmp(a->ruta, b->ruta) ||
                (a->existe && b->existe && a->archivo.st_dev == b->archivo.st_dev &&
                 a->archivo.st_ino == b->archivo.st_ino)) {
                fprintf(diag, "O2: colisión de destinos: %s y %s\n", j ? rutas[j - 1] : fuente, rutas[i]);
                goto fin;
            }
        }
    }
    /* Ninguna salida ha sido abierta. Publicamos solo la colección completa
     * de nombres comprobados y liberamos las cadenas originales del visitante. */
    for (size_t i = 0; i < cantidad; ++i) {
        free(rutas[i]); rutas[i] = destinos[i + 1].ruta; destinos[i + 1].ruta = NULL;
    }
    valido = 1;
fin:
    for (size_t i = 0; i < 8; ++i) free(destinos[i].ruta);
    return valido;
}

/* O2 consume la IR transformada, no el AST original como backend de respaldo.
 * El resultado C es atómico; los archivos se crean solo después del enlace y
 * de todas las validaciones. Los módulos se rechazan antes de publicar. */
static int visitante_o2(void *ctx,const Ast *ast,SimTabla *tabla,FILE *diag)
{
    const Compilacion *op=ctx;MemProyectoMapa mapa={0};IrVliwPrograma r={0};
    int estado=1;
    for(const AstNodo *n=ast->raiz->primer_hijo;n;n=n->siguiente)
        if(n->tipo==AST_IMPORTACION) {fputs("O2: módulos sin enlace IR acreditado\n",diag);return 1;}
    if(mem_asignar_proyecto_referencias(tabla,CLI_GP_BASE,CLI_FIN_GLOBALES,CLI_TOPE_PILA-CLI_FIN_GLOBALES,&mapa)!=MEM_PROYECTO_OK) {
        fputs("O2: memoria/ABI no soportados para esta unidad\n",diag);return 1;
    }
    estado=ir_vliw_programa(ast,tabla,3,&r,diag);
    if(estado) {ir_vliw_programa_liberar(&r);return estado;}
    char *asm_ruta=cambiar_extension(op->entrada,".bal",".asm");
    char *bin=op->salida?cambiar_extension(op->salida,"",""):cambiar_extension(op->entrada,".bal",".bin");
    char *mem=bin?cambiar_extension(bin,".bin",".mem"):NULL;
    char *meta=cambiar_extension(op->entrada,".bal",".o2.json");
    char *antes=cambiar_extension(op->entrada,".bal",".o2-antes.ir");
    char *despues=cambiar_extension(op->entrada,".bal",".o2-despues.ir");
    char *spill=cambiar_extension(op->entrada,".bal",".o2-spilling.ir");
    char *salidas[7]={asm_ruta,bin,mem,meta,antes,despues,spill};
    if(!validar_destinos_o2(op->entrada,salidas,7,diag))estado=2;
    asm_ruta=salidas[0];bin=salidas[1];mem=salidas[2];meta=salidas[3];
    antes=salidas[4];despues=salidas[5];spill=salidas[6];
    FbDatos datos={0};datos.base=CLI_GP_BASE;
    if(!estado && (!escribir_archivo(asm_ruta,NULL,NULL,r.cadena->asm_texto,NULL)||!escribir_archivo(bin,&r.cadena->codigo,NULL,NULL,&datos)||!escribir_archivo(mem,&r.cadena->codigo,cb_escribir_mem,NULL,NULL)||!escribir_archivo(meta,NULL,NULL,r.reporte_json,NULL)))estado=2;
    const char *rutas[3]={antes,despues,spill};const IrPrograma *irs[3]={&r.original,&r.optimizada,&r.spilling};
    for(unsigned k=0;!estado && k<3;++k) {
        FILE *f=fopen(rutas[k],"w");if(!f){estado=2;break;}
        int io=ir_imprimir(irs[k],f);if(fclose(f))io=1;if(io)estado=2;
    }
    if(!estado)fprintf(diag,"[O2] DCE=%zu unroll=%zu rename=%zu reorder=%zu spills=%zu loads=%zu stores=%zu bundles=%zu generaciones=%zu\n",r.eliminadas,r.desenrollados,r.renombradas,r.movidas,r.metricas.ranuras,r.metricas.cargas,r.metricas.almacenes,r.cadena->codigo.cantidad,r.generaciones);
    if(!estado && op->paquetes)fputs(r.cadena->visualizacion_json,stdout);
    if(!estado && op->simbolos)imprimir_simbolos(tabla,&mapa,NULL);
    free(asm_ruta);free(bin);free(mem);free(meta);free(antes);free(despues);free(spill);ir_vliw_programa_liberar(&r);return estado;
}

int cli_run(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "--clones")) return buscar_clones(argc, argv);
    Compilacion op = {0};
    int parsed = parse_compilacion(argc, argv, &op);
    if (parsed != 0) return parsed == 1 ? 0 : 2;
    if (op.ast_json) return exportar_ast_json(op.entrada);
    if (op.vliw_ir) {
        if (!termina_en(op.entrada, ".bal")) {
            fputs("--vliw-ir necesita un archivo fuente .bal\n", stderr);
            return 2;
        }
        if ((op.detallado || op.ast) && frontend(&op)) return 1;
        return sem_visitar_proyecto(op.entrada, stderr, visitante_vliw_ir, &op);
    }
    if (op.ir_spilling)
        return sem_visitar_proyecto(op.entrada, stderr, visitante_ir_spilling, NULL);
    if (op.ir) return sem_visitar_proyecto(op.entrada, stderr, visitante_ir, &op);
    if (op.diagnosticos) return exportar_diagnosticos(op.entrada);
    if (op.nivel==2) {
        if(!termina_en(op.entrada,".bal")) {fputs("-O2 requiere fuente .bal\n",stderr);return 2;}
        return sem_visitar_proyecto(op.entrada,stderr,visitante_o2,&op);
    }
    if (op.nivel==1) {
        if (!termina_en(op.entrada,".bal")) {
            fputs("-O1 requiere un fuente .bal\n",stderr);return 2;
        }
        VliwOptFuente fuente={0};
        int estado=vliw_opt_fuente_preparar(op.entrada,&fuente,stderr);
        if (!estado) {
            fprintf(stderr,"[O1] DCE: IR eliminadas=%zu; BAL borradas=%zu cambiadas=%zu conservadas=%zu; cambio=%d\n",
                fuente.eliminadas_ir,fuente.borradas_bal,fuente.cambiadas_bal,fuente.conservadas_bal,fuente.cambio_bal);
            op.fuente_optimizada=fuente.ruta;
            /* El backend reconstruye memoria, símbolos, enlace y CB desde el
             * BAL elegido; los nombres de salida conservan la entrada original. */
            estado=compilar(&op);
        }
        vliw_opt_fuente_liberar(&fuente);return estado;
    }
    return compilar(&op);
}

int cli_frontend(int argc, char **argv) {
    Options opt = {0};
    int parsed = parse_args(argc, argv, &opt);
    if (parsed != 0) return parsed == 1 ? 0 : 2;

    size_t size = 0;
    char *source = read_source(opt.input, &size);
    if (!source) return 2;

    /*Se agrega la condicion para que si se pidio analizar sintaxis y no verbose, se ejecute el analisis sintactico*/
    if (opt.syntax && !opt.verbose) {
        int estado = mostrar_ast(source, size, opt.input);
        free(source);
        return estado;
    }
    Lexer lexer;
    lexer_init(&lexer, source, size, opt.input);
    if (opt.verbose) fprintf(stdout, "[léxico] Analizando %s\n", opt.input);
    size_t count = 0, errors = 0;
    for (;;) {
        Token t = lexer_next(&lexer);
        if (t.error == TOKEN_OUT_OF_MEMORY) {
            fprintf(stderr, "%s:%zu:%zu: memoria insuficiente\n",
                    opt.input, t.line, t.column);
            token_dispose(&t);
            free(source);
            return 2;
        }
        if (t.type == TOKEN_ERROR) {
            fprintf(stderr, "%s:%zu:%zu: error léxico: %s\n",
                    opt.input, t.line, t.column, token_error_message(t.error));
            ++errors;
        }
        if (opt.verbose) {
            printf("%zu:%zu %-23s ", t.line, t.column, token_type_name(t.type));
            print_escaped(&t);
            putchar('\n');
        }
        TokenType kind = t.type;
        if (kind != TOKEN_EOF) ++count;
        token_dispose(&t);
        if (kind == TOKEN_EOF) break;
    }
    if (opt.verbose)
        printf("[léxico] %zu tokens, %zu errores.\n",
               count, errors);
    else if (errors == 0 && !opt.syntax)
        fputs("Análisis léxico correcto (modo --frontend; no se generó binario).\n",
              stderr);

    /*Se agrega la condicion para que si se pidio analizar sintaxis y no hubo errores léxicos, se ejecute el analisis sintactico*/
    int estado = errors ? 1 : 0;
    if (opt.syntax && errors == 0) {
        puts("[sintáctico] Construyendo AST");
        estado = mostrar_ast(source, size, opt.input);
    }
    if (opt.verbose && !opt.syntax && errors == 0)
        puts("[estado] Solo análisis léxico; no se generó ensamblador ni binario.");
    free(source);
    return estado;
}
