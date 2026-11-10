/*
 * Eliminación de código muerto (DCE) con análisis de variables vivas.
 * La explicación completa está en include/dce.h.
 *
 * El archivo tiene cuatro partes:
 *   1. Qué usa, qué define y qué efectos tiene cada instrucción de la IR.
 *   2. La lista de variables del programa (para numerarlas).
 *   3. El análisis de variables vivas: use, def, IN y OUT (diapositiva 25).
 *   4. La eliminación: recorrer cada bloque hacia atrás quitando las
 *      asignaciones a variables que no están vivas (diapositiva 28).
 */
#include "dce.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* 1. Usos, definiciones y efectos de cada instrucción                */
/* ================================================================== */

/*
 * ¿Es este operando una VARIABLE? Los operandos de la IR son cadenas, y
 * pueden ser:
 *   - variables del programa (x, total) o temporales ($t3): SÍ;
 *   - números (5, -2), textos ("hola"), caracteres ('a') y los valores
 *     declare_true / declare_false: NO, son constantes.
 */
static int es_variable(const char *s)
{
    if (s == NULL || s[0] == '\0') return 0;
    if (strcmp(s, "declare_true") == 0 || strcmp(s, "declare_false") == 0) return 0;
    return s[0] == '$' || s[0] == '_' || (s[0] >= 'a' && s[0] <= 'z') ||
           (s[0] >= 'A' && s[0] <= 'Z');
}

/* ¿Es "s" un número entero escrito tal cual (con signo opcional)?
 * Si lo es, deja su valor en "valor". */
static int es_numero(const char *s, long long *valor)
{
    if (s == NULL || s[0] == '\0') return 0;
    char *fin = NULL;
    long long v = strtoll(s, &fin, 10);
    if (fin == s || *fin != '\0') return 0;
    *valor = v;
    return 1;
}

/*
 * La variable que la instrucción ASIGNA (su "def"), o NULL si no asigna
 * ninguna. En la IR el destino está en "dst":
 *     x = a          (copia)          $t1 = a gauss b   (binaria)
 *     $t2 = ~ a      (unaria)         $t3 = ref v[i]    (referencia)
 *     $t4 = load $t3 (lectura)        $t5 = size xs     (tamaño)
 *     $t6 = call f, 2 (llamada)       param_decl n      (parámetro)
 * En store, add y remove el "dst" NO se asigna: es el lugar de memoria donde
 * se escribe, así que cuenta como un USO (ver usos()).
 */
static const char *definicion(const IrInstr *x)
{
    switch (x->op) {
        case IR_COPY: case IR_BIN: case IR_UNARY: case IR_INDEX_REF:
        case IR_LOAD: case IR_SIZE: case IR_CALL: case IR_PARAM_DECL:
            return x->dst;
        default:
            return NULL;
    }
}

/*
 * Las variables que la instrucción LEE (su "use"). Deja hasta 3 en "u" y
 * devuelve cuántas. Las constantes no cuentan.
 * Ojo: en "call f, 2" ni "f" (el nombre de la función) ni "2" (la cantidad
 * de argumentos) son variables; los argumentos ya se leyeron en los "arg".
 */
static int usos(const IrInstr *x, const char *u[3])
{
    const char *posibles[3] = {NULL, NULL, NULL};
    switch (x->op) {
        case IR_COPY: case IR_UNARY: case IR_LOAD: case IR_SIZE:
        case IR_JFALSE: case IR_JTRUE: case IR_ARG: case IR_RETURN:
            posibles[0] = x->a;
            break;
        case IR_BIN: case IR_INDEX_REF:
            posibles[0] = x->a;
            posibles[1] = x->b;
            break;
        case IR_STORE: case IR_LIST_ADD: case IR_LIST_REMOVE:
            posibles[0] = x->dst;  /* Dónde se escribe. */
            posibles[1] = x->a;    /* Qué se escribe. */
            break;
        case IR_ARRAY_DIM:
            posibles[0] = x->dst;  /* El arreglo. */
            posibles[1] = x->b;    /* El tamaño de la dimensión. */
            break;
        default:
            break;
    }
    int cuantos = 0;
    for (int i = 0; i < 3; ++i) {
        if (es_variable(posibles[i])) u[cuantos++] = posibles[i];
    }
    return cuantos;
}

/*
 * ¿Puede esta operación binaria FALLAR al ejecutarse? Entonces quitarla
 * cambiaría el comportamiento (el programa dejaría de dar el error), y la
 * regla de oro (diapositiva 12) no lo permite.
 *   - euclides y euler dividen: fallan si el divisor es 0. Solo es seguro
 *     si el divisor es un número escrito distinto de 0.
 *   - descartes es la potencia: el backend no acepta exponentes negativos.
 *     Solo es seguro si el exponente es un número escrito >= 0.
 */
