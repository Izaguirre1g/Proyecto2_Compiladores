/*
 * Niveles de optimización -O0 y -O2 (ver include/ide_optimizacion.h).
 *
 * Pasos de ide_optimizacion_analizar:
 *   1. -O0: el botón Ensamblador (ide_ensamblador_generar), que hace lo
 *      mismo que "balc -O0": ensamblador, .bin y las variables de main.
 *   1b. -O1: el DCE (ide_dce_analizar) arma el código BAL sin el código
 *      muerto y ese código se compila igual que -O0 (lo mismo que "balc -O1").
 *   2. -O2: lo mismo que visitante_o2 de src/Linea_Comandos/cli.c ("balc
 *      -O2"): asignar la memoria con referencias y llamar a ir_vliw_programa,
 *      que aplica las pasadas sobre la IR y arma la cadena física VLIW.
 *   3. Cada nivel se compila MET_COMPILACIONES veces para el tiempo de
 *      compilación (la mediana, como en las Métricas).
 *   4. Cada binario se ejecuta en el simulador hasta que main termina
 *      (met_simular) y se guarda la memoria de datos de ese momento.
 *   5. Se leen las variables de main en esa memoria y se comparan.
 */
/* clock_gettime y CLOCK_MONOTONIC son de POSIX, no de C11. */
#define _POSIX_C_SOURCE 200809L

#include "ide_optimizacion.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ast.h"
#include "cli.h"
#include "formato_binario.h"
#include "ide_dce.h"
#include "ide_analisis.h"
#include "ir.h"
#include "ir_vliw_programa.h"
#include "memory.h"
#include "semantic_modules.h"

/* Registros temporales de -O2: los mismos 3 que usa "balc -O2". */
#define O2_REGISTROS 3

/* El ciclo final de -O2: después de que main regresa a pc_fin, el PC
 * recorre pc_fin ... pc_fin + 64 (5 bundles) y vuelve a pc_fin. */
#define O2_ANCHO_FIN 64u

/* La hora en milisegundos según el reloj monótono (solo para restar). */
static double ahora_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0.0;
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

/* Copia una cadena (NULL si falta memoria o si s es NULL). */
static char *copiar(const char *s)
{
    if (s == NULL) return NULL;
    char *c = malloc(strlen(s) + 1);
    if (c != NULL) strcpy(c, s);
    return c;
}

/* Lee todo lo escrito en un archivo temporal (tmpfile) como una cadena. */
static char *leer_todo(FILE *f)
{
    if (f == NULL || fflush(f) != 0 || fseek(f, 0, SEEK_END) != 0) return NULL;
    long largo = ftell(f);
    if (largo < 0 || fseek(f, 0, SEEK_SET) != 0) return NULL;
    char *texto = malloc((size_t)largo + 1);
    if (texto == NULL) return NULL;
    size_t leidos = fread(texto, 1, (size_t)largo, f);
    texto[leidos] = '\0';
    return texto;
}

/* El primer mensaje de error del texto. Se saltan los avisos informativos,
 * que empiezan con "[": "[OK] ..." del semántico y "[unroll] ciclo ... ->
 * desenrollado TOTAL" del unrolling, que no son errores. */
static char *primer_error(const char *texto)
{
    for (const char *linea = texto; linea != NULL && *linea != '\0';) {
        size_t largo = strcspn(linea, "\n");
        if (largo > 0 && linea[0] != '[') {
            char *copia = malloc(largo + 1);
            if (copia != NULL) {
                memcpy(copia, linea, largo);
                copia[largo] = '\0';
            }
            return copia;
        }
        linea += largo;
        if (*linea == '\n') linea++;
    }
    return NULL;
}

/*
 * Cuántas instrucciones tiene un ensamblador VLIW sin contar los NOP. Cada
 * línea que no es comentario ni etiqueta es un bundle con sus slots
 * separados por '|'; se cuenta cada slot que no es "nop":
 *     sumai x3,x0,0 | nop | nop | nop     -> 1
 *     nop                                 -> 0 (bundle vacío)
 *     __vliw_b1:                          -> etiqueta, no es un bundle
 */
