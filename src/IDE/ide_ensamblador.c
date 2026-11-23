/*
 * Ensamblador del código del editor (ver include/ide_ensamblador.h).
 *
 * Hace los mismos pasos que "balc -s" (compilar_proyecto en
 * src/Linea_Comandos/cli.c), con el mismo mapa de memoria (include/cli.h):
 *   1. Guardar el texto del editor en el archivo temporal oculto.
 *   2. Análisis semántico; si el programa es válido, se llama al visitante.
 *   3. En el visitante: asignar la memoria y generar + enlazar el
 *      ensamblador (con el arranque que inicializa gp y sp y llama a main).
 *   4. Codificar cada bundle a binario, como hace balc antes de escribir
 *      el .bin: así también se comprueba que el ensamblador es válido.
 *   5. Armar el .bin completo (formato BALB: cabecera + código + datos de
 *      las constantes globales), igual que balc, pero en memoria.
 * No escribe ningún archivo .asm ni .bin.
 *
 * Además mide cuánto tarda cada fase (IdeAsmTiempos): el tiempo de
 * compilación que muestran las ventanas Ensamblador y Métricas.
 */
/* clock_gettime y CLOCK_MONOTONIC son de POSIX, no de C11: hay que pedirlos
 * antes de incluir cualquier cabecera del sistema. */
#define _POSIX_C_SOURCE 200809L

#include "ide_ensamblador.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * La hora actual en milisegundos, según el reloj MONÓTONO: siempre avanza y
 * no salta si alguien cambia la hora del sistema. Solo sirve para restar
 * dos momentos (cuánto tardó algo), no para saber la hora del día.
 */
static double ahora_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0.0;
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

#include "cli.h"
#include "codificacion_binaria.h"
#include "enlace_funciones.h"
#include "formato_binario.h"
#include "ide_analisis.h"
#include "memory.h"
#include "semantic_modules.h"

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

/* Lo que el visitante necesita y lo que deja. */
typedef struct {
    IdeEnsamblador *salida;
    int llego;              /* 1 si el semántico aceptó el programa y llamó al visitante. */
    int sin_memoria;
    const char *explicacion; /* Si la asignación de memoria falló: por qué, en español. */
    FbDatos *datos;         /* Valores iniciales de las globales, para el .bin (paso 5).
                             * Se piden aquí porque el AST se libera al salir del visitante. */
    double entrada_ms;      /* Cuándo entró al visitante: ahí terminó el análisis. */
    double salida_ms;       /* Cuándo salió: ahí terminó la generación. */
} Contexto;

/*
 * Qué significa cada código de error de mem_asignar_proyecto_referencias (memory.c).
 * balc solo muestra el número ("codigo 6"); el IDE lo explica.
 */
static const char *explicar_memoria(MemProyectoResultado r)
{
    switch (r) {
        case MEM_PROYECTO_PARAMETROS_PENDIENTES:
            return "el backend todavía no soporta parámetros que sean listas, text o "
                   "arreglos de varias dimensiones: solo int, boolean, char y arreglos "
                   "de una dimensión (asignación de memoria, memory.c)";
        case MEM_PROYECTO_TAMANO_INDEFINIDO:
            return "hay un arreglo sin tamaño conocido al compilar, y el backend necesita "
                   "saber cuánta memoria reservar (memory.c)";
        case MEM_PROYECTO_SIN_ESPACIO:
        case MEM_PROYECTO_DESBORDAMIENTO:
            return "las variables no caben en la memoria disponible: en las globales "
                   "(0x4000-0x7FFF) o en el marco de una función (máximo 1020 bytes)";
        default:
            return "la asignación de memoria no pudo procesar la tabla de símbolos";
    }
}

static size_t marco_de_main(const SimTabla *tabla);   /* Más abajo. */

/*
 * El backend: los mismos pasos que compilar_proyecto() de cli.c.
 * Devuelve 0 si se generó; 1 si el backend no pudo (el motivo queda en
 * "diag"); 2 si faltó memoria.
 */