static int puede_fallar(const IrInstr *x)
{
    if (x->op != IR_BIN || x->operador == NULL) return 0;
    long long valor = 0;
    if (strcmp(x->operador, "euclides") == 0 || strcmp(x->operador, "euler") == 0) {
        return !(es_numero(x->b, &valor) && valor != 0);
    }
    if (strcmp(x->operador, "descartes") == 0) {
        return !(es_numero(x->b, &valor) && valor >= 0);
    }
    return 0;
}

/*
 * ¿Se puede eliminar esta instrucción si su resultado no se usa?
 * Solo las que CALCULAN un valor sin hacer nada más: copiar, operar,
 * calcular una referencia, leer memoria o pedir un tamaño.
 * Todo lo demás se conserva siempre (ver include/dce.h): llamadas,
 * escrituras en memoria, declaraciones, saltos, etiquetas, retornos...
 */
static int se_puede_eliminar(const IrInstr *x)
{
    switch (x->op) {
        case IR_COPY: case IR_UNARY: case IR_INDEX_REF: case IR_LOAD: case IR_SIZE:
            return 1;
        case IR_BIN:
            return !puede_fallar(x);
        default:
            return 0;  /* IR_CALL entre ellas: la función puede tener efectos. */
    }
}

/* Escribe la instrucción en "texto" con el mismo formato que ir_imprimir.
 * Solo hace falta para las que se pueden eliminar. */
static void describir(const IrInstr *x, char *texto, size_t largo)
{
    switch (x->op) {
        case IR_COPY:      snprintf(texto, largo, "%s = %s", x->dst, x->a); break;
        case IR_BIN:       snprintf(texto, largo, "%s = %s %s %s", x->dst, x->a, x->operador, x->b); break;
        case IR_UNARY:     snprintf(texto, largo, "%s = %s %s", x->dst, x->operador, x->a); break;
        case IR_INDEX_REF: snprintf(texto, largo, "%s = ref %s[%s]", x->dst, x->a, x->b); break;
        case IR_LOAD:      snprintf(texto, largo, "%s = load %s", x->dst, x->a); break;
        case IR_SIZE:      snprintf(texto, largo, "%s = size %s", x->dst, x->a); break;
        default:           snprintf(texto, largo, "(instruccion %d)", (int)x->op); break;
    }
}

/* ================================================================== */
/* 2. Lista de variables                                               */
/* ================================================================== */

/* Para usar conjuntos de 0/1, cada variable recibe un número: su posición
 * en la lista ordenada alfabéticamente. */