static size_t contar_instrucciones(const char *ensamblador)
{
    size_t total = 0;
    for (const char *linea = ensamblador; linea != NULL && *linea != '\0';) {
        size_t largo = strcspn(linea, "\n");
        size_t codigo = strcspn(linea, "#\n");       /* Sin el comentario del final. */
        size_t inicio = strspn(linea, " \t");
        int etiqueta = codigo > 0 && linea[codigo - 1] == ':';
        if (inicio < codigo && !etiqueta) {
            /* Recorre los slots: [inicio, codigo) separados por '|'. */
            size_t a = inicio;
            while (a < codigo) {
                size_t b = a;
                while (b < codigo && linea[b] != '|') b++;
                size_t i = a, j = b;
                while (i < j && (linea[i] == ' ' || linea[i] == '\t')) i++;
                while (j > i && (linea[j - 1] == ' ' || linea[j - 1] == '\t')) j--;
                if (j > i && !(j - i == 3 && strncmp(linea + i, "nop", 3) == 0)) total++;
                a = b + 1;
            }
        }
        linea += largo;
        if (*linea == '\n') linea++;
    }
    return total;
}

/* Imprime una IR en un texto propio (NULL si falló). */
static char *texto_de_ir(const IrPrograma *ir)
{
    FILE *f = tmpfile();
    if (f == NULL) return NULL;
    char *texto = ir_imprimir(ir, f) == 0 ? leer_todo(f) : NULL;
    fclose(f);
    return texto;
}

/*
 * El marco de main en -O2, del reporte JSON de ir_vliw_programa:
 *   "funciones":[{"nombre":"main","marco":184,...}]
 * Es distinto del de -O0: además de las variables reserva las ranuras del
 * spilling y los registros que salva. 0 si no aparece.
 */
static size_t marco_main_o2(const char *reporte)
{
    const char *clave = "\"nombre\":\"main\",\"marco\":";
    const char *p = reporte != NULL ? strstr(reporte, clave) : NULL;
    return p != NULL ? (size_t)strtoul(p + strlen(clave), NULL, 10) : 0;
}

/* ------------------------------------------------------------------ */
/* -O2: compilar con ir_vliw_programa                                  */
/* ------------------------------------------------------------------ */

/* Lo que deja una compilación con -O2. */
typedef struct {
    int llego;                 /* 1 si el semántico aceptó el programa. */
    int compilado;             /* 1 si ir_vliw_programa generó el programa. */
    int sin_memoria;
    char *motivo;              /* Si no compiló: por qué. */
    char *ensamblador;
    unsigned char *balb;       /* El .bin (BALB). */
    size_t largo_balb;
    size_t bundles;
    uint32_t pc_fin;           /* Dirección a la que regresa main (fin del programa). */
    size_t marco_main;
    IdeVarMain *variables;
    size_t cantidad_variables;
    size_t dce, unroll, renombradas, reordenadas, spills, cargas, guardados, generaciones;
    size_t war, waw;
    size_t ir_antes_cant, ir_despues_cant, ir_spilling_cant;   /* Instrucciones de cada IR. */
    char *ir_antes, *ir_despues, *ir_spilling;
    double entrada_ms, salida_ms;   /* Para el tiempo de cada fase. */
} CompilacionO2;

static void liberar_o2(CompilacionO2 *c)
{
    free(c->motivo);
    free(c->ensamblador);
    free(c->balb);
    free(c->variables);
    free(c->ir_antes);
    free(c->ir_despues);
    free(c->ir_spilling);
    memset(c, 0, sizeof *c);
}

/* Lee los bytes de un archivo temporal (pueden tener ceros). */
static unsigned char *leer_bytes(FILE *f, size_t *largo)
{
    if (f == NULL || fflush(f) != 0 || fseek(f, 0, SEEK_END) != 0) return NULL;
    long total = ftell(f);
    if (total <= 0 || fseek(f, 0, SEEK_SET) != 0) return NULL;
    unsigned char *bytes = malloc((size_t)total);
    if (bytes == NULL) return NULL;
    if (fread(bytes, 1, (size_t)total, f) != (size_t)total) {
        free(bytes);
        return NULL;
    }
    *largo = (size_t)total;
    return bytes;
}

