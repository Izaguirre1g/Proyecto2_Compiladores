/* =====================================================================
 * reordenamiento.c  --  Reordenamiento seguro dentro de bloques basicos
 * =====================================================================
 *
 * Como funciona
 * -------------
 * El procesador VLIW no tiene forwarding: un valor escrito en el ciclo N
 * recien se puede leer en N+3. Si una instruccion lee un valor que se
 * acaba de calcular, el procesador tiene que esperar (NOPs). Este modulo
 * cambia el orden de las instrucciones de cada bloque basico para meter
 * trabajo independiente entre una escritura y su lectura, SIN cambiar el
 * resultado del programa.
 *
 * Para cada bloque basico:
 *   1. Se separan las partes fijas: etiquetas/func/param al inicio y el
 *      salto, return o endfunc al final (dependencias de control).
 *   2. Se arma un grafo de dependencias (DAG) entre las instrucciones:
 *      RAW, WAR, WAW, memoria y efectos (arg/call).
 *   3. "List scheduling": se elige, ciclo a ciclo, la instruccion lista
 *      con el camino critico mas largo.
 *   4. Se estiman los ciclos del orden viejo y del nuevo; el bloque solo
 *      cambia si el nuevo es MAS RAPIDO.
 *
 * COMO ESTA ORGANIZADO
 * --------------------
 *   1. Lecturas y escrituras de cada instruccion   (usos)
 *   2. Modelo de ciclos                             (ciclos_orden, ro_ciclos)
 *   3. Grafo de dependencias                        (Grafo, construir)
 *   4. List scheduling                              (calendarizar)
 *   5. Analisis de un bloque                        (analizar)
 *   6. Interfaz publica                             (ro_reordenar, ro_analizar_bloque)
 * ===================================================================== */

#include "reordenamiento.h"   /* RoOpciones, RoResumen, IrPrograma */
#include <stdlib.h>           /* malloc, calloc, realloc, free */
#include <string.h>           /* strcmp, memcpy */

/* Nombre especial para "la memoria" (arreglos, listas, store/load).
 * '@' no puede aparecer en un identificador de BAL, asi que no choca. */
#define MEM "@memoria"

/* ===================================================================== */
/* 1. LECTURAS Y ESCRITURAS DE CADA INSTRUCCION                           */
/* ===================================================================== */

/* Que lee y que escribe una instruccion. Hasta 4 lecturas y 2 escrituras
 * (una variable y/o la memoria). */
typedef struct {
    const char *lee[4];  size_t nlee;   /* Nombres que lee */
    const char *escribe[2]; size_t nesc; /* Nombres que escribe */
    int barrera;                        /* 1 si es arg/call (efectos) */
} Usos;

/* Agrega un nombre a la lista de lecturas (ignora NULL). */
static void leer(Usos *u, const char *s) { if (s) u->lee[u->nlee++] = s; }
/* Agrega un nombre a la lista de escrituras (ignora NULL). */
static void escribir(Usos *u, const char *s) { if (s) u->escribe[u->nesc++] = s; }

/* Llena *u segun la operacion. Los numeros ("5") tambien quedan como
 * "lecturas", pero nadie los escribe, asi que no crean dependencias. */
