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

/* Mapa de memoria del contrato: globales desde 0x4000 hasta 0x7FFF y pila
 * desde 0x10000 hacia abajo (0x8000-0xFFFF). El codigo empieza en 0. */
#define CLI_GP_BASE 0x4000u
#define CLI_FIN_GLOBALES 0x8000u
#define CLI_TOPE_PILA 0x10000u

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
          "-v            muestra cada fase del compilador y las decisiones importantes\n"
          "-s            genera tambien el ensamblador (.asm)\n"
          "-t            imprime el arbol sintactico (AST)\n"
          "-m            imprime la tabla de simbolos y genera <fuente>.simbolos.md\n"
          "-x            toma un archivo de ensamblador y genera el binario\n"
          "-p            muestra los paquetes generados en ensamblador y en binario\n"
          "--ast-json    exporta solamente el AST como JSON por stdout\n"
          "--diagnosticos  revisa el fuente y escribe sus errores como JSON por\n"
          "              stdout; no genera ningun archivo\n", out);
}

static int parse_compilacion(int argc, char **argv, Compilacion *c)
{
    int posicional = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!posicional && !strcmp(arg, "--ast-json")) { c->ast_json = 1; continue; }
        if (!posicional && !strcmp(arg, "--diagnosticos")) { c->diagnosticos = 1; continue; }
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
    MemProyectoResultado r = mem_asignar_proyecto(tabla, CLI_GP_BASE, CLI_FIN_GLOBALES,
                                                  1020, &mapa);
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
    int ok = ef_enlazar(arbol, tabla, 0, &arranque, &funciones, temporal,
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
    char *fuente = read_source(op->entrada, &largo);
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
            estado = sem_visitar_proyecto(op->entrada, stderr, compilar_proyecto, &datos);
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

int cli_run(int argc, char **argv) {
    Compilacion op = {0};
    int parsed = parse_compilacion(argc, argv, &op);
    if (parsed != 0) return parsed == 1 ? 0 : 2;
    if (op.ast_json) return exportar_ast_json(op.entrada);
    if (op.diagnosticos) return exportar_diagnosticos(op.entrada);
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