/*
 * Lo llama el semántico cuando el programa es válido. Son los mismos pasos
 * que visitante_o2 de cli.c, pero en memoria en vez de escribir archivos.
 * Devuelve 0 si generó el programa, 1 si -O2 no lo soporta (motivo en diag),
 * 2 si faltó memoria.
 */
static int visitante_o2(void *datos, const Ast *arbol, SimTabla *tabla, FILE *diag)
{
    CompilacionO2 *c = datos;
    c->llego = 1;
    c->entrada_ms = ahora_ms();

    /* Igual que balc -O2: los módulos ("bring") todavía no tienen enlace por IR. */
    for (const AstNodo *n = arbol->raiz->primer_hijo; n != NULL; n = n->siguiente) {
        if (n->tipo == AST_IMPORTACION) {
            fputs("-O2 todavía no enlaza módulos importados con bring\n", diag);
            c->salida_ms = ahora_ms();
            return 1;
        }
    }
    MemProyectoMapa mapa = {0};
    if (mem_asignar_proyecto_referencias(tabla, CLI_GP_BASE, CLI_FIN_GLOBALES,
                                         CLI_TOPE_PILA - CLI_FIN_GLOBALES, &mapa) != MEM_PROYECTO_OK) {
        fputs("-O2 no soporta la memoria de este programa (memoria/ABI)\n", diag);
        c->salida_ms = ahora_ms();
        return 1;
    }

    IrVliwPrograma r = {0};
    int estado = ir_vliw_programa(arbol, tabla, O2_REGISTROS, &r, diag);
    if (estado == 0 && (r.cadena == NULL || r.cadena->asm_texto == NULL)) estado = 1;
    c->salida_ms = ahora_ms();   /* Fin de la generación (pasadas + cadena física). */
    if (estado == 0) {
        c->compilado = 1;
        c->ensamblador = copiar(r.cadena->asm_texto);
        c->bundles = r.cadena->codigo.cantidad;
        c->pc_fin = (uint32_t)r.pc_fin;
        c->marco_main = marco_main_o2(r.reporte_json);
        c->dce = r.eliminadas;
        c->unroll = r.desenrollados;
        c->renombradas = r.renombradas;
        c->war = r.war_eliminadas;
        c->waw = r.waw_eliminadas;
        c->reordenadas = r.movidas;
        c->spills = r.metricas.ranuras;
        c->cargas = r.metricas.cargas;
        c->guardados = r.metricas.almacenes;
        c->generaciones = r.generaciones;
        c->ir_antes_cant = r.original.cantidad;
        c->ir_despues_cant = r.optimizada.cantidad;
        c->ir_spilling_cant = r.spilling.cantidad;
        c->ir_antes = texto_de_ir(&r.original);
        c->ir_despues = texto_de_ir(&r.optimizada);
        c->ir_spilling = texto_de_ir(&r.spilling);

        /* El .bin, como balc -O2: las globales las inicializa el propio
         * código (__o2_init_global), así que no lleva datos aparte. */
        FbDatos *fb = calloc(1, sizeof *fb);
        FILE *f = fb != NULL ? tmpfile() : NULL;
        if (f != NULL) {
            fb->base = CLI_GP_BASE;
            if (fb_escribir(&r.cadena->codigo, fb, f) == 0) c->balb = leer_bytes(f, &c->largo_balb);
            fclose(f);
        }
        free(fb);
        if (ide_variables_de_main(tabla, &c->variables, &c->cantidad_variables) != 0 ||
            c->ensamblador == NULL || c->balb == NULL || c->ir_antes == NULL ||
            c->ir_despues == NULL || c->ir_spilling == NULL) {
            c->sin_memoria = 1;
            estado = 2;
        }
    }
    ir_vliw_programa_liberar(&r);
    return estado;
}

