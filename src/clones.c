#include "clones.h"

#include "ast.h"
#include "sintactico.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Información de cada nodo del AST                                    */
/* ------------------------------------------------------------------ */

/* Las dos formas de comparar: con nombres y valores (Tipo I) o sin ellos (Tipo II). */
enum { MODO_TIPO_1 = 0, MODO_TIPO_2 = 1 };

/*
 * Todos los nodos de todos los archivos se guardan en un solo arreglo, en
 * PREORDEN: cada nodo va antes que sus hijos. Así, los descendientes de un
 * nodo quedan juntos, justo después de él:
 *
 *     Asignacion           [0]   tamano 5  -> sus descendientes son [1..4]
 *       Identificador x    [1]   tamano 1
 *       Binario gauss      [2]   tamano 3  -> sus descendientes son [3..4]
 *         Identificador a  [3]   tamano 1
 *         Entero 1         [4]   tamano 1
 */
typedef struct {
    const AstNodo *nodo;
    size_t fuente;          /* De qué archivo es (posición en el arreglo de fuentes). */
    size_t tamano;          /* Cuántos nodos tiene su subárbol, contándolo a él. */
    uint64_t huella[2];     /* Huella para el Tipo I y para el Tipo II. */
    size_t linea_fin, columna_fin;  /* Hasta dónde llega su subárbol en el texto. */
    int cubierto;           /* 1 si ya es parte de un clon reportado. */
} InfoNodo;

typedef struct {
    InfoNodo *elementos;
    size_t cantidad;
    size_t capacidad;
} ListaNodos;

/* ------------------------------------------------------------------ */
/* Huellas                                                             */
/* ------------------------------------------------------------------ */

/*
 * La huella se calcula con FNV-1a, una forma sencilla de convertir datos en
 * un número de 64 bits: por cada dato, se hace XOR y se multiplica por un
 * primo. Datos iguales dan la misma huella; datos distintos casi siempre
 * dan huellas distintas. "Casi" porque puede haber choques: por eso, cuando
 * dos huellas coinciden, después se comparan los árboles de verdad.
 */
#define FNV_INICIO 14695981039346656037ULL
#define FNV_PRIMO 1099511628211ULL

/* Mezcla un número en la huella. */
static uint64_t mezclar(uint64_t huella, uint64_t dato)
{
    for (int i = 0; i < 8; ++i) {
        huella ^= (dato >> (8 * i)) & 0xFF;
        huella *= FNV_PRIMO;
    }
    return huella;
}

/* Mezcla un texto en la huella (NULL cuenta como texto vacío). */
static uint64_t mezclar_texto(uint64_t huella, const char *texto)
{
    for (const char *c = (texto != NULL) ? texto : ""; *c != '\0'; ++c) {
        huella ^= (unsigned char)*c;
        huella *= FNV_PRIMO;
    }
    /* Un separador, para que "ab"+"c" no dé lo mismo que "a"+"bc". */
    return mezclar(huella, 0xFF);
}

/*
 * ¿El Tipo II ignora el texto de este nodo? Sí para los nombres y los
 * valores: es lo que puede cambiar en una copia Tipo II. No para los
 * operadores ("gauss") ni los tipos ("declare_int"), que son la forma del código.
 */
static int tipo2_ignora_texto(AstTipo tipo)
{
    return tipo == AST_IDENTIFICADOR || tipo == AST_ENTERO || tipo == AST_CADENA ||
           tipo == AST_CARACTER || tipo == AST_BOOLEANO;
}

/* ------------------------------------------------------------------ */
/* Paso 1: pasar el AST a la lista en preorden                         */
/* ------------------------------------------------------------------ */

static int agregar_nodo(ListaNodos *lista, const AstNodo *nodo, size_t fuente)
{
    if (lista->cantidad == lista->capacidad) {
        size_t nueva = (lista->capacidad == 0) ? 256 : lista->capacidad * 2;
        InfoNodo *mas = realloc(lista->elementos, nueva * sizeof *mas);
        if (mas == NULL) return -1;
        lista->elementos = mas;
        lista->capacidad = nueva;
    }
    InfoNodo info = {0};
    info.nodo = nodo;
    info.fuente = fuente;
    lista->elementos[lista->cantidad++] = info;
    return 0;
}

