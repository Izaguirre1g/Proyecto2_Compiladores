/* List scheduling determinista con latencia N+3 y barreras conservadoras.
 * El grafo llega de otra fase: no se deducen ni se reparan sus dependencias. */
#include "vliw_scheduler.h"
#include <stdlib.h>
#include <string.h>

#define EF_MEM (VLIW_EF_LEE_MEMORIA | VLIW_EF_ESCRIBE_MEMORIA)
#define EF_BARRERA (VLIW_EF_BOVEDA | VLIW_EF_ESTADO | VLIW_EF_EXCEPCION | \
                    VLIW_EF_DESCONOCIDOS | VLIW_EF_BARRERA)
#define EF_TODOS (EF_MEM | EF_BARRERA)

typedef struct {
    size_t *offset, *aristas, *grados, *minimo;
} Grafo;

static VliwEstado diagnosticar(VliwDiagnostico *d, VliwEstado e,
                               size_t i, size_t a, const char *mensaje)
{
    if (d) *d = (VliwDiagnostico){e, i, a, mensaje};
    return e;
}
#define ERROR(e,i,a,m) diagnosticar(d,e,i,a,m)
static uint32_t lee(const VliwInstruccion *i) { return (uint32_t)i->lee & ~UINT32_C(1); }
static uint32_t escribe(const VliwInstruccion *i) { return (uint32_t)i->escribe & ~UINT32_C(1); }
static unsigned bits(uint64_t m)
{
    unsigned n = 0;
    while (m) { m &= m - 1; ++n; }
    return n;
}
static bool barrera(const VliwInstruccion *i)
{
    /* Toda CRIPTO se aísla aunque el adaptador tenga información más precisa.
     * La distancia tres protege también AUTH/INIT y las excepciones de S6. */
    return i->slot == VLIW_SLOT_CRIPTO || (i->efectos & EF_BARRERA) ||
        i->control == VLIW_CONTROL_LLAMADA || i->control == VLIW_CONTROL_RETORNO;
}
static bool conflicto(const VliwInstruccion *a, const VliwInstruccion *b)
{
    return (escribe(a) & (lee(b) | escribe(b))) || (lee(a) & escribe(b));
}
static bool proteger_cero(const VliwEntrada *e)
{
    for (size_t b = 0; b < e->cantidad_bloques; ++b)
        for (size_t s = 0; s < e->bloques[b].cantidad_sucesores; ++s)
            if (e->bloques[b].sucesores[s] == 0) return true;
    return false;
}
static void liberar_grafo(Grafo *g)
{
    free(g->offset); free(g->aristas); free(g->grados); free(g->minimo);
    *g = (Grafo){0};
}

/* Comprueba dimensiones, bloques y metadatos antes de reservar el grafo.
 * Un rechazo devuelve diagnóstico sin reparar información del productor externo. */