/* Compila el texto con -O2. 0 si se pudo intentar, -1 si falló algo interno. */
static int compilar_o2(const char *texto, size_t largo, const char *ruta, CompilacionO2 *c,
                       IdeAsmTiempos *t)
{
    memset(c, 0, sizeof *c);
    memset(t, 0, sizeof *t);
    const double inicio_ms = ahora_ms();
    char *temporal = ide_ruta_temporal(ruta);
    if (temporal == NULL) return -1;
    if (ide_escribir_archivo(temporal, texto, largo) != 0) {
        free(temporal);
        return -1;
    }
    int estado = -1;
    FILE *diag = tmpfile();
    if (diag != NULL) {
        const double analisis_ms = ahora_ms();
        int resultado = sem_visitar_proyecto(temporal, diag, visitante_o2, c);
        t->analisis = (c->llego ? c->entrada_ms : ahora_ms()) - analisis_ms;
        if (c->llego) t->generacion = c->salida_ms - c->entrada_ms;
        if (c->sin_memoria) {
            estado = -1;
        } else {
            estado = 0;
            if (c->llego && (resultado != 0 || !c->compilado)) {
                c->compilado = 0;
                char *mensajes = leer_todo(diag);
                c->motivo = primer_error(mensajes);
                free(mensajes);
                if (c->motivo == NULL) c->motivo = copiar("-O2 no pudo generar este programa");
                if (c->motivo == NULL) estado = -1;
            }
        }
        fclose(diag);
    }
    remove(temporal);
    free(temporal);
    t->total = ahora_ms() - inicio_ms;
    /* En -O2 la codificación (CB) ocurre dentro de la cadena física: queda
     * en "generacion". El .bin se arma aparte, pero es muy poco. */
    if (estado != 0) liberar_o2(c);
    return estado;
}

/* ------------------------------------------------------------------ */
/* -O1: el DCE sobre el código BAL                                     */
/* ------------------------------------------------------------------ */

/* Lo que deja el DCE de -O1. */
typedef struct {
    int listo;                 /* 1 si hay un código BAL para compilar. */
    char *motivo;              /* Si listo es 0: por qué no. */
    char *codigo;              /* El código que se compila (el nuevo o, sin cambios, el original). */
    size_t eliminadas, borradas, cambiadas, conservadas;
    size_t ir_antes, ir_despues;   /* Instrucciones de la IR antes y después del DCE. */
    int cambio;                /* 1 si "codigo" es distinto del original. */
} PreparacionO1;

static void liberar_o1(PreparacionO1 *p)
{
    free(p->motivo);
    free(p->codigo);
    memset(p, 0, sizeof *p);
}

/*
 * Los mismos pasos que vliw_opt_fuente_preparar (tools/vliw/optimizacion_cli.c,
 * "balc -O1"): el DCE sobre la IR y el código BAL sin el código muerto. Si
 * el DCE quita algo de la IR pero no pudo reescribir el código, -O1 no se
 * hace (no se compila el original como si estuviera optimizado).
 * "dce_ms" recibe cuánto tardó el DCE. 0 si se pudo intentar, -1 interno.
 */
static int preparar_o1(const char *texto, size_t largo, const char *ruta, PreparacionO1 *p,
                       double *dce_ms)
{
    memset(p, 0, sizeof *p);
    const double inicio = ahora_ms();
    IdeDce dce;
    if (ide_dce_analizar(texto, largo, ruta, &dce) != 0) return -1;
    *dce_ms = ahora_ms() - inicio;
    int estado = 0;
    if (dce.estado != IDE_DCE_LISTO) {
        char t[600];
        snprintf(t, sizeof t, "-O1 necesita la IR, y la IR todavía no soporta este programa%s%s",
                 dce.motivo ? ": " : "", dce.motivo ? dce.motivo : "");
        p->motivo = copiar(t);
    } else if (dce.resultado.cantidad_eliminadas > 0 && dce.codigo_nuevo == NULL) {
        p->motivo = copiar("el DCE quitó instrucciones de la IR, pero no pudo reescribir el "
                           "código BAL sin ellas (igual que balc -O1, no se compila)");
    } else {
        p->listo = 1;
        p->codigo = dce.codigo_nuevo != NULL ? copiar(dce.codigo_nuevo) : copiar(texto);
        p->eliminadas = dce.resultado.cantidad_eliminadas;
        p->ir_antes = dce.resultado.instrucciones_antes;
        p->ir_despues = dce.resultado.instrucciones_despues;
        p->borradas = dce.sentencias_borradas;
        p->cambiadas = dce.sentencias_cambiadas;
        p->conservadas = dce.sentencias_conservadas;
        p->cambio = dce.codigo_nuevo != NULL && strcmp(dce.codigo_nuevo, texto) != 0;
        if (p->codigo == NULL) estado = -1;
    }
    if (!p->listo && p->motivo == NULL) estado = -1;
    ide_dce_liberar(&dce);
    if (estado != 0) liberar_o1(p);
    return estado;
}

