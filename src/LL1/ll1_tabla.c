/*
 * Conjuntos PRIMERO y SIGUIENTE, y tabla de análisis predictivo M[A, a].
 *
 * Se siguen los algoritmos de las diapositivas de Análisis Sintáctico:
 *   - PRIMERO:   diapositiva 25.
 *   - SIGUIENTE: diapositiva 26.
 *   - Tabla M:   diapositiva 27.
 *
 * Los tres son algoritmos de "punto fijo": se repite una pasada sobre todas
 * las producciones hasta que ningún conjunto cambie. Como los conjuntos
 * solo crecen y son finitos, eso termina.
 */
#include "ll1.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Operaciones con conjuntos                                           */
/* ------------------------------------------------------------------ */

/* Agrega un terminal. Devuelve 1 si no estaba (el conjunto cambió). */
static int agregar(Ll1Conjunto *conjunto, int terminal)
{
    if (conjunto->tiene[terminal]) return 0;
    conjunto->tiene[terminal] = 1;
    return 1;
}

/* destino = destino ∪ (origen − {ε}). Devuelve 1 si destino cambió. */
static int unir_sin_epsilon(Ll1Conjunto *destino, const Ll1Conjunto *origen)
{
    int cambio = 0;
    for (int t = 0; t < LL1_TERMINALES; ++t) {
        if (origen->tiene[t]) cambio |= agregar(destino, t);
    }
    return cambio;
}

/* ------------------------------------------------------------------ */
/* PRIMERO                                                             */
/* ------------------------------------------------------------------ */

/*
 * PRIMERO de una cadena X1 X2 ... Xn, usando los PRIMERO de los no
 * terminales que ya se tengan calculados:
 *   - se agrega PRIMERO(X1) sin ε;
 *   - si X1 puede ser ε, se agrega también PRIMERO(X2) sin ε, y así;
 *   - si TODOS pueden ser ε (o la cadena está vacía), se agrega ε.
 * Un terminal es su propio PRIMERO y nunca es ε.
 */
void ll1_primero_de_cadena(const Ll1Tabla *tabla, const int *simbolos, int cantidad,
                           Ll1Conjunto *resultado)
{
    memset(resultado, 0, sizeof *resultado);
    for (int i = 0; i < cantidad; ++i) {
        int x = simbolos[i];
        if (LL1_ES_TERMINAL(x)) {
            agregar(resultado, x);
            return;  /* Un terminal no deriva ε: aquí se detiene. */
        }
        const Ll1Conjunto *primero_x = &tabla->primero[LL1_INDICE(x)];
        unir_sin_epsilon(resultado, primero_x);
        if (!primero_x->epsilon) return;  /* Xi no puede desaparecer: se detiene. */
    }
    resultado->epsilon = 1;  /* Todos los símbolos pueden ser ε. */
}

/* Para cada producción A → α:  PRIMERO(A) = PRIMERO(A) ∪ PRIMERO(α). */
static void calcular_primero(Ll1Tabla *tabla)
{
    int cambio;
    do {
        cambio = 0;
        for (int p = 0; p < ll1_cantidad_producciones; ++p) {
            const Ll1Produccion *prod = &ll1_producciones[p];
            Ll1Conjunto primero_cuerpo;
            ll1_primero_de_cadena(tabla, prod->cuerpo, ll1_largo_cuerpo(prod), &primero_cuerpo);

            Ll1Conjunto *primero_a = &tabla->primero[LL1_INDICE(prod->cabeza)];
            cambio |= unir_sin_epsilon(primero_a, &primero_cuerpo);
            if (primero_cuerpo.epsilon && !primero_a->epsilon) {
                primero_a->epsilon = 1;
                cambio = 1;
            }
        }
    } while (cambio);
}

/* ------------------------------------------------------------------ */
/* SIGUIENTE                                                           */
/* ------------------------------------------------------------------ */

/*
 * Algoritmo de la diapositiva 26:
 *   1. $ ∈ SIGUIENTE(programa), porque el programa es el símbolo inicial.
 *   2. Para cada producción A → α B β, con B no terminal:
 *        - PRIMERO(β) − {ε} ⊆ SIGUIENTE(B);
 *        - si β puede ser ε (o no hay β): SIGUIENTE(A) ⊆ SIGUIENTE(B).
 *   3. Repetir hasta que no haya cambios.
 */