static void usos(const IrInstr *x, Usos *u) {
    *u = (Usos){0};
    switch (x->op) {
    case IR_COPY:  case IR_UNARY: leer(u, x->a); escribir(u, x->dst); break; /* dst = a  /  dst = op a */
    case IR_BIN:   leer(u, x->a); leer(u, x->b); escribir(u, x->dst); break; /* dst = a op b */
    case IR_JFALSE: case IR_JTRUE: case IR_RETURN: leer(u, x->a); break;     /* Leen la condicion o el valor */
    case IR_ARG:   leer(u, x->a); u->barrera = 1; break;                     /* Argumento de una llamada */
    case IR_CALL:  escribir(u, x->dst); u->barrera = 1; break;               /* x->a es la funcion, no una variable */
    case IR_INDEX_REF: leer(u, x->a); leer(u, x->b); escribir(u, x->dst); break; /* Calcula una direccion */
    case IR_LOAD:  leer(u, x->a); leer(u, MEM); escribir(u, x->dst); break;  /* Lee de memoria */
    case IR_STORE: leer(u, x->dst); leer(u, x->a); escribir(u, MEM); break;  /* Escribe en memoria */
    case IR_SIZE:  leer(u, x->a); leer(u, MEM); escribir(u, x->dst); break;  /* El tamano depende de la memoria */
    case IR_LIST_ADD: case IR_LIST_REMOVE:                                    /* Cambian una lista */
        leer(u, x->dst); leer(u, x->a); leer(u, MEM); escribir(u, MEM); break;
    case IR_ARRAY_DIM: leer(u, x->dst); leer(u, x->a); leer(u, x->b); escribir(u, MEM); break;
    case IR_ARRAY_DECL: leer(u, x->a); escribir(u, x->dst); escribir(u, MEM); break;
    case IR_LIST_DECL: escribir(u, x->dst); escribir(u, MEM); break;
    default: break;                     /* label, goto, func, endfunc, param: nada */
    }
}

/* Igualdad de nombres (NULL nunca es igual). */
static int igual(const char *a, const char *b) { return a && b && !strcmp(a, b); }

/* 1 si u lee / escribe el nombre s. */
static int lee_nombre(const Usos *u, const char *s) {
    for (size_t i = 0; i < u->nlee; ++i) if (igual(u->lee[i], s)) return 1;
    return 0;
}
static int escribe_nombre(const Usos *u, const char *s) {
    for (size_t i = 0; i < u->nesc; ++i) if (igual(u->escribe[i], s)) return 1;
    return 0;
}

/* Instrucciones que no ocupan un ciclo del procesador (son marcas). */
static int ocupa_ciclo(IrOp op) {
    return op != IR_LABEL && op != IR_FUNC && op != IR_END_FUNC && op != IR_PARAM_DECL;
}
/* Instrucciones que deben quedar al INICIO del bloque. */
static int fija_inicio(IrOp op) { return op == IR_LABEL || op == IR_FUNC || op == IR_PARAM_DECL; }
/* Instrucciones que terminan el bloque y deben quedar al FINAL. */
static int terminador(IrOp op) {
    return op == IR_JUMP || op == IR_JFALSE || op == IR_JTRUE || op == IR_RETURN || op == IR_END_FUNC;
}

/* ===================================================================== */
/* 2. MODELO DE CICLOS                                                    */
/* ===================================================================== */
/* Modelo sencillo del procesador: una instruccion por ciclo, en orden, y
 * un valor escrito en el ciclo t se puede leer desde t + latencia. Si una
 * lectura llega antes, la instruccion espera (eso son los NOPs). */

/* Momento en que cada nombre queda listo para leerse. */
typedef struct { const char *nombre; size_t listo; } Listo;

/* Ciclos para ejecutar las n instrucciones de v en ese orden. Si `emision`
 * no es NULL, guarda en emision[i] el ciclo en que sale v[i]
 * (RO_SIN_CICLO para las marcas que no ocupan ciclo). */