/* Compila con -O1 solo para medir el tiempo: el DCE más el backend. El
 * tiempo del DCE se suma a la "generación" (es la pasada de optimización). */
static int tiempo_o1(const char *texto, size_t largo, const char *ruta, IdeAsmTiempos *t)
{
    PreparacionO1 p;
    double dce_ms = 0.0;
    if (preparar_o1(texto, largo, ruta, &p, &dce_ms) != 0 || !p.listo) {
        liberar_o1(&p);
        return -1;
    }
    IdeEnsamblador a;
    int estado = ide_ensamblador_generar(p.codigo, strlen(p.codigo), ruta, &a);
    if (estado == 0) {
        *t = a.tiempos;
        t->generacion += dce_ms;
        t->total += dce_ms;
        ide_ensamblador_liberar(&a);
    }
    liberar_o1(&p);
    return estado;
}

/* ------------------------------------------------------------------ */
/* Tiempo de compilación: la mediana                                   */
/* ------------------------------------------------------------------ */

static int comparar_tiempos(const void *a, const void *b)
{
    double x = ((const IdeAsmTiempos *)a)->total, y = ((const IdeAsmTiempos *)b)->total;
    return (x > y) - (x < y);
}

/* Compila MET_COMPILACIONES - 1 veces más con el nivel pedido (0, 1 o 2)
 * y deja la corrida mediana (por el total). "primera" es la que ya se hizo.
 * "texto" es siempre el código del editor (en -O1 se repite también el DCE). */
static int mediana(const char *texto, size_t largo, const char *ruta, int nivel,
                   const IdeAsmTiempos *primera, IdeAsmTiempos *salida)
{
    IdeAsmTiempos corridas[MET_COMPILACIONES];
    corridas[0] = *primera;
    for (int i = 1; i < MET_COMPILACIONES; ++i) {
        if (nivel == 0) {
            IdeEnsamblador a;
            if (ide_ensamblador_generar(texto, largo, ruta, &a) != 0) return -1;
            corridas[i] = a.tiempos;
            ide_ensamblador_liberar(&a);
        } else if (nivel == 1) {
            if (tiempo_o1(texto, largo, ruta, &corridas[i]) != 0) return -1;
        } else {
            CompilacionO2 c;
            if (compilar_o2(texto, largo, ruta, &c, &corridas[i]) != 0) return -1;
            liberar_o2(&c);
        }
    }
    qsort(corridas, MET_COMPILACIONES, sizeof corridas[0], comparar_tiempos);
    *salida = corridas[MET_COMPILACIONES / 2];
    return 0;
}

/* ------------------------------------------------------------------ */
/* El resultado de main                                                */
/* ------------------------------------------------------------------ */

/* Un elemento de una variable, como texto, leído de la memoria. */
static void escribir_elemento(const uint8_t *memoria, size_t direccion, const IdeVarMain *v,
                              char *texto, size_t largo)
{
    if (direccion + v->bytes_elemento > MET_BYTES_MEMORIA) {
        snprintf(texto, largo, "?");
        return;
    }
    if (v->bytes_elemento == 1) {
        unsigned char ch = memoria[direccion];
        if (v->es_char && ch >= 32 && ch < 127) snprintf(texto, largo, "'%c'", ch);
        else snprintf(texto, largo, "%u", (unsigned)ch);
        return;
    }
    /* 4 bytes, little endian (el orden del procesador). */
    int32_t valor = (int32_t)((uint32_t)memoria[direccion] | (uint32_t)memoria[direccion + 1] << 8 |
                              (uint32_t)memoria[direccion + 2] << 16 | (uint32_t)memoria[direccion + 3] << 24);
    if (v->es_boolean) snprintf(texto, largo, "%s", valor ? "declare_true" : "declare_false");
    else snprintf(texto, largo, "%ld", (long)valor);
}