static VliwEstado validar_entrada(const VliwEntrada *e, VliwDiagnostico *d)
{
    if (!e) return ERROR(VLIW_ARGUMENTO,VLIW_VACIO,VLIW_VACIO,"entrada nula");
    size_t n = e->cantidad_instrucciones;
    /* Es imposible alojar más de cuatro operaciones por bundle. El límite
     * previo evita recorrer cantidades absurdas antes de reservar memoria. */
    if (n > VLIW_MAX_BUNDLES * (size_t)VLIW_SLOTS)
        return ERROR(VLIW_CAPACIDAD,VLIW_VACIO,VLIW_VACIO,"demasiadas instrucciones para IMEM");
    if (!n) {
        if (e->cantidad_bloques || e->cantidad_dependencias)
            return ERROR(VLIW_BLOQUES,VLIW_VACIO,VLIW_VACIO,"programa vacío con bloques o aristas");
        return VLIW_OK;
    }
    if (!e->instrucciones || !e->bloques ||
        (e->cantidad_dependencias && !e->dependencias))
        return ERROR(VLIW_ARGUMENTO,VLIW_VACIO,VLIW_VACIO,"arreglo requerido ausente");
    if (!e->metadatos_completos)
        return ERROR(VLIW_METADATOS,VLIW_VACIO,VLIW_VACIO,"se requieren metadatos completos del productor externo");
    if (!e->cantidad_bloques || e->cantidad_bloques > n)
        return ERROR(VLIW_BLOQUES,VLIW_VACIO,VLIW_VACIO,"cantidad de bloques inválida");
    if (e->cantidad_dependencias > SIZE_MAX / sizeof(size_t) ||
        e->cantidad_dependencias > SIZE_MAX / sizeof(VliwDependencia))
        return ERROR(VLIW_ARGUMENTO,VLIW_VACIO,VLIW_VACIO,"tamaño de grafo desbordado");
    size_t siguiente = 0;
    for (size_t b = 0; b < e->cantidad_bloques; ++b) {
        const VliwBloque *bb = &e->bloques[b];
        if (bb->inicio != siguiente || bb->fin <= bb->inicio || bb->fin > n)
            return ERROR(VLIW_BLOQUES,bb->inicio,VLIW_VACIO,"bloques incompletos, vacíos o superpuestos");
        if (bb->cantidad_sucesores > e->cantidad_bloques ||
            (bb->cantidad_sucesores && !bb->sucesores))
            return ERROR(VLIW_BLOQUES,bb->inicio,VLIW_VACIO,"lista de sucesores inválida");
        for (size_t s = 0; s < bb->cantidad_sucesores; ++s)
            if (bb->sucesores[s] >= e->cantidad_bloques)
                return ERROR(VLIW_BLOQUES,bb->inicio,VLIW_VACIO,"sucesor fuera del CFG");
        for (size_t i = bb->inicio; i < bb->fin; ++i)
            if (e->instrucciones[i].bloque != b)
                return ERROR(VLIW_BLOQUES,i,VLIW_VACIO,"instrucción asignada a otro bloque");
        /* Sin transferencia solo existe caída secuencial. Los destinos de BRU
         * y llamadas los entrega el CFG, nunca se extraen del texto ensamblador. */
        if (e->instrucciones[bb->fin-1].control == VLIW_CONTROL_NINGUNO) {
            size_t esperado = b + 1 < e->cantidad_bloques ? 1 : 0;
            if (bb->cantidad_sucesores != esperado ||
                (esperado && bb->sucesores[0] != b + 1))
                return ERROR(VLIW_BLOQUES,bb->fin-1,VLIW_VACIO,"caída secuencial inconsistente con el CFG");
        }
        siguiente = bb->fin;
    }
    if (siguiente != n)
        return ERROR(VLIW_BLOQUES,siguiente,VLIW_VACIO,"instrucciones sin bloque");
    for (size_t i = 0; i < n; ++i) {
        const VliwInstruccion *in = &e->instrucciones[i];
        if ((unsigned)in->slot >= VLIW_SLOTS || (unsigned)in->control > VLIW_CONTROL_RETORNO ||
            (in->efectos & ~EF_TODOS) || in->lee > UINT32_MAX || in->escribe > UINT32_MAX ||
            bits(lee(in)) > 2 || bits(escribe(in)) > (in->slot == VLIW_SLOT_CRIPTO ? 2u : 1u))
            return ERROR(VLIW_METADATOS,i,VLIW_VACIO,"slot, efectos o máscaras inconsistentes con la ISA");
        if (((in->slot == VLIW_SLOT_BRU) != (in->control != VLIW_CONTROL_NINGUNO)) ||
            (in->control != VLIW_CONTROL_NINGUNO && i + 1 != e->bloques[in->bloque].fin))
            return ERROR(VLIW_BLOQUES,i,VLIW_VACIO,"BRU debe terminar su bloque");
        if ((in->slot == VLIW_SLOT_LSU && !(in->efectos & EF_MEM)) ||
            ((in->efectos & EF_MEM) && in->slot != VLIW_SLOT_LSU && in->slot != VLIW_SLOT_CRIPTO) ||
            ((in->efectos & (VLIW_EF_BOVEDA | VLIW_EF_ESTADO)) && in->slot != VLIW_SLOT_CRIPTO) ||
            (in->slot == VLIW_SLOT_CRIPTO && !(in->efectos & EF_TODOS)) ||
            (in->control == VLIW_CONTROL_LLAMADA && !(in->efectos & VLIW_EF_DESCONOCIDOS)))
            return ERROR(VLIW_METADATOS,i,VLIW_VACIO,"faltan efectos de memoria, criptografía o llamada");
        for (size_t j = 0; j < i; ++j)
            if (e->instrucciones[j].id == in->id)
                return ERROR(VLIW_METADATOS,i,VLIW_VACIO,"identificador original duplicado");
    }
    for (size_t a = 0; a < e->cantidad_dependencias; ++a) {
        const VliwDependencia *ar = &e->dependencias[a];
        if (ar->antes >= n || ar->despues >= n || (unsigned)ar->tipo > VLIW_DEP_BARRERA ||
            !ar->distancia || (ar->tipo == VLIW_DEP_RAW && ar->distancia < VLIW_RAW_DISTANCIA))
            return ERROR(VLIW_DEPENDENCIA,ar->despues,a,"referencia, tipo o distancia de dependencia inválidos");
        if (ar->distancia >= VLIW_MAX_BUNDLES)
            return ERROR(VLIW_CAPACIDAD,ar->despues,a,"separación imposible dentro de IMEM ordinaria");
    }
    return VLIW_OK;
}

