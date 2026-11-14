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
 */
#include "ide_ensamblador.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
} Contexto;

/*
 * Qué significa cada código de error de mem_asignar_proyecto (memory.c).
 * balc solo muestra el número ("codigo 6"); el IDE lo explica.
 */
static const char *explicar_memoria(MemProyectoResultado r)
{
    switch (r) {
        case MEM_PROYECTO_PARAMETROS_PENDIENTES:
            return "el backend todavía no soporta parámetros que sean arreglos, listas o "
                   "text: solo int, boolean y char (asignación de memoria, memory.c)";
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

/*
 * Lo llama el análisis semántico cuando el programa es válido. Son los
 * mismos pasos que compilar_proyecto() de cli.c.
 * Devuelve 0 si se generó; 1 si el backend no pudo (el motivo queda en
 * "diag"); 2 si faltó memoria.
 */
static int visitante(void *datos, const Ast *arbol, SimTabla *tabla, FILE *diag)
{
    Contexto *c = datos;
    IdeEnsamblador *s = c->salida;
    c->llego = 1;

    /* Memoria: globales desde gp y la pila desde su tope hacia abajo. */
    MemProyectoMapa mapa = {0};
    MemProyectoResultado r = mem_asignar_proyecto(tabla, CLI_GP_BASE, CLI_FIN_GLOBALES,
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
    int estado = ef_enlazar(arbol, tabla, 0, &arranque, &funciones, temporal, diag, &resumen);
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

    s->funciones = resumen.funciones;
    s->saltos = resumen.saltos.saltos;
    s->reescritos = resumen.saltos.relajados;
    return estado;
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
        Contexto c = {s, 0, 0, NULL, datos};
        int resultado = sem_visitar_proyecto(temporal, diag, visitante, &c);
        if (resultado == 0 && c.llego) {
            int codificado = codificar(s, datos, diag);
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
    memset(s, 0, sizeof *s);
}