static size_t ciclos_orden(const IrInstr *const *v, size_t n, unsigned lat, size_t *emision) {
    Listo *tabla = malloc((2 * n + 1) * sizeof *tabla);  /* Como mucho 2 escrituras por instruccion */
    if (!tabla) return n;                                /* Sin memoria: estimacion trivial */
    size_t cant = 0, t = 0;                              /* Entradas en la tabla / ciclo actual */
    for (size_t i = 0; i < n; ++i) {
        if (emision) emision[i] = RO_SIN_CICLO;
        if (!ocupa_ciclo(v[i]->op)) continue;            /* Las marcas no gastan ciclos */
        Usos u; usos(v[i], &u);
        size_t inicio = t;                               /* Lo antes que podria emitirse */
        for (size_t r = 0; r < u.nlee; ++r)              /* Espera a cada valor que lee */
            for (size_t k = 0; k < cant; ++k)
                if (igual(tabla[k].nombre, u.lee[r]) && tabla[k].listo > inicio) inicio = tabla[k].listo;
        for (size_t w = 0; w < u.nesc; ++w) {            /* Anota cuando estaran listos sus resultados */
            size_t k = 0;
            while (k < cant && !igual(tabla[k].nombre, u.escribe[w])) ++k; /* Busca el nombre */
            if (k == cant) tabla[cant++].nombre = u.escribe[w];           /* O lo agrega */
            tabla[k].listo = inicio + lat;
        }
        if (emision) emision[i] = inicio;                /* Ciclo en que sale */
        t = inicio + 1;                                 /* La siguiente va en el ciclo siguiente */
    }
    free(tabla);
    return t;                                            /* Total de ciclos usados */
}

size_t ro_ciclos(const IrPrograma *ir, size_t inicio, size_t fin, unsigned latencia) {
    if (!ir || fin <= inicio) return 0;
    const IrInstr **v = malloc((fin - inicio) * sizeof *v); /* Arreglo de punteros en el orden actual */
    if (!v) return fin - inicio;
    for (size_t i = inicio; i < fin; ++i) v[i - inicio] = &ir->codigo[i];
    size_t c = ciclos_orden(v, fin - inicio, latencia ? latencia : 1, NULL);
    free(v);
    return c;
}

/* ===================================================================== */
/* 3. GRAFO DE DEPENDENCIAS                                               */
/* ===================================================================== */
/* lat[i*m + j] > 0 significa "i debe ir antes que j" y cuantos ciclos
 * despues de i puede emitirse j (latencia para RAW, 1 para lo demas). */

typedef struct {
    size_t m;                 /* Cantidad de nodos (instrucciones de la region) */
    unsigned char *lat;       /* Matriz m x m de latencias (0 = sin arista) */
    unsigned char *tipo;      /* Matriz m x m con los bits RO_DEP_* de cada arista */
    const char **por;         /* Opcional (JSON): variable que causa cada arista */
    size_t raw, war, waw, memoria, efecto, control; /* Contadores para el reporte */
} Grafo;

/* Agrega la arista i -> j con latencia l (se queda con la mayor), anota su
 * tipo t (RO_DEP_*) y, si se pidio, la variable `nombre` que la causa. */
static void arista(Grafo *g, size_t i, size_t j, unsigned l, unsigned t, const char *nombre) {
    size_t k = i * g->m + j;                          /* Celda (i, j) de la matriz */
    if (l > g->lat[k]) g->lat[k] = (unsigned char)l;
    g->tipo[k] |= (unsigned char)t;                   /* Puede tener varios tipos */
    if (g->por && nombre && !g->por[k]) g->por[k] = nombre; /* Se queda con la primera */
}

/* Construye las dependencias de las m instrucciones v[0..m-1].
 * Si la ultima es un terminador, todas las demas deben ir antes (control).
 * Solo se agregan dependencias DIRECTAS (con la escritura mas cercana);
 * las demas quedan cubiertas por transitividad. */