/* Construye índices de aristas y grados pendientes desde las dependencias
 * declaradas. Las reservas y rechazos se liberan aquí; no se inventan aristas. */
static VliwEstado construir_grafo(const VliwEntrada *e, Grafo *g, VliwDiagnostico *d)
{
    size_t n = e->cantidad_instrucciones, m = e->cantidad_dependencias;
    *g = (Grafo){0};
    if (!n) return VLIW_OK;
    g->offset = calloc(n+1,sizeof(size_t));
    g->grados = calloc(n,sizeof(size_t)); g->minimo = calloc(n,sizeof(size_t));
    g->aristas = m ? malloc(m*sizeof(size_t)) : NULL;
    size_t *cola = malloc(n*sizeof(size_t)), *cursor = malloc(n*sizeof(size_t));
    if (!g->offset || !g->grados || !g->minimo || (m && !g->aristas) || !cola || !cursor) {
        free(cola); free(cursor); liberar_grafo(g);
        return ERROR(VLIW_MEMORIA,VLIW_VACIO,VLIW_VACIO,"sin memoria para el grafo");
    }
    for (size_t a = 0; a < m; ++a) {
        ++g->offset[e->dependencias[a].antes+1];
        ++g->grados[e->dependencias[a].despues];
    }
    for (size_t i = 1; i <= n; ++i) g->offset[i] += g->offset[i-1];
    memcpy(cursor,g->offset,n*sizeof(size_t));
    for (size_t a = 0; a < m; ++a)
        g->aristas[cursor[e->dependencias[a].antes]++] = a;
    /* Kahn visita el grafo entero antes de emitir nada, incluso componentes
     * desconectados. Un backedge del CFG no es una arista del DAG de emisión. */
    size_t inicio = 0, fin = 0;
    for (size_t i = 0; i < n; ++i) if (!g->grados[i]) cola[fin++] = i;
    while (inicio < fin) {
        size_t i = cola[inicio++];
        for (size_t a = g->offset[i]; a < g->offset[i+1]; ++a) {
            size_t j = e->dependencias[g->aristas[a]].despues;
            if (!--g->grados[j]) cola[fin++] = j;
        }
    }
    free(cola); free(cursor);
    if (fin != n) {
        liberar_grafo(g);
        return ERROR(VLIW_CICLO,VLIW_VACIO,VLIW_VACIO,"el grafo de dependencias contiene un ciclo");
    }
    for (size_t a = 0; a < m; ++a) {
        const VliwDependencia *ar = &e->dependencias[a];
        const VliwInstruccion *i = &e->instrucciones[ar->antes], *j = &e->instrucciones[ar->despues];
        if (ar->antes >= ar->despues ||
            (ar->tipo == VLIW_DEP_RAW && !(escribe(i) & lee(j))) ||
            (ar->tipo == VLIW_DEP_WAR && !(lee(i) & escribe(j))) ||
            (ar->tipo == VLIW_DEP_WAW && !(escribe(i) & escribe(j)))) {
            liberar_grafo(g);
            return ERROR(VLIW_DEPENDENCIA,ar->despues,a,"dependencia contraria al orden original o a sus máscaras");
        }
        ++g->grados[ar->despues];
    }
    return VLIW_OK;
}