/*
 * Recorre el árbol en preorden sin recursión (con una pila propia), igual
 * que ast_imprimir, para no agotar la pila de C con árboles muy profundos.
 */
static int recorrer_preorden(const AstNodo *raiz, size_t fuente, ListaNodos *lista)
{
    size_t capacidad = 64, cantidad = 0;
    const AstNodo **pila = malloc(capacidad * sizeof *pila);
    if (pila == NULL) return -1;
    pila[cantidad++] = raiz;

    int resultado = 0;
    while (cantidad > 0 && resultado == 0) {
        const AstNodo *nodo = pila[--cantidad];
        if (agregar_nodo(lista, nodo, fuente) != 0) { resultado = -1; break; }

        /* Apilar los hijos al revés, para que el primero salga primero. */
        size_t desde = cantidad;
        for (const AstNodo *h = nodo->primer_hijo; h != NULL; h = h->siguiente) {
            if (cantidad == capacidad) {
                capacidad *= 2;
                const AstNodo **mas = realloc(pila, capacidad * sizeof *mas);
                if (mas == NULL) { resultado = -1; break; }
                pila = mas;
            }
            pila[cantidad++] = h;
        }
        for (size_t i = desde, j = cantidad; resultado == 0 && i + 1 < j; ++i, --j) {
            const AstNodo *t = pila[i];
            pila[i] = pila[j - 1];
            pila[j - 1] = t;
        }
    }
    free(pila);
    return resultado;
}

/* ------------------------------------------------------------------ */
/* Paso 2: huellas, de las hojas hacia arriba                          */
/* ------------------------------------------------------------------ */

/* ¿(l1, c1) va después que (l2, c2) en el texto? */
static int despues_de(size_t l1, size_t c1, size_t l2, size_t c2)
{
    return l1 > l2 || (l1 == l2 && c1 > c2);
}

/*
 * Recorre la lista de atrás hacia adelante. En preorden, los hijos están
 * DESPUÉS del padre, así que al ir hacia atrás los hijos se calculan antes:
 * es el recorrido ascendente (post-orden) de la diapositiva 8.
 *
 * Los hijos de un nodo i se encuentran así: el primero está en i + 1; el
 * siguiente, saltando el subárbol del anterior (i + 1 + su tamaño); etc.
 */
static void calcular_huellas(ListaNodos *lista)
{
    for (size_t k = lista->cantidad; k > 0; --k) {
        size_t i = k - 1;
        InfoNodo *info = &lista->elementos[i];
        const AstNodo *nodo = info->nodo;

        uint64_t h1 = mezclar(FNV_INICIO, (uint64_t)nodo->tipo);
        uint64_t h2 = h1;
        h1 = mezclar_texto(h1, nodo->texto);
        h2 = mezclar_texto(h2, tipo2_ignora_texto(nodo->tipo) ? NULL : nodo->texto);

        info->tamano = 1;
        info->linea_fin = nodo->ubicacion.last_line;
        info->columna_fin = nodo->ubicacion.last_column;

        size_t hijo = i + 1;
        for (const AstNodo *h = nodo->primer_hijo; h != NULL; h = h->siguiente) {
            const InfoNodo *ih = &lista->elementos[hijo];
            h1 = mezclar(h1, ih->huella[MODO_TIPO_1]);
            h2 = mezclar(h2, ih->huella[MODO_TIPO_2]);
            info->tamano += ih->tamano;
            if (despues_de(ih->linea_fin, ih->columna_fin, info->linea_fin, info->columna_fin)) {
                info->linea_fin = ih->linea_fin;
                info->columna_fin = ih->columna_fin;
            }
            hijo += ih->tamano;
        }
        info->huella[MODO_TIPO_1] = h1;
        info->huella[MODO_TIPO_2] = h2;
    }
}

