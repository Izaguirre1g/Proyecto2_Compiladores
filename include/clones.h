#ifndef CLONES_H
#define CLONES_H

/*
 * Detección de código clonado (copiado y pegado) entre archivos BAL.
 *
 * Un CLON es un pedazo de código que aparece más de una vez, en el mismo
 * archivo o en archivos distintos. Para decidir si dos fragmentos son clones
 * se sigue el CAMINO DE LA GRAMÁTICA: cada archivo se analiza con el
 * analizador predictivo LL(1) (src/LL1/), que deja su árbol de derivación
 * (qué producción se aplicó en cada paso). Dos fragmentos son clones si el
 * analizador recorrió el MISMO camino: las mismas producciones, en el mismo
 * orden. Se buscan dos tipos:
 *
 *   Tipo I:  mismo camino y mismos tokens. Solo pueden cambiar los espacios,
 *            los saltos de línea y los comentarios, que el lexer descarta.
 *              x : a gauss 1;        y      x  :  a gauss 1;   %% copia
 *
 *   Tipo II: mismo camino; solo cambian los nombres y los valores
 *            (los terminales id, num, cadena y car).
 *              x : a gauss 1;        y      total : suma gauss 5;
 *
 * NO son clones dos fragmentos cuyo camino es otro, aunque se parezcan:
 *   - "x : (a gauss 1);" frente a "x : a gauss 1;": los paréntesis usan la
 *     producción  primario → ( expresion ) .
 *   - un whale frente a un whether con el mismo cuerpo, "gauss" frente a
 *     "neumann", declare_true frente a declare_false: son terminales o
 *     producciones distintas.
 *
 * Cómo se compara rápido (ideas de la presentación "Optimización de Código"):
 *   1. Con un recorrido ascendente, post-orden (diapositiva 8), cada nodo del
 *      árbol de derivación recibe una HUELLA: un número calculado con su
 *      símbolo, la producción aplicada y las huellas de sus hijos. Dos
 *      subárboles iguales tienen la misma huella, igual que en el DAG
 *      (diapositiva 17), donde dos cálculos iguales caen en el mismo nodo.
 *   2. Para el Tipo II, la huella ignora el texto de los nombres y valores.
 *   3. Solo se reportan funciones, bloques y sentencias de un tamaño mínimo
 *      (en tokens), y solo el clon más grande: si dos funciones son copia, no
 *      se listan también todas sus sentencias.
 */

#include <stddef.h>
#include <stdio.h>

/* Un archivo para revisar: su nombre (para los mensajes) y su texto. */
typedef struct {
    const char *nombre;
    const char *texto;
    size_t largo;
} ClonFuente;

/* Tipo de clon. */
typedef enum {
    CLON_TIPO_1 = 1,  /* Código idéntico. */
    CLON_TIPO_2 = 2   /* Misma estructura, otros nombres o valores. */
} ClonTipo;

/* Un lugar donde aparece el código repetido. */
typedef struct {
    char *archivo;              /* Copia propia del nombre del archivo. */
    size_t linea, columna;      /* Dónde empieza. */
    size_t linea_fin, columna_fin;  /* Dónde termina (justo después del final). */
} ClonFragmento;

/* Un grupo de fragmentos que son copia unos de otros (al menos 2). */
typedef struct {
    ClonTipo tipo;
    const char *que;            /* Qué se repite: "funcion", "ciclo whale"... (texto fijo). */
    size_t tokens;              /* Tamaño del fragmento, en tokens. */
    ClonFragmento *fragmentos;
    size_t cantidad;
} Clon;

/* Resultado de la búsqueda. Se crea vacío así: ClonLista lista = {0}; */
typedef struct {
    Clon *elementos;
    size_t cantidad;
    size_t capacidad;
    size_t omitidos;            /* Archivos con errores de sintaxis (sin derivación, no se revisan). */
    char **nombres_omitidos;    /* Sus nombres (copias propias), "omitidos" en total. */
} ClonLista;

/* Tamaño mínimo, en tokens, para que un fragmento cuente como clon.
 * Con menos, saldrían como "clones" cosas tan comunes como "i : i gauss 1;"
 * (6 tokens). */
#define CLONES_MINIMO_TOKENS 10

/*
 * Busca clones en un grupo de archivos.
 *
 *   fuentes, cantidad: los archivos a revisar (se comparan todos con todos,
 *                      y cada uno consigo mismo).
 *   minimo_tokens:     tamaño mínimo de un clon, en tokens (normalmente CLONES_MINIMO_TOKENS).
 *   lista:             aquí se agregan los clones, del más grande al más pequeño.
 *
 * Devuelve 0 si funcionó, o -1 si no hubo memoria.
 */
int clones_buscar(const ClonFuente *fuentes, size_t cantidad, size_t minimo_tokens,
                  ClonLista *lista);

/* Libera la memoria de la lista y la deja vacía otra vez. */
void clones_liberar(ClonLista *lista);

/*
 * Escribe los clones para leerlos en la terminal, por ejemplo:
 *
 *   Clon 1: tipo I (codigo identico), funcion de 22 tokens, 2 copias
 *     examples/llamadas.bal: lineas 2 a 4
 *     examples/ejercicio3_puntajes.bal: lineas 2 a 4
 *
 *   archivos: cuántos archivos se revisaron (para el resumen final).
 * Devuelve 0 si se pudo escribir, o -1 si falló la salida.
 */
int clones_escribir_texto(const ClonLista *lista, size_t archivos, FILE *salida);

/*
 * Escribe los clones como JSON, para que los lea el IDE:
 *
 *   {"archivos": 2, "omitidos": [], "clones": [
 *     {"tipo": 1, "que": "funcion", "tokens": 22, "fragmentos": [
 *       {"archivo": "a.bal", "linea": 2, "columna": 1, "linea_fin": 4, "columna_fin": 2}, ...]}]}
 *
 * Devuelve 0 si se pudo escribir, o -1 si falló la salida.
 */
int clones_escribir_json(const ClonLista *lista, size_t archivos, FILE *salida);

#endif