static void construir(Grafo *g, const IrInstr *const *v, size_t m, unsigned lat, int hay_terminador) {
    Usos *u = malloc(m * sizeof *u);                  /* Usos de cada instruccion */
    if (!u) return;
    for (size_t i = 0; i < m; ++i) usos(v[i], &u[i]);
    long barrera = -1;                                /* Ultima arg/call vista */
    for (size_t j = 0; j < m; ++j) {
        /* RAW: j lee r -> depende de la escritura mas cercana de r. */
        for (size_t r = 0; r < u[j].nlee; ++r)
            for (size_t i = j; i-- > 0;)
                if (escribe_nombre(&u[i], u[j].lee[r])) {
                    int es_mem = igual(u[j].lee[r], MEM);
                    arista(g, i, j, lat, es_mem ? RO_DEP_MEMORIA : RO_DEP_RAW,
                           u[j].lee[r]);              /* Debe esperar la latencia */
                    if (es_mem) g->memoria++; else g->raw++;
                    break;                            /* Solo la mas cercana */
                }
        /* WAW y WAR: j escribe w. */
        for (size_t w = 0; w < u[j].nesc; ++w) {
            const char *nom = u[j].escribe[w];
            int es_mem = igual(nom, MEM);
            for (size_t i = j; i-- > 0;) {
                if (escribe_nombre(&u[i], nom)) {     /* WAW: escritura anterior mas cercana */
                    arista(g, i, j, 1, es_mem ? RO_DEP_MEMORIA : RO_DEP_WAW, nom);
                    if (es_mem) g->memoria++; else g->waw++;
                    break;                            /* Antes de esta ya no importa (transitivo) */
                }
                if (lee_nombre(&u[i], nom)) {         /* WAR: alguien lee el valor viejo */
                    arista(g, i, j, 1, es_mem ? RO_DEP_MEMORIA : RO_DEP_WAR, nom);
                    if (es_mem) g->memoria++; else g->war++;
                }
            }
        }
        /* Efectos: arg/call no se mueven respecto a nada de su alrededor. */
        if (u[j].barrera) {
            for (size_t i = barrera < 0 ? 0 : (size_t)barrera; i < j; ++i) {
                arista(g, i, j, 1, RO_DEP_EFECTO, NULL); g->efecto++;
            }
            barrera = (long)j;
        } else if (barrera >= 0) {
            arista(g, (size_t)barrera, j, 1, RO_DEP_EFECTO, NULL); /* Nada sube por encima de una llamada */
            g->efecto++;
        }
    }
    /* Control: el salto/return final queda despues de todo. */
    if (hay_terminador)
        for (size_t i = 0; i + 1 < m; ++i) { arista(g, i, m - 1, 1, RO_DEP_CONTROL, NULL); g->control++; }
    free(u);
}

/* ===================================================================== */
/* 4. LIST SCHEDULING                                                     */
/* ===================================================================== */

/* Calcula un orden nuevo en `orden` (indices 0..m-1). Ciclo a ciclo elige,
 * entre las instrucciones cuyas dependencias ya se emitieron y cuyos
 * valores ya estan listos, la de camino critico mas largo (desempate:
 * la que estaba antes). Si ninguna esta lista, avanza al primer ciclo en
 * que alguna lo este. Devuelve 0 si falta memoria. */
/* Camino critico desde cada nodo: altura[i] = ciclos minimos desde que se
 * emite i hasta que se emite la ultima instruccion que depende de el (+1).
 * Como las aristas van siempre de i a j > i, basta recorrer de atras hacia
 * adelante. */
static void alturas(const Grafo *g, size_t *altura) {
    size_t m = g->m;
    for (size_t i = m; i-- > 0;) {
        altura[i] = 1;                                /* El nodo solo: 1 ciclo */
        for (size_t j = i + 1; j < m; ++j) {
            unsigned l = g->lat[i * m + j];
            if (l && l + altura[j] > altura[i]) altura[i] = l + altura[j];
        }
    }
}