static int generar(Contexto *c, const Ast *arbol, SimTabla *tabla, FILE *diag)
{
    IdeEnsamblador *s = c->salida;

    /* Memoria: globales desde gp y la pila desde su tope hacia abajo. Igual
     * que compilar_proyecto() de cli.c: la variante "_referencias" pasa los
     * arreglos de una dimensión como parámetro por su dirección. */
    MemProyectoMapa mapa = {0};
    MemProyectoResultado r = mem_asignar_proyecto_referencias(tabla, CLI_GP_BASE, CLI_FIN_GLOBALES,
                                                              CLI_CAPACIDAD_MARCO, &mapa);
    if (r != MEM_PROYECTO_OK) {
        c->explicacion = explicar_memoria(r);
        return 1;
    }

    /* Generación y enlace: el arranque (inicializa gp y sp y llama a main)
     * y cada función, con los saltos ya resueltos. */
    const EfArranque arranque = {(uint32_t)mapa.globales.gp_base, CLI_TOPE_PILA};
    RrTabla funciones;
    rr_inicializar(&funciones);
    FILE *temporal = tmpfile();
    if (temporal == NULL) {
        rr_liberar(&funciones);
        c->sin_memoria = 1;
        return 2;
    }
    EfResumen resumen = {0};
    /* La variante "_referencias", igual que balc: genera las funciones que
     * reciben arreglos por su dirección. */
    int estado = ef_enlazar_referencias(arbol, tabla, 0, &arranque, &funciones, temporal, diag, &resumen);
    /* Los datos de las globales (como hace balc): sin ellos el .bin no se arma. */
    if (estado == 0 && fb_preparar_datos(arbol, tabla, (uint32_t)mapa.globales.gp_base,
                                         mapa.globales.bytes_globales, c->datos, diag) != 0) {
        estado = 1;
    }
    if (estado == 0) {
        s->ensamblador = leer_todo(temporal);
        if (s->ensamblador == NULL) {
            c->sin_memoria = 1;
            estado = 2;
        }
    } else {
        estado = 1;
    }
    fclose(temporal);
    rr_liberar(&funciones);

    /* Para leer el resultado de main en el simulador (ventana -O0 / -O2):
     * el tamaño de su marco y dónde está cada variable. Se calcula DESPUÉS
     * de generar el ensamblador, así no puede cambiar lo que ya se generó. */
    if (estado == 0) {
        s->marco_main = marco_de_main(tabla);
        if (ide_variables_de_main(tabla, &s->variables, &s->cantidad_variables) != 0) {
            c->sin_memoria = 1;
            estado = 2;
        }
    }

    s->funciones = resumen.funciones;
    s->saltos = resumen.saltos.saltos;
    s->reescritos = resumen.saltos.relajados;
    return estado;
}

/*
 * Lo llama el análisis semántico cuando el programa es válido. Anota el
 * momento en que entra (fin del análisis) y en que sale (fin de la
 * generación), y entre los dos hace el backend.
 */
static int visitante(void *datos, const Ast *arbol, SimTabla *tabla, FILE *diag)
{
    Contexto *c = datos;
    c->llego = 1;
    c->entrada_ms = ahora_ms();
    int estado = generar(c, arbol, tabla, diag);
    c->salida_ms = ahora_ms();
    return estado;
}

/* El ámbito de función que contiene a "a" (el hijo directo del global), o NULL. */
static const SimAmbito *funcion_de(const SimTabla *tabla, const SimAmbito *a)
{
    while (a != NULL && a->padre != NULL && a->padre != tabla->global) a = a->padre;
    return a != NULL && a->padre == tabla->global ? a : NULL;
}

/*
 * Bytes del marco de main con la memoria ya asignada: donde termina su
 * última variable (incluidas las de sus bloques), con un mínimo de 4 para
 * ra y redondeado a múltiplo de 4. Es la misma regla que usa el enlazador
 * (marco_de en src/Resolucion_y_Enlace/enlace_funciones.c, que es interna),
 * así que es el mismo marco con que se generó el código de main.
 */