/* Máximo de elementos de un arreglo que se muestran. */
#define MAX_ELEMENTOS 16

/*
 * El valor final de una variable de main como texto: "120" o "[16, 15]".
 * Su dirección es el tope de la pila menos el marco de main más su
 * desplazamiento (main es la primera función: su marco está arriba).
 */
static char *valor_de(const uint8_t *memoria, size_t marco, const IdeVarMain *v)
{
    char texto[MAX_ELEMENTOS * 24 + 16] = "";
    size_t base = (size_t)CLI_TOPE_PILA - marco + (size_t)v->desplazamiento;
    if (v->elementos > 1) strcat(texto, "[");
    for (size_t i = 0; i < v->elementos && i < MAX_ELEMENTOS; ++i) {
        char elemento[24];
        escribir_elemento(memoria, base + i * v->bytes_elemento, v, elemento, sizeof elemento);
        if (i > 0) strcat(texto, ", ");
        strcat(texto, elemento);
    }
    if (v->elementos > MAX_ELEMENTOS) strcat(texto, ", …");
    if (v->elementos > 1) strcat(texto, "]");
    return copiar(texto);
}

/* Lee los valores de todas las variables de main de un nivel. */
static int leer_valores(IdeOptNivel *n, const uint8_t *memoria)
{
    if (n->cantidad_variables == 0) return 0;
    n->valores = calloc(n->cantidad_variables, sizeof *n->valores);
    if (n->valores == NULL) return -1;
    for (size_t i = 0; i < n->cantidad_variables; ++i) {
        n->valores[i] = valor_de(memoria, n->marco_main, &n->variables[i]);
        if (n->valores[i] == NULL) return -1;
    }
    return 0;
}

/* Ejecuta el binario del nivel y lee el resultado de main. "ancho": bytes
 * que recorre el ciclo final después de "fin" (ver MetMedicion.ancho_fin). */
static int simular(IdeOptNivel *n, const unsigned char *balb, size_t largo, uint32_t fin,
                   uint32_t ancho, const char *ruta)
{
    uint8_t *memoria = calloc(1, MET_BYTES_MEMORIA);
    if (memoria == NULL) return -1;
    n->simulacion.direccion_fin = fin;
    n->simulacion.ancho_fin = ancho;
    n->simulacion.bundles = n->bundles;
    n->simulacion.compilacion = n->tiempos;
    int estado = met_simular(balb, largo, ruta, memoria, &n->simulacion);
    if (estado == 0 && n->simulacion.estado == MET_LISTO && n->marco_main > 0)
        estado = leer_valores(n, memoria);
    free(memoria);
    return estado;
}

/*
 * Compara el resultado de main de un nivel con el de -O0, variable por
 * variable y POR NOMBRE (si un nombre se repite en bloques distintos, cuenta
 * el orden en que aparece). Una variable que el otro nivel no tiene no es
 * una diferencia: -O1 borra del código las variables muertas de main (nadie
 * las lee, no son resultado). 1 igual, 0 alguna distinta, -1 no se pudo
 * (alguno de los dos niveles no compiló o main no terminó).
 */