static int calendarizar(const Grafo *g, size_t *orden) {
    size_t m = g->m;
    size_t *altura = calloc(m, sizeof *altura);       /* Camino critico desde cada nodo */
    size_t *temprano = calloc(m, sizeof *temprano);   /* Primer ciclo en que puede emitirse */
    size_t *faltan = calloc(m, sizeof *faltan);       /* Predecesores sin emitir */
    unsigned char *hecho = calloc(m, 1);              /* Ya emitido */
    if (!altura || !temprano || !faltan || !hecho) {
        free(altura); free(temprano); free(faltan); free(hecho); return 0;
    }
    alturas(g, altura);                               /* Prioridad: camino critico */
    for (size_t i = 0; i < m; ++i)                   /* Cuenta predecesores */
        for (size_t j = i + 1; j < m; ++j) if (g->lat[i * m + j]) faltan[j]++;
    size_t ciclo = 0;
    for (size_t paso = 0; paso < m; ++paso) {
        size_t mejor = m;                             /* m = ninguno todavia */
        int listo_mejor = 0;
        for (size_t j = 0; j < m; ++j) {
            if (hecho[j] || faltan[j]) continue;      /* Ya emitida o le faltan dependencias */
            int listo = temprano[j] <= ciclo;         /* Sus valores ya estan disponibles? */
            if (mejor == m) { mejor = j; listo_mejor = listo; continue; }
            if (listo != listo_mejor) {               /* Preferir las que estan listas */
                if (listo) { mejor = j; listo_mejor = 1; }
                continue;
            }
            if (!listo && temprano[j] != temprano[mejor]) { /* Ninguna lista: la que este antes */
                if (temprano[j] < temprano[mejor]) mejor = j;
                continue;
            }
            if (altura[j] > altura[mejor]) mejor = j; /* Camino critico mas largo primero */
        }
        if (temprano[mejor] > ciclo) ciclo = temprano[mejor]; /* Espera (NOP) si hace falta */
        orden[paso] = mejor;
        hecho[mejor] = 1;
        for (size_t j = mejor + 1; j < m; ++j) {      /* Avisa a sus sucesores */
            unsigned l = g->lat[mejor * m + j];
            if (!l) continue;
            faltan[j]--;
            if (ciclo + l > temprano[j]) temprano[j] = ciclo + l;
        }
        ciclo++;                                      /* Una instruccion por ciclo */
    }
    free(altura); free(temprano); free(faltan); free(hecho);
    return 1;
}

/* Comprueba que el orden respete TODAS las aristas (red de seguridad). */
static int respeta(const Grafo *g, const size_t *orden) {
    size_t m = g->m;
    size_t *pos = malloc(m * sizeof *pos);            /* pos[nodo] = lugar en el orden nuevo */
    if (!pos) return 0;
    for (size_t k = 0; k < m; ++k) pos[orden[k]] = k;
    int ok = 1;
    for (size_t i = 0; i < m && ok; ++i)
        for (size_t j = i + 1; j < m && ok; ++j)
            if (g->lat[i * m + j] && pos[i] > pos[j]) ok = 0; /* i debia ir antes que j */
    free(pos);
    return ok;
}

/* ===================================================================== */
/* 5. ANALISIS DE UN BLOQUE                                               */
/* ===================================================================== */
/* Lo comparten ro_reordenar (que aplica el orden) y ro_analizar_bloque
 * (que lo exporta para el visualizador), asi ambos ven el MISMO grafo. */

typedef struct {
    size_t p, m;              /* Region del grafo: ir->codigo[p, p + m) */
    int hay_term;             /* 1 si la ultima instruccion es un salto/return */
    int valido;               /* 1 si hay un orden nuevo que respeta el grafo */
    Grafo g;                  /* Dependencias */
    size_t *orden;            /* Orden nuevo (identidad si no es valido) */
    size_t ciclos_nuevo;      /* Ciclos del bloque completo con el orden nuevo */
} Analisis;

static void analisis_liberar(Analisis *a) {
    free(a->g.lat); free(a->g.tipo); free((void *)a->g.por); free(a->orden);
}

/* Arma el grafo del bloque [ini, fin) y calcula el orden nuevo. `nombres`
 * pide guardar la variable de cada arista (solo para el JSON).
 * Devuelve 0 en exito y 2 si falta memoria. */