/* Libera el plan y sus arreglos propios, y deja *plan en NULL. Recibe
 * el doble puntero publicado por el scheduler; admite NULL y no libera textos prestados. */
void vliw_liberar(VliwPlan **plan)
{
    if (!plan || !*plan) return;
    free((*plan)->bundles); free((*plan)->bundle_por_instruccion);
    free((*plan)->inicio_por_bloque); free(*plan); *plan = NULL;
}

/* Recorre y valida la salida independientemente del algoritmo de selección.
 * No repara dependencias ni modifica el plan. Acepta también planes de prueba. */
VliwEstado vliw_verificar(const VliwEntrada *e, const VliwPlan *p, VliwDiagnostico *d)
{
    VliwEstado estado = validar_entrada(e,d);
    if (estado != VLIW_OK) return estado;
    Grafo g;
    estado = construir_grafo(e,&g,d); if (estado != VLIW_OK) return estado;
    liberar_grafo(&g);
    if (!p || p->cantidad_bundles > VLIW_MAX_BUNDLES ||
        p->cantidad_instrucciones != e->cantidad_instrucciones || p->cantidad_bloques != e->cantidad_bloques ||
        (p->cantidad_bundles && !p->bundles) ||
        (e->cantidad_instrucciones && !p->bundle_por_instruccion) ||
        (e->cantidad_bloques && !p->inicio_por_bloque))
        return ERROR(VLIW_PLAN_INVALIDO,VLIW_VACIO,VLIW_VACIO,"dimensiones o arreglos del plan inválidos");
    size_t n = e->cantidad_instrucciones;
    if (!n) {
        if (p->cantidad_bundles || p->cantidad_vacios)
            return ERROR(VLIW_PLAN_INVALIDO,VLIW_VACIO,VLIW_VACIO,"plan no vacío para entrada vacía");
        return ERROR(VLIW_OK,VLIW_VACIO,VLIW_VACIO,"plan vacío válido");
    }
    if (!p->cantidad_bundles || p->inicio_por_bloque[0] != 0)
        return ERROR(VLIW_PLAN_INVALIDO,VLIW_VACIO,VLIW_VACIO,"inicio del primer bloque inválido");
    bool protege_inicial = proteger_cero(e);
    for (size_t b = 0; b < e->cantidad_bloques; ++b) {
        size_t inicio = p->inicio_por_bloque[b];
        size_t fin = b+1 < e->cantidad_bloques ? p->inicio_por_bloque[b+1] : p->cantidad_bundles;
        if (inicio >= fin || fin > p->cantidad_bundles)
            return ERROR(VLIW_PLAN_INVALIDO,VLIW_VACIO,VLIW_VACIO,"mapeo de bloques no monótono");
        if (b || protege_inicial) {
            if (fin-inicio < 3 || p->bundles[inicio].operaciones || p->bundles[inicio+1].operaciones)
                return ERROR(VLIW_PLAN_INVALIDO,VLIW_VACIO,VLIW_VACIO,"faltan dos bundles de protección a la entrada");
        }
        for (size_t q = inicio; q < fin; ++q)
            if (p->bundles[q].bloque != b)
                return ERROR(VLIW_PLAN_INVALIDO,VLIW_VACIO,VLIW_VACIO,"bundle movido entre bloques");
    }
    bool *visto = calloc(n,sizeof(bool));
    if (!visto) return ERROR(VLIW_MEMORIA,VLIW_VACIO,VLIW_VACIO,"sin memoria para verificar índices");
    size_t vacios = 0, total = 0;
    for (size_t q = 0; q < p->cantidad_bundles; ++q) {
        const VliwBundle *bu = &p->bundles[q]; unsigned ops = 0;
        for (unsigned s = 0; s < VLIW_SLOTS; ++s) {
            size_t i = bu->instrucciones[s];
            if (i == VLIW_VACIO) continue;
            if (i >= n || visto[i] || e->instrucciones[i].slot != (VliwSlot)s ||
                e->instrucciones[i].bloque != bu->bloque || p->bundle_por_instruccion[i] != q) {
                free(visto);
                return ERROR(VLIW_PLAN_INVALIDO,i,VLIW_VACIO,"índice duplicado, slot o mapeo inconsistente");
            }
            visto[i] = true; ++ops; ++total;
            const VliwInstruccion *in = &e->instrucciones[i];
            size_t fin = bu->bloque+1 < p->cantidad_bloques ? p->inicio_por_bloque[bu->bloque+1] : p->cantidad_bundles;
            if ((barrera(in) && bu->operaciones != 1) ||
                (in->control != VLIW_CONTROL_NINGUNO && (q+1 != fin || bu->operaciones != 1))) {
                free(visto);
                return ERROR(VLIW_PLAN_INVALIDO,i,VLIW_VACIO,"barrera compartida o transferencia fuera del final del bloque");
            }
        }
        if (ops != bu->operaciones) {
            free(visto);
            return ERROR(VLIW_PLAN_INVALIDO,VLIW_VACIO,VLIW_VACIO,"conteo de operaciones incorrecto");
        }
        vacios += ops == 0;
    }
    free(visto);
    if (total != n || vacios != p->cantidad_vacios)
        return ERROR(VLIW_PLAN_INVALIDO,VLIW_VACIO,VLIW_VACIO,"instrucción omitida o conteo de vacíos incorrecto");
    for (size_t a = 0; a < e->cantidad_dependencias; ++a) {
        const VliwDependencia *ar = &e->dependencias[a];
        size_t antes = p->bundle_por_instruccion[ar->antes], despues = p->bundle_por_instruccion[ar->despues];
        if (despues < antes || despues-antes < ar->distancia)
            return ERROR(VLIW_PLAN_INVALIDO,ar->despues,a,"distancia de dependencia incumplida");
    }
    /* Barrera defensiva de salida: compara máscaras y efectos originales,
     * no construye dependencias ni corrige el DAG externo. Una arista omitida
     * que permita adelantar un uso/escritura provoca rechazo, no un plan parcial. */
    for (size_t i = 0; i < n; ++i) for (size_t j = i+1; j < n; ++j) {
        const VliwInstruccion *a = &e->instrucciones[i], *b = &e->instrucciones[j];
        size_t ca = p->bundle_por_instruccion[i], cb = p->bundle_por_instruccion[j];
        bool raw = (escribe(a) & lee(b)) != 0;
        bool orden = (lee(a) & escribe(b)) || (escribe(a) & escribe(b)) ||
            ((a->efectos & EF_MEM) && (b->efectos & EF_MEM));
        bool fuerte = barrera(a) || barrera(b);
        if ((ca == cb && conflicto(a,b)) ||
            ((raw || orden || fuerte) && (cb <= ca || cb-ca < ((raw || fuerte) ? 3u : 1u))))
            return ERROR(VLIW_PLAN_INVALIDO,j,VLIW_VACIO,"riesgo de registros o cruce de efectos no protegido por metadatos");
    }
    return ERROR(VLIW_OK,VLIW_VACIO,VLIW_VACIO,"plan verificado");
}