/*
 * Compara dos subárboles de verdad (por si dos huellas chocaron).
 * Como están en preorden, basta comparar posición por posición: mismo
 * tipo, mismo tamaño (misma forma) y, si el modo lo pide, mismo texto.
 */
static int subarboles_iguales(const ListaNodos *lista, size_t a, size_t b, int modo)
{
    const InfoNodo *e = lista->elementos;
    if (e[a].tamano != e[b].tamano) return 0;

    for (size_t k = 0; k < e[a].tamano; ++k) {
        const AstNodo *x = e[a + k].nodo;
        const AstNodo *y = e[b + k].nodo;
        if (x->tipo != y->tipo || e[a + k].tamano != e[b + k].tamano) return 0;
        if (modo == MODO_TIPO_2 && tipo2_ignora_texto(x->tipo)) continue;

        const char *tx = (x->texto != NULL) ? x->texto : "";
        const char *ty = (y->texto != NULL) ? y->texto : "";
        if (strcmp(tx, ty) != 0) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Paso 3: candidatos y grupos de clones                               */
/* ------------------------------------------------------------------ */

/* ¿Qué se repite? Devuelve el nombre para mostrar, o NULL si este tipo de
 * nodo no se reporta (una expresión suelta, un nombre, un tipo...). */
static const char *descripcion(AstTipo tipo)
{
    switch (tipo) {
        case AST_FUNCION: return "funcion";
        case AST_BLOQUE: return "bloque";
        case AST_SI: return "condicional whether";
        case AST_MIENTRAS: return "ciclo whale";
        case AST_RECORRIDO: return "ciclo cycle";
        case AST_INTENTAR: return "bloque seek";
        case AST_DECLARACION: return "declaracion";
        case AST_ASIGNACION: return "asignacion";
        case AST_RETORNO: return "give";
        case AST_LLAMADA: return "llamada";
        case AST_AGREGAR: return "add";
        case AST_ELIMINAR: return "remove";
        case AST_CONSTANTE: return "constante";
        default: return NULL;
    }
}

/* Un nodo que podría ser un clon, en uno de los dos modos. */
typedef struct {
    size_t indice;
    int modo;
    const ListaNodos *lista;  /* Para poder ordenarlos con qsort. */
} Candidato;

/*
 * Orden para revisar los candidatos:
 *   1. Del más grande al más pequeño: así el clon grande se reporta primero
 *      y sus partes ya no se repiten después.
 *   2. Con el mismo tamaño, Tipo I antes que Tipo II.
 *   3. Con la misma huella, juntos (así se forman los grupos).
 *   4. Por posición, para que el resultado siempre salga en el mismo orden.
 */
static int comparar_candidatos(const void *pa, const void *pb)
{
    const Candidato *a = pa, *b = pb;
    const InfoNodo *ia = &a->lista->elementos[a->indice];
    const InfoNodo *ib = &b->lista->elementos[b->indice];

    if (ia->tamano != ib->tamano) return (ia->tamano > ib->tamano) ? -1 : 1;
    if (a->modo != b->modo) return (a->modo < b->modo) ? -1 : 1;
    if (ia->huella[a->modo] != ib->huella[b->modo]) {
        return (ia->huella[a->modo] < ib->huella[b->modo]) ? -1 : 1;
    }
    if (a->indice != b->indice) return (a->indice < b->indice) ? -1 : 1;
    return 0;
}

/* Marca un nodo y todo su subárbol como parte de un clon ya reportado. */
static void cubrir(ListaNodos *lista, size_t indice)
{
    size_t fin = indice + lista->elementos[indice].tamano;
    for (size_t k = indice; k < fin; ++k) lista->elementos[k].cubierto = 1;
}

/* Agrega un clon a la lista de resultados con los nodos indicados. */
static int agregar_clon(ClonLista *resultado, const ListaNodos *lista,
                        const ClonFuente *fuentes, const size_t *miembros,
                        size_t cantidad, int modo)
{
    if (resultado->cantidad == resultado->capacidad) {
        size_t nueva = (resultado->capacidad == 0) ? 16 : resultado->capacidad * 2;
        Clon *mas = realloc(resultado->elementos, nueva * sizeof *mas);
        if (mas == NULL) return -1;
        resultado->elementos = mas;
        resultado->capacidad = nueva;
    }

    const InfoNodo *primero = &lista->elementos[miembros[0]];
    Clon clon = {0};
    clon.tipo = (modo == MODO_TIPO_1) ? CLON_TIPO_1 : CLON_TIPO_2;
    clon.que = descripcion(primero->nodo->tipo);
    clon.nodos = primero->tamano;
    clon.fragmentos = calloc(cantidad, sizeof *clon.fragmentos);
    if (clon.fragmentos == NULL) return -1;

    for (size_t i = 0; i < cantidad; ++i) {
        const InfoNodo *info = &lista->elementos[miembros[i]];
        const char *nombre = fuentes[info->fuente].nombre;
        ClonFragmento *f = &clon.fragmentos[i];

        f->archivo = malloc(strlen(nombre) + 1);
        if (f->archivo == NULL) {
            for (size_t j = 0; j < i; ++j) free(clon.fragmentos[j].archivo);
            free(clon.fragmentos);
            return -1;
        }
        strcpy(f->archivo, nombre);
        f->linea = info->nodo->ubicacion.first_line;
        f->columna = info->nodo->ubicacion.first_column;
        f->linea_fin = info->linea_fin;
        f->columna_fin = info->columna_fin;
        clon.cantidad++;
    }
    resultado->elementos[resultado->cantidad++] = clon;
    return 0;
}

/*
 * Revisa una "racha" de candidatos con el mismo tamaño, modo y huella.
 * Casi siempre son todos iguales, pero si hubo un choque de huellas puede
 * haber varios grupos mezclados: se separan comparando los árboles.
 */
static int revisar_racha(Candidato *racha, size_t largo, ListaNodos *lista,
                         const ClonFuente *fuentes, ClonLista *resultado)
{
    size_t *grupo = malloc(largo * sizeof *grupo);
    char *usado = calloc(largo, 1);
    if (grupo == NULL || usado == NULL) {
        free(grupo);
        free(usado);
        return -1;
    }

    int modo = racha[0].modo;
    int resultado_final = 0;
    for (size_t i = 0; i < largo && resultado_final == 0; ++i) {
        if (usado[i]) continue;

        /* Armar el grupo de los que son iguales a racha[i]. */
        size_t cantidad = 0;
        grupo[cantidad++] = racha[i].indice;
        usado[i] = 1;
        for (size_t j = i + 1; j < largo; ++j) {
            if (!usado[j] && subarboles_iguales(lista, racha[i].indice, racha[j].indice, modo)) {
                grupo[cantidad++] = racha[j].indice;
                usado[j] = 1;
            }
        }
        if (cantidad < 2) continue;  /* Solo aparece una vez: no es clon. */

        /* Si TODOS ya son parte de un clon más grande, no se repite. */
        int alguno_nuevo = 0;
        for (size_t k = 0; k < cantidad; ++k) {
            if (!lista->elementos[grupo[k]].cubierto) alguno_nuevo = 1;
        }
        if (!alguno_nuevo) continue;

        if (agregar_clon(resultado, lista, fuentes, grupo, cantidad, modo) != 0) {
            resultado_final = -1;
            break;
        }
        for (size_t k = 0; k < cantidad; ++k) cubrir(lista, grupo[k]);
    }

    free(grupo);
    free(usado);
    return resultado_final;
}

/* ------------------------------------------------------------------ */
/* Función principal                                                   */
/* ------------------------------------------------------------------ */

/* Anota un archivo que no se pudo revisar por tener errores de sintaxis. */
static int anotar_omitido(ClonLista *resultado, const char *nombre)
{
    char **mas = realloc(resultado->nombres_omitidos,
                         (resultado->omitidos + 1) * sizeof *mas);
    if (mas == NULL) return -1;
    resultado->nombres_omitidos = mas;

    char *copia = malloc(strlen(nombre) + 1);
    if (copia == NULL) return -1;
    strcpy(copia, nombre);
    resultado->nombres_omitidos[resultado->omitidos++] = copia;
    return 0;
}

int clones_buscar(const ClonFuente *fuentes, size_t cantidad, size_t minimo_nodos,
                  ClonLista *resultado)
{
    int estado = 0;
    ListaNodos lista = {0};
    Candidato *candidatos = NULL;
    Ast *arboles = calloc(cantidad > 0 ? cantidad : 1, sizeof *arboles);
    FILE *descartar = tmpfile();  /* Los errores de sintaxis no se muestran aquí. */
    if (arboles == NULL || descartar == NULL) estado = -1;

    /* Paso 1: el AST de cada archivo, pasado a la lista en preorden.
     * Un archivo con errores de sintaxis no tiene AST: se cuenta y se salta. */
    for (size_t f = 0; f < cantidad && estado == 0; ++f) {
        int r = construir_ast(fuentes[f].texto, fuentes[f].largo, fuentes[f].nombre,
                              descartar, &arboles[f]);
        if (r == 2) estado = -1;
        else if (r != 0 || arboles[f].raiz == NULL) {
            if (anotar_omitido(resultado, fuentes[f].nombre) != 0) estado = -1;
        }
        else if (recorrer_preorden(arboles[f].raiz, f, &lista) != 0) estado = -1;
    }

    /* Paso 2: las huellas de todos los nodos. */
    if (estado == 0) calcular_huellas(&lista);

    /* Paso 3: los candidatos (funciones, bloques y sentencias de tamaño
     * suficiente), una vez por cada modo. */
    size_t total = 0;
    if (estado == 0) {
        candidatos = malloc((lista.cantidad * 2 + 1) * sizeof *candidatos);
        if (candidatos == NULL) estado = -1;
    }
    for (size_t i = 0; i < lista.cantidad && estado == 0; ++i) {
        const InfoNodo *info = &lista.elementos[i];
        if (descripcion(info->nodo->tipo) == NULL || info->tamano < minimo_nodos) continue;
        for (int modo = MODO_TIPO_1; modo <= MODO_TIPO_2; ++modo) {
            Candidato c = {i, modo, &lista};
            candidatos[total++] = c;
        }
    }

    /* Paso 4: ordenar y revisar las rachas con la misma huella. */
    if (estado == 0 && total > 0) {
        qsort(candidatos, total, sizeof *candidatos, comparar_candidatos);

        size_t inicio = 0;
        while (inicio < total && estado == 0) {
            const InfoNodo *a = &lista.elementos[candidatos[inicio].indice];
            size_t fin = inicio + 1;
            while (fin < total) {
                const InfoNodo *b = &lista.elementos[candidatos[fin].indice];
                if (b->tamano != a->tamano || candidatos[fin].modo != candidatos[inicio].modo ||
                    b->huella[candidatos[fin].modo] != a->huella[candidatos[inicio].modo]) break;
                fin++;
            }
            if (fin - inicio >= 2 &&
                revisar_racha(candidatos + inicio, fin - inicio, &lista, fuentes, resultado) != 0) {
                estado = -1;
            }
            inicio = fin;
        }
    }

    free(candidatos);
    free(lista.elementos);
    for (size_t f = 0; arboles != NULL && f < cantidad; ++f) ast_liberar(&arboles[f]);
    free(arboles);
    if (descartar != NULL) fclose(descartar);
    return estado;
}

void clones_liberar(ClonLista *lista)
{
    for (size_t i = 0; i < lista->cantidad; ++i) {
        Clon *c = &lista->elementos[i];
        for (size_t j = 0; j < c->cantidad; ++j) free(c->fragmentos[j].archivo);
        free(c->fragmentos);
    }
    free(lista->elementos);
    for (size_t i = 0; i < lista->omitidos; ++i) free(lista->nombres_omitidos[i]);
    free(lista->nombres_omitidos);
    lista->nombres_omitidos = NULL;
    lista->elementos = NULL;
    lista->cantidad = 0;
    lista->capacidad = 0;
    lista->omitidos = 0;
}

/* ------------------------------------------------------------------ */
/* Salida: texto y JSON                                                */
/* ------------------------------------------------------------------ */

/* Nombre del tipo de clon para mostrar. */
static const char *nombre_tipo(ClonTipo tipo)
{
    return (tipo == CLON_TIPO_1) ? "tipo I (codigo identico)"
                                 : "tipo II (misma estructura, otros nombres o valores)";
}

int clones_escribir_texto(const ClonLista *lista, size_t archivos, FILE *salida)
{
    for (size_t i = 0; i < lista->cantidad; ++i) {
        const Clon *c = &lista->elementos[i];
        fprintf(salida, "Clon %zu: %s, %s de %zu nodos, %zu copias\n",
                i + 1, nombre_tipo(c->tipo), c->que, c->nodos, c->cantidad);
        for (size_t j = 0; j < c->cantidad; ++j) {
            const ClonFragmento *f = &c->fragmentos[j];
            if (f->linea == f->linea_fin) {
                fprintf(salida, "  %s: linea %zu\n", f->archivo, f->linea);
            } else {
                fprintf(salida, "  %s: lineas %zu a %zu\n", f->archivo, f->linea, f->linea_fin);
            }
        }
        fputc('\n', salida);
    }

    /* Resumen al final. */
    fprintf(salida, "%zu clon%s en %zu archivo%s", lista->cantidad,
            lista->cantidad == 1 ? "" : "es", archivos, archivos == 1 ? "" : "s");
    fputc('\n', salida);
    for (size_t i = 0; i < lista->omitidos; ++i) {
        fprintf(salida, "No se reviso %s: tiene errores de sintaxis\n", lista->nombres_omitidos[i]);
    }
    return ferror(salida) ? -1 : 0;
}

/* Escribe un texto entre comillas, escapando lo que JSON no permite tal cual. */
static void escribir_texto_json(const char *texto, FILE *salida)
{
    fputc('"', salida);
    for (const unsigned char *p = (const unsigned char *)texto; *p != '\0'; ++p) {
        if (*p == '"') fputs("\\\"", salida);
        else if (*p == '\\') fputs("\\\\", salida);
        else if (*p < 0x20) fprintf(salida, "\\u%04x", *p);
        else fputc(*p, salida);
    }
    fputc('"', salida);
}

int clones_escribir_json(const ClonLista *lista, size_t archivos, FILE *salida)
{
    fprintf(salida, "{\"archivos\": %zu, \"omitidos\": [", archivos);
    for (size_t i = 0; i < lista->omitidos; ++i) {
        if (i > 0) fputs(", ", salida);
        escribir_texto_json(lista->nombres_omitidos[i], salida);
    }
    fputs("], \"clones\": [", salida);
    for (size_t i = 0; i < lista->cantidad; ++i) {
        const Clon *c = &lista->elementos[i];
        fprintf(salida, "%s\n  {\"tipo\": %d, \"que\": ", i > 0 ? "," : "", (int)c->tipo);
        escribir_texto_json(c->que, salida);
        fprintf(salida, ", \"nodos\": %zu, \"fragmentos\": [", c->nodos);
        for (size_t j = 0; j < c->cantidad; ++j) {
            const ClonFragmento *f = &c->fragmentos[j];
            fprintf(salida, "%s\n    {\"archivo\": ", j > 0 ? "," : "");
            escribir_texto_json(f->archivo, salida);
            fprintf(salida, ", \"linea\": %zu, \"columna\": %zu, \"linea_fin\": %zu, \"columna_fin\": %zu}",
                    f->linea, f->columna, f->linea_fin, f->columna_fin);
        }
        fputs("]}", salida);
    }
    fputs(lista->cantidad > 0 ? "\n]}\n" : "]}\n", salida);
    return ferror(salida) ? -1 : 0;
}
