/*
 * Botón "Tabla de símbolos" del IDE: la tabla de símbolos del código del
 * editor, tal como la arma el análisis semántico con
 * src/Analisis_Semantico/symbol_table.c.
 *
 * Pasos (ver ide_simbolos.c):
 *   1. Guardar el texto del editor en el archivo temporal oculto.
 *   2. Análisis semántico completo (sem_visitar_proyecto). Mientras analiza,
 *      el semántico llena la SimTabla con sim_entrar_ambito, sim_insertar,
 *      sim_definir_dimensiones y sim_salir_ambito (symbol_table.c).
 *   3. Si el programa es válido, el semántico llama al visitante con la
 *      tabla terminada. Ahí se usa symbol_table.c para LEERLA:
 *        - sim_verificar_pre_memoria: la tabla es consistente;
 *        - la memoria se asigna igual que "balc -m" (cada símbolo recibe su
 *          almacén y desplazamiento con sim_definir_ubicacion);
 *        - el código se genera y se enlaza como en "balc -m": el enlazador
 *          dice en qué dirección quedó cada función (su "dirección de
 *          código"). Si el backend no puede generarlo, queda "pendiente";
 *        - se recorre tabla->primero ... siguiente_orden (orden en que se
 *          declararon) y cada ámbito con su tabla hash (uthash);
 *        - sim_nombre_clase y sim_nombre_tipo dan los nombres legibles;
 *        - sim_imprimir da el listado de texto de la tabla.
 *      Todo se COPIA, porque la tabla se libera al terminar el análisis.
 *
 * Las columnas y la sección Memoria son las mismas del archivo
 * <fuente>.simbolos.md que escribe "balc -m" (exportar_simbolos_md, cli.c).
 *
 * No modifica ningún módulo de los compañeros: solo llama a sus funciones.
 */
#ifndef IDE_SIMBOLOS_H
#define IDE_SIMBOLOS_H

#include <stddef.h>

typedef enum {
    IDE_SIM_LISTO,        /* El programa es válido: hay tabla. */
    IDE_SIM_CON_ERRORES   /* El código tiene errores: el semántico no terminó la tabla. */
} IdeSimEstado;

/* Un símbolo de la tabla (una fila de la ventana). */
typedef struct {
    char nombre[64];
    const char *clase;        /* sim_nombre_clase: "variable", "funcion"... (texto fijo). */
    const char *tipo;         /* sim_nombre_tipo: "declare_int"... (texto fijo). */
    char ambito[64];          /* Nombre del ámbito: "global", "main", "bloque"... */
    unsigned nivel;           /* 0 = global, 1 = función, 2 o más = bloques internos. */
    char dimensiones[48];     /* "" si no es arreglo; "[2][3]" si es una matriz 2x3. */
    size_t parametros;        /* En las funciones: cuántos parámetros recibe. */
    const char *almacen;      /* "global(gp)", "local(sp)", "parametro(sp)", "codigo"... */
    unsigned direccion;       /* Dirección de código de una función (ver con_direccion). */
    int con_direccion;        /* 1 si el enlazador le dio dirección; si no, "pendiente". */
    size_t bytes;             /* Bytes que ocupa en memoria. */
    long desplazamiento;      /* Desde gp (globales) o sp (locales y parámetros). */
    int con_desplazamiento;   /* 0 si no tiene (funciones o memoria sin asignar). */
    size_t linea, columna;    /* Dónde se declaró. */
} IdeSimbolo;

/* Un ámbito (alcance) de la tabla: cada uno tiene su propia tabla hash. */
typedef struct {
    char nombre[64];
    unsigned nivel;
    char padre[64];           /* "" para el ámbito global. */
    size_t simbolos;          /* Símbolos en su tabla hash (HASH_COUNT). */
} IdeAmbito;

typedef struct {
    IdeSimEstado estado;
    IdeSimbolo *simbolos;     /* En el orden en que se declararon. */
    size_t cantidad;
    IdeAmbito *ambitos;       /* En el orden en que se crearon. */
    size_t cantidad_ambitos;
    int consistente;          /* sim_verificar_pre_memoria: 1 si la tabla es válida. */
    int con_memoria;          /* 1 si se pudo asignar la memoria (como balc -m). */
    size_t funciones;         /* Del mapa de memoria. */
    size_t marco_maximo;      /* Bytes del marco más grande. */
    size_t gp;                /* Dirección donde empiezan las globales. */
    unsigned sp;              /* Tope de la pila (la pila crece hacia abajo). */
    int con_codigo;           /* 1 si se generó el código (hay direcciones de funciones). */
    size_t bytes_globales;
    char *listado;            /* Lo que escribe sim_imprimir (memoria propia). */
} IdeSimbolos;

/* Arma la tabla del texto del editor. "ruta" es el archivo abierto (puede
 * ser NULL); se usa para el temporal, así funcionan los imports relativos.
 * Devuelve 0 si se pudo analizar (ver s->estado) o -1 si faltó memoria o no
 * se pudo escribir el archivo temporal. Liberar con ide_simbolos_liberar. */
int ide_simbolos_generar(const char *texto, size_t largo, const char *ruta, IdeSimbolos *s);

void ide_simbolos_liberar(IdeSimbolos *s);

#endif