static int analizar(const IrPrograma *ir, size_t ini, size_t fin, unsigned lat,
                    int nombres, Analisis *a) {
    *a = (Analisis){0};
    size_t p = ini;
    while (p < fin && fija_inicio(ir->codigo[p].op)) ++p; /* Salta etiquetas/func/param */
    size_t m = fin - p;                                   /* Nodos del grafo */
    a->p = p; a->m = m;
    a->hay_term = m && terminador(ir->codigo[fin - 1].op);
    a->g.m = m;
    if (!m) return 0;                                     /* Bloque de solo etiquetas */
    const IrInstr **v = malloc(m * sizeof *v);            /* Punteros a la region */
    const IrInstr **nuevo = malloc((fin - ini) * sizeof *nuevo); /* Bloque con el orden nuevo */
    a->orden = malloc(m * sizeof *a->orden);
    a->g.lat = calloc(m * m, 1);                          /* Matriz de dependencias */
    a->g.tipo = calloc(m * m, 1);
    if (nombres) a->g.por = calloc(m * m, sizeof *a->g.por);
    if (!v || !nuevo || !a->orden || !a->g.lat || !a->g.tipo || (nombres && !a->g.por)) {
        free(v); free(nuevo); analisis_liberar(a); *a = (Analisis){0}; return 2;
    }
    for (size_t k = 0; k < m; ++k) { v[k] = &ir->codigo[p + k]; a->orden[k] = k; }
    construir(&a->g, v, m, lat, a->hay_term);             /* Paso 2: el DAG */
    if (m >= 2 + (size_t)a->hay_term) {                   /* Hay algo que reordenar */
        if (calendarizar(&a->g, a->orden) && respeta(&a->g, a->orden)) { /* Paso 3 */
            for (size_t k = ini; k < p; ++k) nuevo[k - ini] = &ir->codigo[k];   /* Parte fija */
            for (size_t k = 0; k < m; ++k) nuevo[p - ini + k] = v[a->orden[k]]; /* Region nueva */
            a->ciclos_nuevo = ciclos_orden(nuevo, fin - ini, lat, NULL);       /* Paso 4 */
            a->valido = 1;
        } else {
            for (size_t k = 0; k < m; ++k) a->orden[k] = k; /* Sin orden nuevo: el original */
        }
    }
    free(v); free(nuevo);
    return 0;
}

static unsigned limitar(unsigned lat) { return lat < 1 ? 1 : lat > 255 ? 255 : lat; } /* Cabe en un byte */

/* ===================================================================== */
/* 6. INTERFAZ PUBLICA                                                    */
/* ===================================================================== */

void ro_opciones_defecto(RoOpciones *op) { op->latencia = 3; }

void ro_resumen_liberar(RoResumen *res) {
    if (!res) return;
    free(res->detalle);
    free(res->origen);
    res->detalle = NULL; res->cantidad_detalle = 0;
    res->origen = NULL; res->cantidad_origen = 0;
}

/* Suma los contadores de dependencias de g al bloque y al total. */
static void sumar(RoBloque *b, RoResumen *r, const Grafo *g) {
    b->raw = g->raw; b->war = g->war; b->waw = g->waw;
    b->memoria = g->memoria; b->efecto = g->efecto; b->control = g->control;
    r->raw += g->raw; r->war += g->war; r->waw += g->waw;
    r->memoria += g->memoria; r->efecto += g->efecto; r->control += g->control;
}