/* Agrega un bundle inicialmente vacío e identifica su bloque. Si IMEM
 * alcanzó el límite, devuelve false antes de escribir fuera del arreglo. */
static bool agregar_bundle(VliwPlan *p, size_t bloque)
{
    if (p->cantidad_bundles == VLIW_MAX_BUNDLES) return false;
    VliwBundle *b = &p->bundles[p->cantidad_bundles++];
    *b = (VliwBundle){.bloque = bloque};
    for (unsigned s = 0; s < VLIW_SLOTS; ++s) b->instrucciones[s] = VLIW_VACIO;
    return true;
}

/* Entradas/arreglos/textos prestados e inmutables durante la llamada; sus
 * cantidades deben describir almacenamiento accesible real, como en toda API C.
 * Salida debe apuntar a NULL. Solo se publica un plan completo y verificado;
 * en error conserva *salida. El plan posee sus arreglos, no referencias a texto.
 * Programa vacío (sin bloques ni aristas) produce un plan vacío liberable. */
VliwEstado vliw_planificar(const VliwEntrada *e, VliwPlan **salida, VliwDiagnostico *d)
{
    if (!salida || *salida)
        return ERROR(VLIW_ARGUMENTO,VLIW_VACIO,VLIW_VACIO,"salida requerida e inicialmente NULL");
    VliwEstado estado = validar_entrada(e,d);
    if (estado != VLIW_OK) return estado;
    Grafo g;
    estado = construir_grafo(e,&g,d); if (estado != VLIW_OK) return estado;
    VliwPlan *p = calloc(1,sizeof(*p));
    if (!p) { liberar_grafo(&g); return ERROR(VLIW_MEMORIA,VLIW_VACIO,VLIW_VACIO,"sin memoria para el plan"); }
    size_t n = e->cantidad_instrucciones;
    p->cantidad_instrucciones = n; p->cantidad_bloques = e->cantidad_bloques;
    if (n) {
        p->bundles = calloc(VLIW_MAX_BUNDLES,sizeof(VliwBundle));
        p->bundle_por_instruccion = malloc(n*sizeof(size_t));
        p->inicio_por_bloque = malloc(e->cantidad_bloques*sizeof(size_t));
        if (!p->bundles || !p->bundle_por_instruccion || !p->inicio_por_bloque) {
            estado = ERROR(VLIW_MEMORIA,VLIW_VACIO,VLIW_VACIO,"sin memoria para los mapeos"); goto terminar;
        }
        for (size_t i = 0; i < n; ++i) p->bundle_por_instruccion[i] = VLIW_VACIO;
    }
    size_t piso = 0;
    bool protege_inicial = proteger_cero(e);
    for (size_t b = 0; b < e->cantidad_bloques; ++b) {
        const VliwBloque *bb = &e->bloques[b];
        p->inicio_por_bloque[b] = p->cantidad_bundles;
        /* El destino lógico SIEMPRE precede sus dos NOP. También se protege
         * B0 si tiene predecesor en el CFG: un bucle no puede saltarse el margen.
         * Dos vacíos + primer consumidor garantizan distancia >=3 incluso sin
         * contar la penalización de flush del procesador. */
        if (b || protege_inicial) for (unsigned k = 0; k < 2; ++k) {
            if (!agregar_bundle(p,b)) goto capacidad;
            ++p->cantidad_vacios;
        }
        size_t pendientes = bb->fin-bb->inicio;
        while (pendientes) {
            size_t ciclo = p->cantidad_bundles;
            if (!agregar_bundle(p,b)) goto capacidad;
            VliwBundle *bu = &p->bundles[ciclo];
            /* La primera barrera/BRU pendiente limita la región elegible.
             * Ninguna instrucción posterior puede adelantarla. */
            size_t frontera = bb->fin;
            for (size_t i = bb->inicio; i < bb->fin; ++i)
                if (p->bundle_por_instruccion[i] == VLIW_VACIO &&
                    (barrera(&e->instrucciones[i]) || e->instrucciones[i].control != VLIW_CONTROL_NINGUNO)) {
                    frontera = i; break;
                }
            size_t siguiente_memoria = bb->fin;
            for (size_t i = bb->inicio; i < bb->fin; ++i)
                if (p->bundle_por_instruccion[i] == VLIW_VACIO && (e->instrucciones[i].efectos & EF_MEM)) {
                    siguiente_memoria = i; break;
                }
            for (size_t i = bb->inicio; i < bb->fin && i <= frontera; ++i) {
                const VliwInstruccion *in = &e->instrucciones[i];
                if (p->bundle_por_instruccion[i] != VLIW_VACIO || g.grados[i] ||
                    g.minimo[i] > ciclo || piso > ciclo || bu->instrucciones[in->slot] != VLIW_VACIO) continue;
                /* Sin aliasing externo, solo el primer acceso pendiente puede
                 * emitir. Se selecciona una vez por ciclo: no se buscan parejas
                 * de direcciones ni se construyen aristas de memoria. */
                if ((in->efectos & EF_MEM) && i != siguiente_memoria) continue;
                bool especial = barrera(in) || in->control != VLIW_CONTROL_NINGUNO;
                bool listo = !especial || !bu->operaciones;
                if (especial) for (size_t j = bb->inicio; listo && j < i; ++j) {
                    size_t previo = p->bundle_por_instruccion[j];
                    if (previo == VLIW_VACIO || previo + (barrera(in) ? 3u : 1u) > ciclo) listo = false;
                }
                for (unsigned s = 0; listo && s < VLIW_SLOTS; ++s)
                    if (bu->instrucciones[s] != VLIW_VACIO && conflicto(in,&e->instrucciones[bu->instrucciones[s]])) listo = false;
                if (!listo) continue;
                /* Orden original desempata las instrucciones listas. La
                 * restricción de slot limita a una operación por unidad. */
                bu->instrucciones[in->slot] = i; ++bu->operaciones;
                p->bundle_por_instruccion[i] = ciclo; --pendientes;
                for (size_t a = g.offset[i]; a < g.offset[i+1]; ++a) {
                    const VliwDependencia *ar = &e->dependencias[g.aristas[a]];
                    --g.grados[ar->despues];
                    size_t minimo = ciclo + ar->distancia;
                    if (minimo > g.minimo[ar->despues]) g.minimo[ar->despues] = minimo;
                }
                if (barrera(in)) piso = ciclo + 3;
                if (especial) break;
            }
            /* Si hay operaciones bloqueadas por latencia, este ciclo vacío
             * es parte de IMEM y de las estadísticas; nunca se omite del mapa. */
            p->cantidad_vacios += bu->operaciones == 0;
        }
    }
    estado = vliw_verificar(e,p,d);
    if (estado == VLIW_PLAN_INVALIDO)
        estado = ERROR(VLIW_METADATOS,d ? d->instruccion : VLIW_VACIO,
                       d ? d->dependencia : VLIW_VACIO,"metadatos insuficientes: el plan propuesto no es seguro");
    goto terminar;
capacidad:
    estado = ERROR(VLIW_CAPACIDAD,VLIW_VACIO,VLIW_VACIO,"el plan y sus separaciones exceden 1023 bundles");
terminar:
    liberar_grafo(&g);
    if (estado != VLIW_OK) { vliw_liberar(&p); return estado; }
    *salida = p;
    return ERROR(VLIW_OK,VLIW_VACIO,VLIW_VACIO,"calendarización completa y verificada");
}