static int comparar_nombres(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Agrega "nombre" a la lista (sin ordenar todavía). 0 bien, 2 sin memoria. */
static int agregar_nombre(char ***lista, size_t *cantidad, size_t *capacidad, const char *nombre)
{
    if (*cantidad == *capacidad) {
        size_t nueva = *capacidad ? *capacidad * 2 : 32;
        char **mas = realloc(*lista, nueva * sizeof *mas);
        if (mas == NULL) return 2;
        *lista = mas;
        *capacidad = nueva;
    }
    (*lista)[(*cantidad)++] = (char *)nombre;  /* Por ahora apunta a la IR. */
    return 0;
}

/* Arma la lista de variables de "ir": todos los usos y definiciones, sin
 * repetir, en orden alfabético. Cada nombre es una copia propia. */
static int armar_variables(const IrPrograma *ir, DceVivas *v)
{
    char **lista = NULL;
    size_t cantidad = 0, capacidad = 0;

    for (size_t i = 0; i < ir->cantidad; ++i) {
        const IrInstr *x = &ir->codigo[i];
        const char *u[3];
        int n = usos(x, u);
        for (int k = 0; k < n; ++k) {
            if (agregar_nombre(&lista, &cantidad, &capacidad, u[k]) != 0) goto sin_memoria;
        }
        const char *d = definicion(x);
        if (es_variable(d) && agregar_nombre(&lista, &cantidad, &capacidad, d) != 0) goto sin_memoria;
    }

    /* Ordenar y quitar repetidos. */
    if (cantidad > 0) qsort(lista, cantidad, sizeof *lista, comparar_nombres);
    size_t unicos = 0;
    for (size_t i = 0; i < cantidad; ++i) {
        if (unicos > 0 && strcmp(lista[unicos - 1], lista[i]) == 0) continue;
        lista[unicos++] = lista[i];
    }

    /* Copias propias: la IR puede cambiar (el DCE le quita instrucciones). */
    v->variables = calloc(unicos ? unicos : 1, sizeof *v->variables);
    if (v->variables == NULL) goto sin_memoria;
    for (size_t i = 0; i < unicos; ++i) {
        size_t largo = strlen(lista[i]) + 1;
        v->variables[i] = malloc(largo);
        if (v->variables[i] == NULL) {
            v->cantidad_variables = i;
            goto sin_memoria;
        }
        memcpy(v->variables[i], lista[i], largo);
    }
    v->cantidad_variables = unicos;
    free(lista);
    return 0;

sin_memoria:
    free(lista);
    return 2;
}

/* El número de una variable, o SIZE_MAX si no está en la lista. */
static size_t numero_de(const DceVivas *v, const char *nombre)
{
    if (nombre == NULL || v->cantidad_variables == 0) return SIZE_MAX;
    char *const *encontrado = bsearch(&nombre, v->variables, v->cantidad_variables,
                                      sizeof *v->variables, comparar_nombres);
    return encontrado ? (size_t)(encontrado - v->variables) : SIZE_MAX;
}

/* ================================================================== */
/* 3. Variables vivas (diapositiva 25)                                 */
/* ================================================================== */

void dce_liberar_vivas(DceVivas *v)
{
    if (v == NULL) return;
    for (size_t i = 0; i < v->cantidad_variables; ++i) free(v->variables[i]);
    free(v->variables);
    free(v->usa);
    free(v->define);
    free(v->entrada);
    free(v->salida);
    memset(v, 0, sizeof *v);
}

int dce_calcular_vivas(const IrPrograma *ir, DceVivas *v)
{
    if (ir == NULL || v == NULL) return 2;
    memset(v, 0, sizeof *v);
    if (armar_variables(ir, v) != 0) {
        dce_liberar_vivas(v);
        return 2;
    }

    size_t nb = ir->cantidad_bloques, nv = v->cantidad_variables;
    v->cantidad_bloques = nb;
    if (nb > 0 && nv > SIZE_MAX / nb) {
        dce_liberar_vivas(v);
        return 2;
    }
    size_t celdas = (nb > 0 && nv > 0) ? nb * nv : 1;
    v->usa = calloc(celdas, 1);
    v->define = calloc(celdas, 1);
    v->entrada = calloc(celdas, 1);
    v->salida = calloc(celdas, 1);
    if (!v->usa || !v->define || !v->entrada || !v->salida) {
        dce_liberar_vivas(v);
        return 2;
    }

    /*
     * Paso 1: use_B y def_B de cada bloque, recorriéndolo hacia ADELANTE.
     * Una variable está en use_B si el bloque la LEE antes de asignarla
     * (su valor viene de antes del bloque). Por eso, en cada instrucción,
     * primero se miran los usos y después la definición:
     *     x = x gauss 1   ->  x está en use (se lee el x de antes) y en def.
     */
    for (size_t b = 0; b < nb; ++b) {
        unsigned char *usa = v->usa + b * nv, *define = v->define + b * nv;
        for (size_t i = ir->bloques[b].inicio; i < ir->bloques[b].fin; ++i) {
            const char *u[3];
            int n = usos(&ir->codigo[i], u);
            for (int k = 0; k < n; ++k) {
                size_t j = numero_de(v, u[k]);
                if (j != SIZE_MAX && !define[j]) usa[j] = 1;
            }
            size_t d = numero_de(v, definicion(&ir->codigo[i]));
            if (d != SIZE_MAX) define[d] = 1;
        }
    }

    /*
     * Paso 2: algoritmo iterativo hasta el punto fijo (diapositiva 27).
     * Se empieza con todos los IN y OUT vacíos y se repite:
     *     OUT[B] = unión de IN[S] para cada sucesor S
     *     IN[B]  = use_B ∪ (OUT[B] − def_B)
     * hasta que ninguna vuelta cambie nada. Un bloque sin sucesores (el
     * final de una función) queda con OUT = ∅, como OUT[SALIDA] = ∅.
     * Como el análisis va hacia atrás, los bloques se recorren del último
     * al primero: así la información llega en menos vueltas.
     */
    int cambio = 1;
    while (cambio) {
        cambio = 0;
        v->iteraciones++;
        for (size_t b = nb; b-- > 0;) {
            unsigned char *out = v->salida + b * nv, *in = v->entrada + b * nv;
            const unsigned char *usa = v->usa + b * nv, *define = v->define + b * nv;
            const IrBloque *bloque = &ir->bloques[b];
            for (size_t j = 0; j < nv; ++j) {
                unsigned char nuevo_out = 0;
                for (size_t s = 0; s < bloque->cantidad_sucesores; ++s) {
                    nuevo_out |= v->entrada[bloque->sucesores[s] * nv + j];
                }
                unsigned char nuevo_in = usa[j] | (nuevo_out & !define[j]);
                if (nuevo_out != out[j] || nuevo_in != in[j]) cambio = 1;
                out[j] = nuevo_out;
                in[j] = nuevo_in;
            }
        }
    }
    return 0;
}

int dce_esta_viva(const DceVivas *v, size_t bloque, const char *variable, int salida)
{
    if (v == NULL || bloque >= v->cantidad_bloques) return 0;
    size_t j = numero_de(v, variable);
    if (j == SIZE_MAX) return 0;
    const unsigned char *conjunto = salida ? v->salida : v->entrada;
    return conjunto[bloque * v->cantidad_variables + j];
}

/* Escribe un conjunto como "{ a, b }". */
static void escribir_conjunto(const DceVivas *v, const unsigned char *conjunto, FILE *f)
{
    const char *separador = "";
    fputs("{ ", f);
    for (size_t j = 0; j < v->cantidad_variables; ++j) {
        if (!conjunto[j]) continue;
        fprintf(f, "%s%s", separador, v->variables[j]);
        separador = ", ";
    }
    fputs(separador[0] ? " }" : "}", f);
}

int dce_imprimir_vivas(const DceVivas *v, FILE *f)
{
    if (v == NULL || f == NULL) return 2;
    fprintf(f, "Variables vivas (punto fijo en %zu iteraciones)\n", v->iteraciones);
    size_t nv = v->cantidad_variables;
    for (size_t b = 0; b < v->cantidad_bloques; ++b) {
        fprintf(f, "[B%zu]\n  use = ", b);
        escribir_conjunto(v, v->usa + b * nv, f);
        fputs("\n  def = ", f);
        escribir_conjunto(v, v->define + b * nv, f);
        fputs("\n  IN  = ", f);
        escribir_conjunto(v, v->entrada + b * nv, f);
        fputs("\n  OUT = ", f);
        escribir_conjunto(v, v->salida + b * nv, f);
        fputc('\n', f);
    }
    return ferror(f) ? 2 : 0;
}

/* ================================================================== */
/* 4. Eliminación de código muerto (diapositiva 28)                    */
/* ================================================================== */

/* Guarda en el resultado una instrucción eliminada. 0 bien, 2 sin memoria. */
static int anotar(DceResultado *r, const IrInstr *x)
{
    if (r->cantidad_eliminadas == r->capacidad) {
        size_t nueva = r->capacidad ? r->capacidad * 2 : 16;
        DceEliminada *mas = realloc(r->eliminadas, nueva * sizeof *mas);
        if (mas == NULL) return 2;
        r->eliminadas = mas;
        r->capacidad = nueva;
    }
    DceEliminada *e = &r->eliminadas[r->cantidad_eliminadas++];
    e->linea = x->linea;
    describir(x, e->texto, sizeof e->texto);
    return 0;
}

/*
 * Una pasada: con las variables vivas ya calculadas, marca en "borrar" las
 * instrucciones muertas. Cada bloque se recorre hacia ATRÁS llevando el
 * conjunto de variables vivas en ese punto, empezando con OUT[B]
 * (como en el ejercicio 4 de la presentación):
 *
 *   - si la instrucción asigna x, x NO está viva y se puede eliminar,
 *     se marca y NO se tocan las vivas (sus usos ya no cuentan: así una
 *     cadena  a = ..., b = a, c = b  con c muerta cae entera en una pasada);
 *   - si no, se aplica la regla de la diapositiva 25 a esa instrucción:
 *     vivas = usos ∪ (vivas − {x}).
 *
 * Devuelve cuántas marcó, o SIZE_MAX si faltó memoria.
 */
static size_t marcar_muertas(const IrPrograma *ir, const DceVivas *v, unsigned char *borrar,
                             DceResultado *r)
{
    size_t nv = v->cantidad_variables, marcadas = 0;
    unsigned char *vivas = malloc(nv ? nv : 1);
    if (vivas == NULL) return SIZE_MAX;

    /* Se cuentan de nuevo en cada pasada; queda lo de la última. */
    r->llamadas_conservadas = 0;
    r->riesgosas_conservadas = 0;

    for (size_t b = 0; b < ir->cantidad_bloques; ++b) {
        if (nv) memcpy(vivas, v->salida + b * nv, nv);
        for (size_t i = ir->bloques[b].fin; i-- > ir->bloques[b].inicio;) {
            const IrInstr *x = &ir->codigo[i];
            size_t d = numero_de(v, definicion(x));
            int muerta = d != SIZE_MAX && !vivas[d];

            if (muerta && se_puede_eliminar(x)) {
                borrar[i] = 1;
                marcadas++;
                continue;
            }
            /* Muerta pero con efectos o con riesgo de fallar: se queda. */
            if (muerta && x->op == IR_CALL) r->llamadas_conservadas++;
            if (muerta && puede_fallar(x)) r->riesgosas_conservadas++;

            if (d != SIZE_MAX) vivas[d] = 0;
            const char *u[3];
            int n = usos(x, u);
            for (int k = 0; k < n; ++k) {
                size_t j = numero_de(v, u[k]);
                if (j != SIZE_MAX) vivas[j] = 1;
            }
        }
    }
    free(vivas);
    return marcadas;
}

/* Quita de la IR las instrucciones marcadas y vuelve a armar los bloques. */
static int quitar_marcadas(IrPrograma *ir, const unsigned char *borrar, DceResultado *r)
{
    size_t quedan = 0;
    for (size_t i = 0; i < ir->cantidad; ++i) {
        IrInstr *x = &ir->codigo[i];
        if (borrar[i]) {
            if (anotar(r, x) != 0) return 2;
            free(x->dst);
            free(x->a);
            free(x->b);
            free(x->operador);
            continue;
        }
        ir->codigo[quedan++] = *x;
    }
    ir->cantidad = quedan;

    /* Los índices de las instrucciones cambiaron: los bloques se calculan
     * de nuevo con la función de la IR. Como nunca se quitan etiquetas ni
     * saltos, el grafo de flujo sigue siendo el mismo. */
    for (size_t b = 0; b < ir->cantidad_bloques; ++b) free(ir->bloques[b].sucesores);
    free(ir->bloques);
    ir->bloques = NULL;
    ir->cantidad_bloques = 0;
    return ir_bloques_basicos(ir);
}

int dce_eliminar(IrPrograma *ir, DceResultado *r)
{
    if (ir == NULL || r == NULL) return 2;
    memset(r, 0, sizeof *r);
    r->instrucciones_antes = ir->cantidad;

    /*
     * Procedimiento de la diapositiva 28:
     *   1. calcular las variables vivas de todo el grafo de flujo;
     *   2. eliminar las asignaciones a variables que no están vivas;
     *   3. repetir: lo eliminado ya no usa sus operandos, y otras
     *      asignaciones pueden haber quedado muertas.
     */
    for (;;) {
        DceVivas vivas;
        if (dce_calcular_vivas(ir, &vivas) != 0) return 2;
        r->pasadas++;

        unsigned char *borrar = calloc(ir->cantidad ? ir->cantidad : 1, 1);
        if (borrar == NULL) {
            dce_liberar_vivas(&vivas);
            return 2;
        }
        size_t marcadas = marcar_muertas(ir, &vivas, borrar, r);
        dce_liberar_vivas(&vivas);

        int estado = 0;
        if (marcadas == SIZE_MAX) estado = 2;
        else if (marcadas > 0) estado = quitar_marcadas(ir, borrar, r);
        free(borrar);
        if (estado != 0) return estado;
        if (marcadas == 0) break;  /* Punto fijo: ya no hay código muerto. */
    }

    r->instrucciones_despues = ir->cantidad;
    return 0;
}

int dce_imprimir_resultado(const DceResultado *r, FILE *f)
{
    if (r == NULL || f == NULL) return 2;
    fprintf(f, "Codigo muerto eliminado: %zu de %zu instrucciones (quedan %zu), en %zu pasadas\n",
            r->cantidad_eliminadas, r->instrucciones_antes, r->instrucciones_despues, r->pasadas);
    for (size_t i = 0; i < r->cantidad_eliminadas; ++i) {
        fprintf(f, "  - linea %zu: %s\n", r->eliminadas[i].linea, r->eliminadas[i].texto);
    }
    if (r->llamadas_conservadas > 0) {
        fprintf(f, "Llamadas conservadas aunque su resultado no se usa (pueden tener efectos): %zu\n",
                r->llamadas_conservadas);
    }
    if (r->riesgosas_conservadas > 0) {
        fprintf(f, "Operaciones conservadas porque pueden fallar (division o potencia): %zu\n",
                r->riesgosas_conservadas);
    }
    return ferror(f) ? 2 : 0;
}

void dce_liberar_resultado(DceResultado *r)
{
    if (r == NULL) return;
    free(r->eliminadas);
    memset(r, 0, sizeof *r);
}