int ro_reordenar(IrPrograma *ir, const RoOpciones *opc, RoResumen *res) {
    if (!ir || (ir->cantidad && !ir->bloques)) return 2;  /* Hace falta la IR con bloques */
    RoOpciones o;
    if (opc) o = *opc; else ro_opciones_defecto(&o);
    o.latencia = limitar(o.latencia);
    RoResumen r = {0};
    r.instrucciones = ir->cantidad;
    r.bloques = ir->cantidad_bloques;
    r.origen = malloc((ir->cantidad + 1) * sizeof *r.origen); /* Permutacion global */
    r.detalle = calloc(ir->cantidad_bloques + 1, sizeof *r.detalle);
    if (!r.origen || !r.detalle) { ro_resumen_liberar(&r); return 2; }
    r.cantidad_origen = ir->cantidad;
    r.cantidad_detalle = ir->cantidad_bloques;
    for (size_t i = 0; i < ir->cantidad; ++i) r.origen[i] = i; /* Al inicio nadie se movio */

    int estado = 0;
    for (size_t b = 0; b < ir->cantidad_bloques && !estado; ++b) {
        size_t ini = ir->bloques[b].inicio, fin = ir->bloques[b].fin;
        RoBloque *d = &r.detalle[b];
        d->inicio = ini; d->fin = fin;
        d->ciclos_antes = d->ciclos_despues = ro_ciclos(ir, ini, fin, o.latencia);
        Analisis a;
        if (analizar(ir, ini, fin, o.latencia, 0, &a)) { estado = 2; break; } /* Pasos 1 a 4 */
        if (a.m >= 2 + (size_t)a.hay_term) sumar(d, &r, &a.g); /* Solo bloques con algo que mover */
        if (a.valido && a.ciclos_nuevo < d->ciclos_antes) {     /* Solo si mejora */
            IrInstr *copia = malloc(a.m * sizeof *copia);
            size_t *orig = malloc(a.m * sizeof *orig);
            if (copia && orig) {
                for (size_t k = 0; k < a.m; ++k) {    /* Toma las instrucciones en el orden nuevo */
                    copia[k] = ir->codigo[a.p + a.orden[k]];
                    orig[k] = r.origen[a.p + a.orden[k]];
                    if (a.orden[k] != k) d->movidas++; /* Cambio de lugar */
                }
                memcpy(&ir->codigo[a.p], copia, a.m * sizeof *copia); /* Las escribe de vuelta */
                memcpy(&r.origen[a.p], orig, a.m * sizeof *orig);
                d->ciclos_despues = a.ciclos_nuevo;
                r.movidas += d->movidas;
                r.bloques_cambiados++;
            } else estado = 2;
            free(copia); free(orig);
        }
        r.ciclos_antes += d->ciclos_antes; r.ciclos_despues += d->ciclos_despues;
        analisis_liberar(&a);
    }
    if (res) *res = r; else ro_resumen_liberar(&r);
    return estado;
}

/* 1 si s es un nombre de variable (no una constante como "5" o "-2"). */
static int es_variable(const char *s) {
    unsigned char c = (unsigned char)s[0];
    return c == '_' || c == '$' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
}

void ro_grafo_liberar(RoGrafoBloque *g) {
    if (!g) return;
    free(g->nodos); free(g->aristas); free(g->orden); free(g->ruta);
    *g = (RoGrafoBloque){0};
}