static int comparar_resultados(const IdeOptNivel *a, const IdeOptNivel *b)
{
    /* Solo se puede comparar si los dos niveles compilaron y main terminó. */
    if (!a->compilado || !b->compilado || a->simulacion.estado != MET_LISTO ||
        b->simulacion.estado != MET_LISTO) return -1;
    if (a->cantidad_variables == 0) return 1;   /* main no tiene variables: nada que comparar. */
    if (a->valores == NULL || b->valores == NULL) return 0;  /* Al optimizado le faltan todas. */
    for (size_t i = 0; i < a->cantidad_variables; ++i) {
        /* Cuántas veces apareció este nombre antes en "a" (variables de
         * bloques distintos pueden llamarse igual). */
        size_t repeticion = 0;
        for (size_t k = 0; k < i; ++k)
            repeticion += strcmp(a->variables[k].nombre, a->variables[i].nombre) == 0;
        int encontrada = 0;
        for (size_t j = 0; j < b->cantidad_variables && !encontrada; ++j) {
            if (strcmp(b->variables[j].nombre, a->variables[i].nombre) != 0) continue;
            if (repeticion-- > 0) continue;
            if (strcmp(a->valores[i], b->valores[j]) != 0) return 0;
            encontrada = 1;
        }
        /* Una variable de main que el optimizado ya no tiene ES una
         * diferencia: las variables de main son el resultado del programa
         * (BAL no tiene print), y ningún nivel puede perder una. */
        if (!encontrada) return 0;
    }
    return 1;
}

/*
 * Un nivel que usa el backend tradicional (-O0 y -O1): compila "codigo",
 * toma la mediana del tiempo (repitiendo el nivel "nivel" sobre el código
 * del editor) y lo ejecuta en el simulador. "primera_extra_ms" se suma al
 * tiempo de la primera compilación (el DCE en -O1). 0 bien, -1 interno.
 */
static int nivel_tradicional(IdeOptNivel *n, const char *codigo, const char *texto, size_t largo,
                             const char *ruta, int nivel, double primera_extra_ms, int *con_errores)
{
    IdeEnsamblador a;
    if (ide_ensamblador_generar(codigo, strlen(codigo), ruta, &a) != 0) return -1;
    int estado = 0;
    if (a.estado == IDE_ASM_CON_ERRORES) {
        if (con_errores != NULL) *con_errores = 1;
        n->motivo = copiar("el código tiene errores");
        if (n->motivo == NULL) estado = -1;
    } else if (a.estado == IDE_ASM_SIN_BACKEND) {
        n->motivo = copiar(a.motivo != NULL ? a.motivo : "el backend no pudo generar el programa");
        if (n->motivo == NULL) estado = -1;
    } else {
        uint32_t fin = 0;
        n->compilado = 1;
        n->ensamblador = a.ensamblador;      /* Se adueña de los textos de "a". */
        a.ensamblador = NULL;
        n->bundles = a.bundles;
        n->instrucciones = contar_instrucciones(n->ensamblador);
        n->marco_main = a.marco_main;
        n->variables = a.variables;
        n->cantidad_variables = a.cantidad_variables;
        a.variables = NULL;
        IdeAsmTiempos primera = a.tiempos;
        primera.generacion += primera_extra_ms;
        primera.total += primera_extra_ms;
        if (mediana(texto, largo, ruta, nivel, &primera, &n->tiempos) != 0 ||
            met_buscar_fin(n->ensamblador, &fin) != 0 ||
            simular(n, a.balb, a.largo_balb, fin, MET_ANCHO_FIN_O0, ruta) != 0) estado = -1;
    }
    ide_ensamblador_liberar(&a);
    return estado;
}

/* ------------------------------------------------------------------ */
/* Todo junto                                                          */
/* ------------------------------------------------------------------ */