static void calcular_siguiente(Ll1Tabla *tabla)
{
    agregar(&tabla->siguiente[LL1_INDICE(NT_PROGRAMA)], LL1_FIN);

    int cambio;
    do {
        cambio = 0;
        for (int p = 0; p < ll1_cantidad_producciones; ++p) {
            const Ll1Produccion *prod = &ll1_producciones[p];
            int largo = ll1_largo_cuerpo(prod);

            for (int i = 0; i < largo; ++i) {
                int b = prod->cuerpo[i];
                if (LL1_ES_TERMINAL(b)) continue;  /* SIGUIENTE solo es de no terminales. */

                /* β = lo que viene después de B en esta producción. */
                Ll1Conjunto primero_beta;
                ll1_primero_de_cadena(tabla, &prod->cuerpo[i + 1], largo - (i + 1),
                                      &primero_beta);

                Ll1Conjunto *siguiente_b = &tabla->siguiente[LL1_INDICE(b)];
                cambio |= unir_sin_epsilon(siguiente_b, &primero_beta);
                if (primero_beta.epsilon) {
                    cambio |= unir_sin_epsilon(siguiente_b,
                                               &tabla->siguiente[LL1_INDICE(prod->cabeza)]);
                }
            }
        }
    } while (cambio);
}

/* ------------------------------------------------------------------ */
/* Tabla M[A, a]                                                       */
/* ------------------------------------------------------------------ */

/* Pone la producción p en M[A, a]. Si la celda ya tenía otra, es un conflicto. */
static void poner_en_celda(Ll1Tabla *tabla, int a_no_terminal, int terminal, int p)
{
    short *celda = &tabla->m[LL1_INDICE(a_no_terminal)][terminal];
    if (*celda == -1 || *celda == p) {
        *celda = (short)p;
        return;
    }
    /* Dos producciones en la misma celda: la gramática NO sería LL(1). */
    if (tabla->conflictos < LL1_MAX_CONFLICTOS) {
        Ll1Conflicto *c = &tabla->lista_conflictos[tabla->conflictos];
        c->no_terminal = a_no_terminal;
        c->terminal = terminal;
        c->produccion1 = *celda;
        c->produccion2 = p;
    }
    tabla->conflictos++;
}

/*
 * Algoritmo de la diapositiva 27. Para cada producción A → α:
 *   - para cada a ∈ PRIMERO(α), poner A → α en M[A, a];
 *   - si ε ∈ PRIMERO(α), poner A → α en M[A, b] para cada b ∈ SIGUIENTE(A).
 * Las celdas que quedan vacías (-1) son errores de sintaxis.
 */
static void construir_tabla(Ll1Tabla *tabla)
{
    for (int a = 0; a < LL1_NO_TERMINALES; ++a) {
        for (int t = 0; t < LL1_TERMINALES; ++t) tabla->m[a][t] = -1;
    }

    for (int p = 0; p < ll1_cantidad_producciones; ++p) {
        const Ll1Produccion *prod = &ll1_producciones[p];
        Ll1Conjunto primero_alfa;
        ll1_primero_de_cadena(tabla, prod->cuerpo, ll1_largo_cuerpo(prod), &primero_alfa);

        for (int t = 0; t < LL1_TERMINALES; ++t) {
            if (primero_alfa.tiene[t]) poner_en_celda(tabla, prod->cabeza, t, p);
        }
        if (primero_alfa.epsilon) {
            const Ll1Conjunto *siguiente_a = &tabla->siguiente[LL1_INDICE(prod->cabeza)];
            for (int t = 0; t < LL1_TERMINALES; ++t) {
                if (siguiente_a->tiene[t]) poner_en_celda(tabla, prod->cabeza, t, p);
            }
        }
    }
}

/* La tabla se calcula una sola vez y se guarda aquí. */
static Ll1Tabla tabla_global;
static int tabla_lista = 0;

const Ll1Tabla *ll1_tabla(void)
{
    if (!tabla_lista) {
        memset(&tabla_global, 0, sizeof tabla_global);
        calcular_primero(&tabla_global);
        calcular_siguiente(&tabla_global);
        construir_tabla(&tabla_global);
        tabla_lista = 1;
    }
    return &tabla_global;
}