int ro_analizar_bloque(const IrPrograma *ir, size_t b, unsigned latencia, RoGrafoBloque *out) {
    if (!out) return 2;
    *out = (RoGrafoBloque){0};
    if (!ir || !ir->bloques || b >= ir->cantidad_bloques) return 2;
    unsigned lat = limitar(latencia);
    size_t ini = ir->bloques[b].inicio, fin = ir->bloques[b].fin, n = fin - ini;
    out->inicio = ini; out->fin = fin;
    out->ciclos_antes = out->ciclos_despues = ro_ciclos(ir, ini, fin, lat);
    Analisis a;
    if (analizar(ir, ini, fin, lat, 1, &a)) return 2;
    size_t m = a.m;
    out->primero = a.p;
    out->cantidad_nodos = m;
    /* Mismo criterio que ro_reordenar: solo se aplica si mejora. */
    out->aplicado = a.valido && a.ciclos_nuevo < out->ciclos_antes;
    if (out->aplicado) out->ciclos_despues = a.ciclos_nuevo;
    if (!m) { analisis_liberar(&a); return 0; }
    if (!out->aplicado) for (size_t k = 0; k < m; ++k) a.orden[k] = k;

    size_t aristas = 0;                                   /* Cuenta las aristas */
    for (size_t i = 0; i < m * m; ++i) if (a.g.lat[i]) ++aristas;
    size_t *altura = malloc(m * sizeof *altura);
    size_t *em_antes = malloc(n * sizeof *em_antes);      /* Ciclo de cada instruccion (antes) */
    size_t *em_despues = malloc(n * sizeof *em_despues);  /* ... y despues */
    const IrInstr **v = malloc(n * sizeof *v);
    out->nodos = calloc(m, sizeof *out->nodos);
    out->aristas = calloc(aristas + 1, sizeof *out->aristas);
    out->orden = malloc(m * sizeof *out->orden);
    out->ruta = malloc(m * sizeof *out->ruta);
    int estado = 0;
    if (!altura || !em_antes || !em_despues || !v || !out->nodos || !out->aristas ||
        !out->orden || !out->ruta) {
        estado = 2;
        goto fin;
    }
    /* Ciclo en que sale cada instruccion: en el orden original y en el final. */
    for (size_t k = 0; k < n; ++k) v[k] = &ir->codigo[ini + k];
    ciclos_orden(v, n, lat, em_antes);
    for (size_t k = 0; k < m; ++k) v[a.p - ini + k] = &ir->codigo[a.p + a.orden[k]];
    ciclos_orden(v, n, lat, em_despues);

    alturas(&a.g, altura);
    for (size_t k = 0; k < m; ++k) {                      /* Datos de cada nodo */
        RoNodo *x = &out->nodos[k];
        x->pos = a.p + k;
        x->altura = altura[k];
        x->ciclo_antes = em_antes[a.p - ini + k];
        Usos u; usos(&ir->codigo[a.p + k], &u);
        for (size_t r = 0; r < u.nlee; ++r) if (es_variable(u.lee[r])) x->lee[x->nlee++] = u.lee[r];
        for (size_t w = 0; w < u.nesc; ++w) x->escribe[x->nesc++] = u.escribe[w];
    }
    for (size_t k = 0; k < m; ++k) {                      /* Donde quedo cada nodo */
        out->orden[k] = a.orden[k];
        out->nodos[a.orden[k]].nueva_pos = k;
        out->nodos[a.orden[k]].ciclo_despues = em_despues[a.p - ini + k];
    }

    /* Ruta critica: desde el nodo de mayor altura, seguir siempre al sucesor
     * que "explica" esa altura (latencia + altura del sucesor). */
    size_t i = 0;
    for (size_t k = 1; k < m; ++k) if (altura[k] > altura[i]) i = k;
    out->longitud_ruta = altura[i];
    for (;;) {
        out->ruta[out->largo_ruta++] = i;
        out->nodos[i].critico = 1;
        size_t sig = m;
        for (size_t j = i + 1; j < m && sig == m; ++j) {
            unsigned l = a.g.lat[i * m + j];
            if (l && l + altura[j] == altura[i]) sig = j;
        }
        if (sig == m) break;                              /* Llego al final */
        i = sig;
    }
    /* Si la ruta termina en una marca que no gasta ciclo (endfunc), no cuenta. */
    if (!ocupa_ciclo(ir->codigo[a.p + i].op) && out->longitud_ruta) out->longitud_ruta--;

    for (size_t x = 0; x < m; ++x)                        /* Lista de aristas */
        for (size_t y = x + 1; y < m; ++y) {
            size_t k = x * m + y;
            if (!a.g.lat[k]) continue;
            RoArista *e = &out->aristas[out->cantidad_aristas++];
            e->de = x; e->a = y;
            e->latencia = a.g.lat[k];
            e->tipos = a.g.tipo[k];
            e->por = a.g.por[k];
            for (size_t r = 0; r + 1 < out->largo_ruta; ++r)
                if (out->ruta[r] == x && out->ruta[r + 1] == y) e->critica = 1;
        }
fin:
    free(altura); free(em_antes); free(em_despues); free(v);
    analisis_liberar(&a);
    if (estado) ro_grafo_liberar(out);
    return estado;
}
