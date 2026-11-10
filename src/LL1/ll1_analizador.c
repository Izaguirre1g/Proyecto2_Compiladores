/*
 * Algoritmo de análisis predictivo no recursivo (diapositiva 28).
 *
 * Componentes:
 *   - la entrada: los tokens, terminando en $ (TOKEN_EOF);
 *   - una pila que empieza con  $ programa  (programa en el tope);
 *   - la tabla M[A, a] de ll1_tabla.c.
 *
 * Reglas, mirando el tope X de la pila y el token actual a:
 *   1. X es terminal y X = a: desapilar y avanzar en la entrada.
 *      Si además X = a = $, la entrada se ACEPTA.
 *   2. X es no terminal: usar M[X, a]. Si tiene la producción X → Y1...Yk,
 *      se desapila X y se apilan Yk ... Y1 (Y1 queda en el tope).
 *   3. En cualquier otro caso (terminal distinto, o celda vacía): ERROR.
 *
 * Además de decir si hay error, este analizador calcula qué tokens se
 * ESPERABAN en esa posición. Eso es lo que usa el IDE para proponer
 * correcciones y sugerencias.
 */
#include "ll1.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Pila de símbolos                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    int *datos;        /* datos[0] es el fondo ($); datos[cantidad-1] es el tope. */
    size_t cantidad;
    size_t capacidad;
} Pila;

static int apilar(Pila *pila, int simbolo)
{
    if (pila->cantidad == pila->capacidad) {
        size_t nueva = (pila->capacidad == 0) ? 64 : pila->capacidad * 2;
        int *mas = realloc(pila->datos, nueva * sizeof *mas);
        if (mas == NULL) return -1;
        pila->datos = mas;
        pila->capacidad = nueva;
    }
    pila->datos[pila->cantidad++] = simbolo;
    return 0;
}

/* Copia el contenido de "origen" en "destino" (reusa la memoria de destino). */
static int copiar_pila(Pila *destino, const Pila *origen)
{
    destino->cantidad = 0;
    for (size_t i = 0; i < origen->cantidad; ++i) {
        if (apilar(destino, origen->datos[i]) != 0) return -1;
    }
    return 0;
}

/* Regla 2: cambia el no terminal del tope por el cuerpo de la producción p,
 * al revés, para que el primer símbolo del cuerpo quede en el tope. */