static size_t marco_de_main(const SimTabla *tabla)
{
    const SimAmbito *main_ambito = NULL;
    for (const SimAmbito *a = tabla->global->siguiente_creacion; a != NULL; a = a->siguiente_creacion)
        if (a->padre == tabla->global && a->nombre != NULL && strcmp(a->nombre, "main") == 0) main_ambito = a;
    if (main_ambito == NULL) return 0;
    size_t marco = 4;
    for (const SimSimbolo *s = tabla->primero; s != NULL; s = s->siguiente_orden) {
        const SimAmbito *a = s->ambito;
        while (a != NULL && a != main_ambito) a = a->padre;
        if (a == NULL || s->desplazamiento < 4) continue;
        size_t fin = (size_t)s->desplazamiento + s->bytes;
        if (fin > marco) marco = fin;
    }
    return (marco + 3) / 4 * 4;
}

int ide_variables_de_main(const SimTabla *tabla, IdeVarMain **variables, size_t *cantidad)
{
    *variables = NULL;
    *cantidad = 0;
    if (tabla == NULL || tabla->global == NULL) return 0;
    size_t capacidad = 0;
    /* En el orden en que se declararon (siguiente_orden), igual en -O0 y -O2. */
    for (const SimSimbolo *s = tabla->primero; s != NULL; s = s->siguiente_orden) {
        const SimAmbito *f = funcion_de(tabla, s->ambito);
        if (f == NULL || f->nombre == NULL || strcmp(f->nombre, "main") != 0) continue;
        if ((s->clase != SIM_VARIABLE && s->clase != SIM_CONSTANTE) ||
            s->almacen != SIM_ALMACEN_LOCAL || s->desplazamiento < 0 || s->bytes == 0) continue;
        if (s->tipo != SIM_TIPO_INT && s->tipo != SIM_TIPO_BOOLEAN && s->tipo != SIM_TIPO_CHAR) continue;
        /* Un arreglo: tantos elementos como el producto de sus longitudes. */
        size_t elementos = 1;
        for (unsigned d = 0; d < s->dimensiones && s->longitudes != NULL; ++d) elementos *= s->longitudes[d];
        if (elementos == 0 || s->bytes % elementos != 0) continue;
        if (*cantidad == capacidad) {
            size_t nueva = capacidad ? 2 * capacidad : 8;
            IdeVarMain *otra = realloc(*variables, nueva * sizeof *otra);
            if (otra == NULL) {
                free(*variables);
                *variables = NULL;
                *cantidad = 0;
                return -1;
            }
            *variables = otra;
            capacidad = nueva;
        }
        IdeVarMain *v = &(*variables)[(*cantidad)++];
        memset(v, 0, sizeof *v);
        snprintf(v->nombre, sizeof v->nombre, "%s", s->nombre);
        if(s->declaracion) {
            v->linea_declaracion=s->declaracion->ubicacion.first_line;
            v->columna_declaracion=s->declaracion->ubicacion.first_column;
        }
        v->desplazamiento = s->desplazamiento;
        v->elementos = elementos;
        v->bytes_elemento = s->bytes / elementos;
        v->es_boolean = s->tipo == SIM_TIPO_BOOLEAN;
        v->es_char = s->tipo == SIM_TIPO_CHAR;
    }
    return 0;
}

/* Lee los bytes de un archivo temporal (pueden incluir ceros) y deja su
 * cantidad en "largo". NULL si falló. */
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

/* Codifica el ensamblador a binario, guarda el listado de los bundles
 * (paso 4) y arma el .bin completo con los datos de las globales (paso 5).
 * Devuelve 0 bien, 1 si el ensamblador no es válido para la ISA, -1 sin memoria. */
static int codificar(IdeEnsamblador *s, const FbDatos *datos, FILE *diag)
{
    CbPrograma programa = {0};
    if (cb_ensamblar(s->ensamblador, &programa, diag) != 0) return 1;
    s->bundles = programa.cantidad;

    int estado = 0;
    FILE *f = tmpfile();
    if (f == NULL || cb_escribir_listado(&programa, f) != 0 || (s->binario = leer_todo(f)) == NULL) {
        estado = -1;
    }
    if (f != NULL) fclose(f);

    /* Paso 5: el mismo contenedor BALB que escribe balc (fb_escribir). */
    if (estado == 0) {
        FILE *bin = tmpfile();
        if (bin == NULL || fb_escribir(&programa, datos, bin) != 0 ||
            (s->balb = leer_bytes(bin, &s->largo_balb)) == NULL) {
            estado = -1;
        }
        if (bin != NULL) fclose(bin);
    }
    cb_liberar(&programa);
    return estado;
}