/* ------------------------------------------------------------------ */
/* Reporte en Markdown                                                 */
/* ------------------------------------------------------------------ */

/* Escribe una producción como "A → X Y Z" (o "A → ε"). */
static void escribir_produccion(FILE *salida, int p)
{
    const Ll1Produccion *prod = &ll1_producciones[p];
    fprintf(salida, "%s →", ll1_nombre(prod->cabeza));
    int largo = ll1_largo_cuerpo(prod);
    if (largo == 0) fprintf(salida, " ε");
    for (int i = 0; i < largo; ++i) fprintf(salida, " %s", ll1_nombre(prod->cuerpo[i]));
}

/* Escribe un conjunto como "{ a, b, ε }". */
static void escribir_conjunto(FILE *salida, const Ll1Conjunto *conjunto)
{
    const char *separador = "";
    fprintf(salida, "{ ");
    for (int t = 0; t < LL1_TERMINALES; ++t) {
        if (!conjunto->tiene[t]) continue;
        fprintf(salida, "%s`%s`", separador, ll1_nombre(t));
        separador = ", ";
    }
    if (conjunto->epsilon) fprintf(salida, "%sε", separador);
    fprintf(salida, " }");
}

void ll1_escribir_markdown(FILE *salida)
{
    const Ll1Tabla *tabla = ll1_tabla();

    fprintf(salida, "# Tabla LL(1) de BAL (generada)\n\n");
    fprintf(salida, "Archivo generado con `make tabla-ll1`; no se edita a mano.\n");
    fprintf(salida, "La explicación está en [ll1.md](ll1.md).\n\n");
    fprintf(salida, "- Producciones: %d\n", ll1_cantidad_producciones);
    fprintf(salida, "- No terminales: %d\n", LL1_NO_TERMINALES);
    fprintf(salida, "- Conflictos (celdas con más de una producción): %d", tabla->conflictos);
    fprintf(salida, tabla->conflictos == 0 ? " → **la gramática es LL(1)**\n\n"
                                           : " → la gramática NO es LL(1)\n\n");

    fprintf(salida, "## Producciones\n\n");
    for (int p = 0; p < ll1_cantidad_producciones; ++p) {
        fprintf(salida, "%3d. ", p);
        escribir_produccion(salida, p);
        fprintf(salida, "\n");
    }

    fprintf(salida, "\n## PRIMERO y SIGUIENTE\n\n");
    fprintf(salida, "| No terminal | PRIMERO | SIGUIENTE |\n|---|---|---|\n");
    for (int a = 0; a < LL1_NO_TERMINALES; ++a) {
        fprintf(salida, "| %s | ", ll1_nombre(a + LL1_TERMINALES));
        escribir_conjunto(salida, &tabla->primero[a]);
        fprintf(salida, " | ");
        escribir_conjunto(salida, &tabla->siguiente[a]);
        fprintf(salida, " |\n");
    }

    /* La tabla completa tiene 62 filas y 64 columnas, casi todas vacías.
     * Se escribe solo lo que tiene: por cada no terminal, sus celdas llenas. */
    fprintf(salida, "\n## Tabla M[A, a]\n\n");
    fprintf(salida, "Solo se listan las celdas con producción; las demás son error.\n\n");
    fprintf(salida, "| A | a | M[A, a] |\n|---|---|---|\n");
    for (int a = 0; a < LL1_NO_TERMINALES; ++a) {
        for (int t = 0; t < LL1_TERMINALES; ++t) {
            int p = tabla->m[a][t];
            if (p < 0) continue;
            fprintf(salida, "| %s | `%s` | ", ll1_nombre(a + LL1_TERMINALES), ll1_nombre(t));
            escribir_produccion(salida, p);
            fprintf(salida, " |\n");
        }
    }

    if (tabla->conflictos > 0) {
        fprintf(salida, "\n## Conflictos\n\n");
        for (int i = 0; i < tabla->conflictos && i < LL1_MAX_CONFLICTOS; ++i) {
            const Ll1Conflicto *c = &tabla->lista_conflictos[i];
            fprintf(salida, "- M[%s, %s]: producciones %d y %d\n",
                    ll1_nombre(c->no_terminal), ll1_nombre(c->terminal),
                    c->produccion1, c->produccion2);
        }
    }
}