static int expandir(Pila *pila, int p)
{
    const Ll1Produccion *prod = &ll1_producciones[p];
    pila->cantidad--;  /* Desapilar la cabeza. */
    for (int i = ll1_largo_cuerpo(prod) - 1; i >= 0; --i) {
        if (apilar(pila, prod->cuerpo[i]) != 0) return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Tokens esperados                                                    */
/* ------------------------------------------------------------------ */

/*
 * ¿Puede venir el terminal t con esta pila? Se simula el algoritmo sobre
 * una copia de la pila, solo con ese token: se expanden no terminales con
 * la tabla hasta que en el tope quede un terminal. Si ese terminal es t,
 * el token se puede leer. Si una celda está vacía, no.
 *
 * Termina siempre: la gramática no tiene recursividad por la izquierda, así
 * que no se puede expandir para siempre sin leer nada.
 */
static int puede_seguir(const Ll1Tabla *tabla, const Pila *pila, int t, Pila *copia)
{
    if (copiar_pila(copia, pila) != 0) return -1;
    while (copia->cantidad > 0) {
        int x = copia->datos[copia->cantidad - 1];
        if (LL1_ES_TERMINAL(x)) return x == t;
        int p = tabla->m[LL1_INDICE(x)][t];
        if (p < 0) return 0;
        if (expandir(copia, p) != 0) return -1;
    }
    return 0;
}

/* Llena "esperados" con todos los terminales que pueden venir con esta pila. */
static int calcular_esperados(const Ll1Tabla *tabla, const Pila *pila, Ll1Conjunto *esperados)
{
    memset(esperados, 0, sizeof *esperados);
    Pila copia = {0};
    for (int t = 0; t < LL1_TERMINALES; ++t) {
        int puede = puede_seguir(tabla, pila, t, &copia);
        if (puede < 0) {
            free(copia.datos);
            return -1;
        }
        esperados->tiene[t] = (unsigned char)puede;
    }
    free(copia.datos);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Inserción predicha por la tabla                                     */
/* ------------------------------------------------------------------ */

/* Si el no terminal tiene UNA sola producción, devuelve su número; si no, -1. */
static int unica_produccion(int no_terminal)
{
    int encontrada = -1;
    for (int p = 0; p < ll1_cantidad_producciones; ++p) {
        if (ll1_producciones[p].cabeza != no_terminal) continue;
        if (encontrada >= 0) return -1;  /* Tiene más de una. */
        encontrada = p;
    }
    return encontrada;
}

/*
 * Busca los terminales que faltan antes del token "a", siguiendo la tabla
 * de forma obligada desde la pila "pila" (ver ll1_insercion_predicha en ll1.h).
 * Devuelve 1 si los encontró, 0 si la tabla no obliga a uno único, -1 sin memoria.
 */
static int predecir_insercion(const Ll1Tabla *tabla, const Pila *pila, int a,
                              Ll1Insercion *insercion)
{
    Pila copia = {0}, auxiliar = {0};
    int estado = 0;
    memset(insercion, 0, sizeof *insercion);
    if (copiar_pila(&copia, pila) != 0) {
        estado = -1;
        goto fin;
    }

    for (;;) {
        /* ¿Ya se puede leer el token del error? Entonces terminamos. */
        int puede = puede_seguir(tabla, &copia, a, &auxiliar);
        if (puede < 0) {
            estado = -1;
            goto fin;
        }
        if (puede) {
            estado = (insercion->cantidad > 0);
            goto fin;
        }
        if (insercion->cantidad == LL1_MAX_INSERCION) goto fin;  /* Faltaría demasiado. */

        /* Bajar por la pila hasta un terminal, sin tener que elegir. */
        while (copia.cantidad > 0 && !LL1_ES_TERMINAL(copia.datos[copia.cantidad - 1])) {
            int x = copia.datos[copia.cantidad - 1];
            int unica = unica_produccion(x);
            if (tabla->primero[LL1_INDICE(x)].epsilon) {
                copia.cantidad--;          /* X → ε: X desaparece. */
            } else if (unica >= 0) {
                if (expandir(&copia, unica) != 0) {   /* X tiene una sola producción. */
                    estado = -1;
                    goto fin;
                }
            } else {
                goto fin;  /* Varias producciones posibles: la tabla no decide sola. */
            }
        }
        if (copia.cantidad == 0) goto fin;

        /* El terminal del tope es el que el parser predice. */
        int t = copia.datos[copia.cantidad - 1];
        if (t == LL1_FIN || !ll1_texto_fijo(t)) goto fin;  /* Un nombre o un número no se inventan. */
        insercion->terminales[insercion->cantidad++] = t;
        copia.cantidad--;  /* Se "lee" como si estuviera escrito. */
    }

fin:
    if (estado != 1) insercion->cantidad = 0;
    free(copia.datos);
    free(auxiliar.datos);
    return estado;
}

/* ------------------------------------------------------------------ */
/* Traza (para mostrar el algoritmo paso a paso)                       */
/* ------------------------------------------------------------------ */

/* Columna "Pila": del fondo ($) al tope, como en la diapositiva 28. */
static void escribir_pila(FILE *salida, const Pila *pila)
{
    int escritos = 0;
    for (size_t i = 0; i < pila->cantidad; ++i) {
        escritos += fprintf(salida, "%s%s", i ? " " : "", ll1_nombre(pila->datos[i]));
    }
    /* Rellenar para que las columnas queden alineadas (si cabe). */
    fprintf(salida, "%*s", escritos < 60 ? 60 - escritos : 1, "");
}

/* Columna "Entrada": los próximos tokens (máximo 6) y el resto como "...". */
static void escribir_entrada(FILE *salida, const TokenType *tokens, size_t cantidad, size_t i)
{
    int escritos = 0;
    for (size_t k = i; k < cantidad && k < i + 6; ++k) {
        escritos += fprintf(salida, "%s ", ll1_nombre(tokens[k]));
    }
    if (cantidad > i + 6) escritos += fprintf(salida, "... ");
    fprintf(salida, "%*s", escritos < 45 ? 45 - escritos : 1, "");
}

/* ------------------------------------------------------------------ */
/* El algoritmo                                                        */
/* ------------------------------------------------------------------ */

/*
 * El análisis completo. "traza" puede ser NULL; si no, se escribe cada paso.
 *
 * Detalle importante para los tokens esperados: cuando se aplica una
 * producción ε (por ejemplo resto_suma → ε), la pila pierde opciones. Si
 * después el token resulta inválido, mirar la pila de ESE momento daría una
 * lista incompleta. Por eso se guarda una copia de la pila justo después de
 * leer cada token ("pila_al_leer") y los esperados se calculan sobre ella:
 * representa exactamente qué podía venir en esa posición.
 */
static int analizar(const TokenType *tokens, size_t cantidad, Ll1Resultado *resultado,
                    FILE *traza, Ll1Insercion *insercion)
{
    const Ll1Tabla *tabla = ll1_tabla();
    Pila pila = {0}, pila_al_leer = {0};
    int estado = 0;

    memset(resultado, 0, sizeof *resultado);

    /* Pila inicial: $ en el fondo y el símbolo inicial en el tope. */
    if (apilar(&pila, LL1_FIN) != 0 || apilar(&pila, NT_PROGRAMA) != 0 ||
        copiar_pila(&pila_al_leer, &pila) != 0) {
        estado = -1;
        goto fin;
    }

    if (traza != NULL) {
        fprintf(traza, "%-60s%-45s%s\n", "Pila", "Entrada", "Accion");
    }

    size_t i = 0;  /* Índice del token actual. */
    for (;;) {
        /* Se acabaron los tokens dados y no había $: es solo un comienzo
         * de programa, sin errores hasta aquí. */
        if (i == cantidad) {
            resultado->estado = LL1_INCOMPLETA;
            resultado->posicion = i;
            break;
        }

        int x = pila.datos[pila.cantidad - 1];  /* Tope de la pila. */
        int a = (int)tokens[i];                  /* Token actual. */
        if (a < 0 || a >= LL1_TERMINALES) a = TOKEN_ERROR;

        if (traza != NULL) {
            escribir_pila(traza, &pila);
            escribir_entrada(traza, tokens, cantidad, i);
        }

        if (LL1_ES_TERMINAL(x)) {
            if (x != a) {  /* Regla 3: el terminal del tope no coincide. */
                if (traza != NULL) fprintf(traza, "error: se esperaba %s\n", ll1_nombre(x));
                resultado->estado = LL1_ERROR;
                resultado->posicion = i;
                break;
            }
            if (x == LL1_FIN) {  /* Regla 1 con $ = $: aceptar. */
                if (traza != NULL) fprintf(traza, "aceptar\n");
                resultado->estado = LL1_ACEPTADA;
                resultado->posicion = i;
                break;
            }
            /* Regla 1: desapilar y avanzar. */
            if (traza != NULL) fprintf(traza, "desapilar %s\n", ll1_nombre(x));
            pila.cantidad--;
            i++;
            if (copiar_pila(&pila_al_leer, &pila) != 0) {
                estado = -1;
                break;
            }
            continue;
        }

        /* Regla 2: consultar la tabla. */
        int p = tabla->m[LL1_INDICE(x)][a];
        if (p < 0) {  /* Celda vacía: regla 3. */
            if (traza != NULL) fprintf(traza, "error: M[%s, %s] vacia\n", ll1_nombre(x), ll1_nombre(a));
            resultado->estado = LL1_ERROR;
            resultado->posicion = i;
            break;
        }
        if (traza != NULL) {
            fprintf(traza, "%s ->", ll1_nombre(x));
            const Ll1Produccion *prod = &ll1_producciones[p];
            int largo = ll1_largo_cuerpo(prod);
            if (largo == 0) fprintf(traza, " ε");
            for (int k = 0; k < largo; ++k) fprintf(traza, " %s", ll1_nombre(prod->cuerpo[k]));
            fprintf(traza, "\n");
        }
        if (expandir(&pila, p) != 0) {
            estado = -1;
            break;
        }
    }

    /* Si no se aceptó, calcular qué podía venir en esa posición. */
    if (estado == 0 && resultado->estado != LL1_ACEPTADA) {
        estado = calcular_esperados(tabla, &pila_al_leer, &resultado->esperados);
    }

    /* Si se pidió, la inserción que la tabla predice para el error. */
    if (insercion != NULL) {
        memset(insercion, 0, sizeof *insercion);
        if (estado == 0 && resultado->estado == LL1_ERROR) {
            int a = (int)tokens[resultado->posicion];
            if (a < 0 || a >= LL1_TERMINALES) a = TOKEN_ERROR;
            estado = predecir_insercion(tabla, &pila_al_leer, a, insercion);
        }
    }

fin:
    free(pila.datos);
    free(pila_al_leer.datos);
    return estado;
}

int ll1_analizar(const TokenType *tokens, size_t cantidad, Ll1Resultado *resultado)
{
    return analizar(tokens, cantidad, resultado, NULL, NULL);
}

int ll1_trazar(const TokenType *tokens, size_t cantidad, FILE *salida)
{
    Ll1Resultado resultado;
    return analizar(tokens, cantidad, &resultado, salida, NULL);
}

int ll1_insercion_predicha(const TokenType *tokens, size_t cantidad, Ll1Insercion *insercion)
{
    Ll1Resultado resultado;
    int estado = analizar(tokens, cantidad, &resultado, NULL, insercion);
    if (estado < 0) return -1;
    return insercion->cantidad > 0;
}