/*
 * El primer mensaje de ERROR, como texto propio (o NULL si no hay).
 * Se saltan los avisos informativos del semántico, que empiezan con "[OK]"
 * (por ejemplo "[OK] Integridad de tabla: 12 simbolos, sin offsets").
 */
static char *primer_error(const char *texto)
{
    for (const char *linea = texto; linea != NULL && *linea != '\0';) {
        size_t largo = strcspn(linea, "\n");
        if (largo > 0 && strncmp(linea, "[OK]", 4) != 0) {
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

/* Copia una cadena (NULL si falta memoria). */
static char *copiar(const char *s)
{
    char *c = malloc(strlen(s) + 1);
    if (c != NULL) strcpy(c, s);
    return c;
}

int ide_ensamblador_generar(const char *texto, size_t largo, const char *ruta, IdeEnsamblador *s)
{
    memset(s, 0, sizeof *s);
    const double inicio_ms = ahora_ms();   /* Para el tiempo total de compilación. */

    /* Paso 1: el texto del editor, en el archivo temporal. */
    char *temporal = ide_ruta_temporal(ruta);
    if (temporal == NULL) return -1;
    if (ide_escribir_archivo(temporal, texto, largo) != 0) {
        free(temporal);
        return -1;
    }

    /* Pasos 2 a 5. FbDatos mide más de 16 KB: se pide con malloc y no en la pila. */
    int estado = -1;
    FbDatos *datos = malloc(sizeof *datos);
    FILE *diag = datos != NULL ? tmpfile() : NULL;
    if (diag != NULL) {
        Contexto c = {s, 0, 0, NULL, datos, 0.0, 0.0};
        const double analisis_ms = ahora_ms();
        int resultado = sem_visitar_proyecto(temporal, diag, visitante, &c);
        /* Análisis: desde que empieza hasta que el semántico llama al
         * visitante. Si no lo llamó (hay errores), hasta que terminó. */
        s->tiempos.analisis = (c.llego ? c.entrada_ms : ahora_ms()) - analisis_ms;
        if (c.llego) s->tiempos.generacion = c.salida_ms - c.entrada_ms;
        if (resultado == 0 && c.llego) {
            const double codificacion_ms = ahora_ms();
            int codificado = codificar(s, datos, diag);
            s->tiempos.codificacion = ahora_ms() - codificacion_ms;
            if (codificado == 0) {
                s->estado = IDE_ASM_LISTO;
                estado = 0;
            } else if (codificado == 1) {
                s->estado = IDE_ASM_SIN_BACKEND;
                estado = 0;
            }
        } else if (c.sin_memoria) {
            estado = -1;
        } else if (c.llego) {
            s->estado = IDE_ASM_SIN_BACKEND;  /* Válido, pero el backend no pudo. */
            estado = 0;
        } else {
            s->estado = IDE_ASM_CON_ERRORES;
            estado = 0;
        }

        /* Si el backend no pudo, se guarda por qué: la explicación de la
         * memoria, o el primer error que escribió el backend. */
        if (estado == 0 && s->estado == IDE_ASM_SIN_BACKEND) {
            if (c.explicacion != NULL) {
                s->motivo = copiar(c.explicacion);
            } else {
                char *mensajes = leer_todo(diag);
                s->motivo = primer_error(mensajes);
                free(mensajes);
                if (s->motivo == NULL) s->motivo = copiar("el backend no pudo generar el ensamblador");
            }
            if (s->motivo == NULL) estado = -1;
        }
        fclose(diag);
    }

    free(datos);
    remove(temporal);
    free(temporal);
    s->tiempos.total = ahora_ms() - inicio_ms;
    if (estado != 0) ide_ensamblador_liberar(s);
    return estado;
}

void ide_ensamblador_liberar(IdeEnsamblador *s)
{
    if (s == NULL) return;
    free(s->motivo);
    free(s->ensamblador);
    free(s->binario);
    free(s->balb);
    free(s->variables);
    memset(s, 0, sizeof *s);
}