int ide_optimizacion_analizar(const char *texto, size_t largo, const char *ruta,
                              IdeOptimizacion *r)
{
    memset(r, 0, sizeof *r);
    r->mismo_o1 = r->mismo_o2 = r->mismo_resultado = -1;

    /* Paso 1: -O0. Si el código tiene errores, no se sigue con ningún nivel. */
    int con_errores = 0;
    int estado = nivel_tradicional(&r->o0, texto, texto, largo, ruta, 0, 0.0, &con_errores);
    if (estado == 0 && con_errores) {
        ide_optimizacion_liberar(r);
        r->estado = IDE_OPT_CON_ERRORES;
        return 0;
    }

    /* Paso 1b: -O1 = DCE + el código BAL nuevo con el backend tradicional. */
    if (estado == 0) {
        PreparacionO1 p;
        double dce_ms = 0.0;
        if (preparar_o1(texto, largo, ruta, &p, &dce_ms) != 0) {
            estado = -1;
        } else if (!p.listo) {
            r->o1.motivo = p.motivo;
            p.motivo = NULL;
        } else {
            r->o1_eliminadas_ir = p.eliminadas;
            r->o1_borradas = p.borradas;
            r->o1_cambiadas = p.cambiadas;
            r->o1_conservadas = p.conservadas;
            r->o1_ir_antes = p.ir_antes;
            r->o1_ir_despues = p.ir_despues;
            r->o1_cambio = p.cambio;
            r->o1_codigo = p.codigo;
            p.codigo = NULL;
            estado = nivel_tradicional(&r->o1, r->o1_codigo, texto, largo, ruta, 1, dce_ms, NULL);
        }
        liberar_o1(&p);
    }

    /* Paso 2: -O2. */
    CompilacionO2 c;
    IdeAsmTiempos primera;
    if (estado == 0 && compilar_o2(texto, largo, ruta, &c, &primera) != 0) estado = -1;
    if (estado == 0) {
        IdeOptNivel *o2 = &r->o2;
        if (!c.compilado) {
            o2->motivo = c.motivo;
            c.motivo = NULL;
        } else {
            o2->compilado = 1;
            o2->ensamblador = c.ensamblador;
            c.ensamblador = NULL;
            o2->bundles = c.bundles;
            o2->instrucciones = contar_instrucciones(o2->ensamblador);
            o2->marco_main = c.marco_main;
            o2->variables = c.variables;
            o2->cantidad_variables = c.cantidad_variables;
            c.variables = NULL;
            r->dce = c.dce;
            r->unroll = c.unroll;
            r->renombradas = c.renombradas;
            r->war_eliminadas = c.war;
            r->waw_eliminadas = c.waw;
            r->reordenadas = c.reordenadas;
            r->spills = c.spills;
            r->cargas_spill = c.cargas;
            r->guardados_spill = c.guardados;
            r->generaciones = c.generaciones;
            r->o2_ir_antes = c.ir_antes_cant;
            r->o2_ir_despues = c.ir_despues_cant;
            r->o2_ir_spilling = c.ir_spilling_cant;
            r->ir_antes = c.ir_antes;
            r->ir_despues = c.ir_despues;
            r->ir_spilling = c.ir_spilling;
            c.ir_antes = c.ir_despues = c.ir_spilling = NULL;
            if (mediana(texto, largo, ruta, 2, &primera, &o2->tiempos) != 0 ||
                simular(o2, c.balb, c.largo_balb, c.pc_fin, O2_ANCHO_FIN, ruta) != 0) estado = -1;
        }
        liberar_o2(&c);
    }

    /* Paso 5: cada nivel contra -O0, y los tres juntos. */
    if (estado == 0) {
        r->mismo_o1 = comparar_resultados(&r->o0, &r->o1);
        r->mismo_o2 = comparar_resultados(&r->o0, &r->o2);
        r->mismo_resultado = (r->mismo_o1 == 0 || r->mismo_o2 == 0) ? 0
                           : (r->mismo_o1 == 1 && r->mismo_o2 == 1) ? 1 : -1;
    }
    if (estado != 0) ide_optimizacion_liberar(r);
    return estado;
}

static void liberar_nivel(IdeOptNivel *n)
{
    free(n->motivo);
    free(n->ensamblador);
    met_liberar(&n->simulacion);
    if (n->valores != NULL)
        for (size_t i = 0; i < n->cantidad_variables; ++i) free(n->valores[i]);
    free(n->valores);
    free(n->variables);
    memset(n, 0, sizeof *n);
}

void ide_optimizacion_liberar(IdeOptimizacion *r)
{
    if (r == NULL) return;
    liberar_nivel(&r->o0);
    liberar_nivel(&r->o1);
    liberar_nivel(&r->o2);
    free(r->o1_codigo);
    free(r->ir_antes);
    free(r->ir_despues);
    free(r->ir_spilling);
    memset(r, 0, sizeof *r);
    r->mismo_o1 = r->mismo_o2 = r->mismo_resultado = -1;
}
