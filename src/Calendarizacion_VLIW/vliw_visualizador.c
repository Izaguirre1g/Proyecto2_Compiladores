/* Exporta el layout S7 en tabla, CSV y JSON. Las únicas categorías de NOP
 * inferidas son las guardas documentadas; los otros vacíos quedan neutrales. */
#include "vliw_visualizador.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const slots[] = {"ALU", "LSU", "BRU", "CRIPTO"};
static const char *const tipos[] = {"RAW", "WAR", "WAW", "MEMORIA", "CONTROL", "BARRERA"};
static const char *const destinos_tipo[] = {"DIRECTO", "CONDICIONAL", "INDIRECTO", "RETORNO", "NO_RESUELTO"};
typedef struct {
    char *datos;
    size_t longitud, capacidad;
    VliwVisualEstado estado;
} Buffer;
typedef struct {
    const VliwEntrada *e;
    const VliwPlan *p;
    const VliwDestinos *destinos;
    bool protege_inicial;
    VliwEstadisticas estadisticas;
} Vista;

static VliwVisualEstado informar(VliwVisualDiagnostico *d, VliwVisualEstado s,
    size_t i, const char *mensaje)
{
    if (d) { d->estado = s; d->instruccion = i; d->mensaje = mensaje; }
    return s;
}
static void iniciar_diagnostico(VliwVisualDiagnostico *d)
{
    if (d) *d = (VliwVisualDiagnostico){.instruccion = VLIW_VACIO,
        .dependencia = VLIW_VACIO, .mensaje = "sin error"};
}
static bool proteger_inicial(const VliwEntrada *e)
{
    for (size_t b = 0; b < e->cantidad_bloques; ++b)
        for (size_t j = 0; j < e->bloques[b].cantidad_sucesores; ++j)
            if (e->bloques[b].sucesores[j] == 0) return true;
    return false;
}
static bool proteccion(const Vista *v, size_t q)
{
    size_t b = v->p->bundles[q].bloque;
    return (b != 0 || v->protege_inicial) && q - v->p->inicio_por_bloque[b] < 2;
}
static bool control(const Vista *v, size_t q)
{
    const VliwBundle *b = &v->p->bundles[q];
    for (unsigned s = 0; s < VLIW_SLOTS; ++s)
        if (b->instrucciones[s] != VLIW_VACIO &&
            v->e->instrucciones[b->instrucciones[s]].control != VLIW_CONTROL_NINGUNO) return true;
    return false;
}
static const char *clase(const Vista *v, size_t q)
{
    if (proteccion(v, q)) return "PROTECCION";
    if (!v->p->bundles[q].operaciones) return "VACIO";
    return control(v, q) ? "CONTROL" : "OPERACIONES";
}
static bool ausente(const char *s) { return !s || !*s; }

/* S7 se invoca antes de recorrer índices o multiplicar cantidades. El límite
 * 1023 acota slots/bytes y hace seguras las restas de posiciones verificadas. */
/* Valida el plan mediante S7 y prepara la vista prestada para estadísticas
 * y formatos. Un plan rechazado no se presenta como una calendarización válida. */
static VliwVisualEstado preparar(const VliwEntrada *e, const VliwPlan *p,
    Vista *v, VliwVisualDiagnostico *d)
{
    if (!e || !p) return informar(d, VLIW_VIS_ARGUMENTO, VLIW_VACIO, "entrada y plan requeridos");
    VliwDiagnostico sd;
    VliwEstado s = vliw_verificar(e, p, &sd);
    if (d) { d->estado_plan = s; d->dependencia = sd.dependencia; }
    if (s != VLIW_OK)
        return informar(d, s == VLIW_MEMORIA ? VLIW_VIS_MEMORIA : VLIW_VIS_PLAN,
                        sd.instruccion, sd.mensaje);
    *v = (Vista){.e = e, .p = p, .protege_inicial = proteger_inicial(e)};
    VliwEstadisticas *a = &v->estadisticas;
    a->instrucciones_utiles = e->cantidad_instrucciones;
    a->bundles_totales = p->cantidad_bundles;
    a->bundles_vacios = p->cantidad_vacios;
    a->bundles_con_operaciones = p->cantidad_bundles - p->cantidad_vacios;
    a->slots_disponibles = VLIW_SLOTS * p->cantidad_bundles;
    a->slots_utilizados = e->cantidad_instrucciones;
    a->slots_vacios = a->slots_disponibles - a->slots_utilizados;
    a->bytes_imem = 16 * p->cantidad_bundles;
    a->bloques = e->cantidad_bloques;
    for (size_t i = 0; i < e->cantidad_instrucciones; ++i) {
        ++a->operaciones_por_slot[e->instrucciones[i].slot];
        a->textos_ausentes += ausente(e->instrucciones[i].texto);
    }
    for (size_t i = 0; i < e->cantidad_dependencias; ++i)
        ++a->dependencias_por_tipo[e->dependencias[i].tipo];
    for (size_t q = 0; q < p->cantidad_bundles; ++q) a->bundles_proteccion += proteccion(v, q);
    if (a->bundles_totales) {
        a->utilizacion_porcentual = 100.0 * (double)a->slots_utilizados / (double)a->slots_disponibles;
        a->operaciones_por_bundle = (double)a->instrucciones_utiles / (double)a->bundles_totales;
    }
    return VLIW_VIS_OK;
}
/* Arreglos y textos prestados, accesibles e inmutables durante la llamada.
 * Se valida el plan con S7. resultado solo cambia en éxito. No se interpretan
 * textos para calcular estadísticas; NULL o "" se cuentan como ausentes.
 * Un programa vacío tiene todos los valores y cocientes en cero. */
VliwVisualEstado vliw_estadisticas(const VliwEntrada *e, const VliwPlan *p,
    VliwEstadisticas *resultado, VliwVisualDiagnostico *d)
{
    iniciar_diagnostico(d);
    if (!resultado) return informar(d, VLIW_VIS_ARGUMENTO, VLIW_VACIO, "resultado requerido");
    Vista v;
    VliwVisualEstado s = preparar(e, p, &v, d);
    if (s == VLIW_VIS_OK) *resultado = v.estadisticas;
    return s;
}

/* No cambia el locale global. Los cocientes JSON se generan con enteros y
 * punto decimal fijo; las métricas públicas siguen siendo double completas. */
static bool reservar(Buffer *b, size_t n)
{
    if (b->estado != VLIW_VIS_OK) return false;
    if (n > SIZE_MAX - b->longitud - 1) { b->estado = VLIW_VIS_GENERACION; return false; }
    size_t requerida = b->longitud + n + 1;
    if (requerida > b->capacidad) {
        size_t capacidad = b->capacidad ? b->capacidad : 256;
        while (capacidad < requerida) capacidad = capacidad > SIZE_MAX / 2 ? requerida : capacidad * 2;
        char *p = realloc(b->datos, capacidad);
        if (!p) { b->estado = VLIW_VIS_MEMORIA; return false; }
        b->datos = p; b->capacidad = capacidad;
    }
    return true;
}
static void texto(Buffer *b, const char *s)
{
    size_t n = strlen(s);
    if (!reservar(b, n)) return;
    memcpy(b->datos + b->longitud, s, n + 1); b->longitud += n;
}
static void formato(Buffer *b, const char *f, ...)
{
    if (b->estado != VLIW_VIS_OK) return;
    va_list args, copia;
    va_start(args, f); va_copy(copia, args);
    int n = vsnprintf(NULL, 0, f, args); va_end(args);
    if (n < 0) b->estado = VLIW_VIS_GENERACION;
    else if (reservar(b, (size_t)n)) {
        int escritos = vsnprintf(b->datos + b->longitud, (size_t)n + 1, f, copia);
        if (escritos != n) b->estado = VLIW_VIS_GENERACION;
        else b->longitud += (size_t)n;
    }
    va_end(copia);
}
static void cociente(Buffer *b, size_t numerador, size_t denominador, unsigned factor)
{
    uint64_t escalado = denominador ? ((uint64_t)numerador * factor * UINT64_C(1000000)
        + denominador / 2) / denominador : 0;
    formato(b, "%" PRIu64 ".%06" PRIu64, escalado / 1000000, escalado % 1000000);
}
static void indice(Buffer *b, size_t i)
{
    if (i == VLIW_VACIO) texto(b, "null"); else formato(b, "%zu", i);
}

/* Se permite texto descriptivo ajeno al ensamblador, pero no UTF-8 inválido:
 * exportarlo como bytes sueltos produciría JSON inválido o datos alterados. */
static bool utf8(const char *s)
{
    if (!s) return true;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned c = *p++, restantes;
        if (c < 128) continue;
        uint32_t valor, minimo;
        if (c >= 0xc2 && c <= 0xdf) { restantes = 1; valor = c & 31u; minimo = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { restantes = 2; valor = c & 15u; minimo = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { restantes = 3; valor = c & 7u; minimo = 0x10000; }
        else return false;
        for (unsigned j = 0; j < restantes; ++j) {
            if ((*p & 0xc0) != 0x80) return false;
            valor = (valor << 6) | (*p++ & 63u);
        }
        if (valor < minimo || valor > 0x10ffff || (valor >= 0xd800 && valor <= 0xdfff)) return false;
    }
    return true;
}
static void json_cadena(Buffer *b, const char *s)
{
    if (!s) { texto(b, "null"); return; }
    texto(b, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (*p == '"' || *p == '\\') formato(b, "\\%c", *p);
        else if (*p < 32) formato(b, "\\u%04x", *p);
        else { char c[] = {(char)*p, 0}; texto(b, c); }
    }
    texto(b, "\"");
}
static void csv_cadena(Buffer *b, const char *s)
{
    texto(b, "\"");
    for (const char *p = s ? s : ""; *p; ++p) {
        if (*p == '"') texto(b, "\"\""); else { char c[] = {*p, 0}; texto(b, c); }
    }
    texto(b, "\"");
}
static const VliwDestino *destino(const Vista *v, size_t i)
{
    if (v->destinos) for (size_t t = 0; t < v->destinos->cantidad; ++t)
        if (v->destinos->items[t].instruccion == i) return &v->destinos->items[t];
    return NULL;
}
static bool sucesor(const VliwBloque *b, size_t i)
{
    for (size_t j = 0; j < b->cantidad_sucesores; ++j) if (b->sucesores[j] == i) return true;
    return false;
}
static VliwVisualEstado validar_anotaciones(Vista *v, const VliwDestinos *ds,
    VliwVisualDiagnostico *d)
{
    if (!ds) return VLIW_VIS_OK;
    if (ds->cantidad > v->e->cantidad_instrucciones || (ds->cantidad && !ds->items))
        return informar(d, VLIW_VIS_DESTINO, VLIW_VACIO, "tabla de anotaciones inválida");
    for (size_t t = 0; t < ds->cantidad; ++t) {
        const VliwDestino *a = &ds->items[t];
        if (a->instruccion >= v->e->cantidad_instrucciones || (unsigned)a->tipo > VLIW_DEST_NO_RESUELTO)
            return informar(d, VLIW_VIS_DESTINO, a->instruccion, "índice o tipo de destino inválido");
        const VliwInstruccion *in = &v->e->instrucciones[a->instruccion];
        const VliwBloque *bb = &v->e->bloques[in->bloque];
        if (in->control == VLIW_CONTROL_NINGUNO ||
            (a->tomado != VLIW_VACIO && (a->tomado >= v->e->cantidad_bloques || !sucesor(bb, a->tomado))) ||
            (a->caida != VLIW_VACIO && (a->caida >= v->e->cantidad_bloques || !sucesor(bb, a->caida))))
            return informar(d, VLIW_VIS_DESTINO, a->instruccion, "anotación ajena al control o CFG");
        for (size_t j = 0; j < t; ++j) if (ds->items[j].instruccion == a->instruccion)
            return informar(d, VLIW_VIS_DESTINO, a->instruccion, "anotación duplicada");
        /* Son anotaciones, no una certificación ISA/S8-A: se admite incluso
         * NO_RESUELTO para inspección, sin inferir o reparar sus operandos. */
    }
    v->destinos = ds;
    return VLIW_VIS_OK;
}

static void estadisticas_texto(Buffer *b, const Vista *v)
{
    const VliwEstadisticas *a = &v->estadisticas;
    formato(b, "\nEstadísticas estáticas: instrucciones=%zu bundles=%zu vacíos=%zu con_operaciones=%zu protección=%zu\n",
        a->instrucciones_utiles, a->bundles_totales, a->bundles_vacios, a->bundles_con_operaciones, a->bundles_proteccion);
    formato(b, "Slots: disponibles=%zu utilizados=%zu vacíos=%zu; utilización=", a->slots_disponibles, a->slots_utilizados, a->slots_vacios);
    cociente(b, a->slots_utilizados, a->slots_disponibles, 100);
    texto(b, "%; operaciones/bundle="); cociente(b, a->instrucciones_utiles, a->bundles_totales, 1);
    formato(b, "; bytes_IMEM=%zu bloques=%zu textos_ausentes=%zu\n", a->bytes_imem, a->bloques, a->textos_ausentes);
    for (unsigned s = 0; s < VLIW_SLOTS; ++s) formato(b, "%s=%zu%s", slots[s], a->operaciones_por_slot[s], s == 3 ? "\n" : " ");
    for (unsigned t = 0; t < VLIW_VIS_TIPOS_DEPENDENCIA; ++t) formato(b, "%s=%zu%s", tipos[t], a->dependencias_por_tipo[t], t == VLIW_DEP_BARRERA ? "\n" : " ");
}
static void estadisticas_json(Buffer *b, const Vista *v)
{
    const VliwEstadisticas *a = &v->estadisticas;
    formato(b, "{\"instrucciones_utiles\":%zu,\"bundles_totales\":%zu,\"bundles_vacios\":%zu,\"bundles_con_operaciones\":%zu,\"bundles_proteccion\":%zu,",
        a->instrucciones_utiles, a->bundles_totales, a->bundles_vacios, a->bundles_con_operaciones, a->bundles_proteccion);
    formato(b, "\"slots_disponibles\":%zu,\"slots_utilizados\":%zu,\"slots_vacios\":%zu,\"bytes_imem\":%zu,\"bloques\":%zu,\"textos_ausentes\":%zu,",
        a->slots_disponibles, a->slots_utilizados, a->slots_vacios, a->bytes_imem, a->bloques, a->textos_ausentes);
    texto(b, "\"utilizacion_porcentual\":"); cociente(b, a->slots_utilizados, a->slots_disponibles, 100);
    texto(b, ",\"operaciones_por_bundle\":"); cociente(b, a->instrucciones_utiles, a->bundles_totales, 1);
    texto(b, ",\"operaciones_por_slot\":{");
    for (unsigned s = 0; s < VLIW_SLOTS; ++s) formato(b, "%s\"%s\":%zu", s ? "," : "", slots[s], a->operaciones_por_slot[s]);
    texto(b, "},\"dependencias_por_tipo\":{");
    for (unsigned t = 0; t < VLIW_VIS_TIPOS_DEPENDENCIA; ++t) formato(b, "%s\"%s\":%zu", t ? "," : "", tipos[t], a->dependencias_por_tipo[t]);
    texto(b, "}}");
}

/* Cada celda de terminal es ASCII segura. El límite visual nunca alcanza los
 * datos CSV/JSON. Se incluye el índice original para distinguir texto e id. */
static void celda(const Vista *v, size_t i, size_t ancho, char salida[161])
{
    if (i == VLIW_VACIO) { strcpy(salida, "NOP"); return; }
    int n = snprintf(salida, 161, "I%zu: ", i);
    size_t j = (size_t)n;
    const unsigned char *s = (const unsigned char *)v->e->instrucciones[i].texto;
    if (!s || !*s) s = (const unsigned char *)"<SIN TEXTO>";
    bool truncado = false;
    for (; *s; ++s) {
        char pieza[8];
        if (*s < 32 || *s >= 127) (void)snprintf(pieza, sizeof pieza, "\\x%02X", *s);
        else if (*s == '|' || *s == '\\') { pieza[0] = '\\'; pieza[1] = (char)*s; pieza[2] = 0; }
        else { pieza[0] = (char)*s; pieza[1] = 0; }
        size_t k = strlen(pieza);
        if (k > ancho - j) { truncado = true; break; }
        memcpy(salida + j, pieza, k); j += k;
    }
    if (truncado) { if (j > ancho - 3) j = ancho - 3; memcpy(salida + j, "...", 3); j += 3; }
    salida[j] = 0;
}
static void tabla(Buffer *b, const Vista *v, const VliwVisualOpciones *o)
{
    size_t anchos[VLIW_SLOTS];
    for (unsigned s = 0; s < VLIW_SLOTS; ++s) {
        anchos[s] = o->ancho_slot ? o->ancho_slot : 8;
        if (!o->ancho_slot) for (size_t q = 0; q < v->p->cantidad_bundles; ++q) {
            char t[161]; celda(v, v->p->bundles[q].instrucciones[s], 96, t);
            if (strlen(t) > anchos[s]) anchos[s] = strlen(t);
        }
    }
    texto(b, "Calendarización VLIW estática (bundle IMEM; no es ciclo de ejecución)\n");
    formato(b, "Bundle | PC     | Bloque | Inicio | Clase       | Control | %-*s | %-*s | %-*s | %-*s\n",
        (int)anchos[0], slots[0], (int)anchos[1], slots[1], (int)anchos[2], slots[2], (int)anchos[3], slots[3]);
    for (size_t q = 0; q < v->p->cantidad_bundles; ++q) {
        const VliwBundle *bu = &v->p->bundles[q];
        formato(b, "%6zu | 0x%04zx | B%-5zu | %-6s | %-11s | %-7s", q, 16 * q, bu->bloque,
            q == v->p->inicio_por_bloque[bu->bloque] ? "SI" : "NO", clase(v, q), control(v, q) ? "SI" : "NO");
        for (unsigned s = 0; s < VLIW_SLOTS; ++s) { char t[161]; celda(v, bu->instrucciones[s], anchos[s], t); formato(b, " | %-*s", (int)anchos[s], t); }
        texto(b, "\n");
    }
    if (o->mostrar_dependencias) {
        texto(b, "\nDependencias externas (distancias en bundles estáticos):\n");
        for (size_t j = 0; j < v->e->cantidad_dependencias; ++j) {
            const VliwDependencia *a = &v->e->dependencias[j];
            size_t p = v->p->bundle_por_instruccion[a->antes], c = v->p->bundle_por_instruccion[a->despues];
            formato(b, "%s I%zu -> I%zu: productor=%zu consumidor=%zu distancia=%zu mínimo=%zu SATISFECHA\n", tipos[a->tipo], a->antes, a->despues, p, c, c-p, a->distancia);
        }
    }
    if (v->destinos && v->destinos->cantidad) {
        texto(b, "\nDestinos anotados (operandos originales conservados):\n");
        for (size_t t = 0; t < v->destinos->cantidad; ++t) {
            const VliwDestino *a = &v->destinos->items[t];
            formato(b, "I%zu %s tomado=", a->instruccion, destinos_tipo[a->tipo]); indice(b, a->tomado);
            texto(b, " caída="); indice(b, a->caida); texto(b, "\n");
        }
    }
    if (o->mostrar_estadisticas) estadisticas_texto(b, v);
}
static void csv(Buffer *b, const Vista *v)
{
    texto(b, "bundle,pc,bloque,alu,lsu,bru,cripto,operaciones,inicio_bloque,proteccion,control,clase,alu_indice,lsu_indice,bru_indice,cripto_indice,alu_texto_ausente,lsu_texto_ausente,bru_texto_ausente,cripto_texto_ausente\r\n");
    for (size_t q = 0; q < v->p->cantidad_bundles; ++q) {
        const VliwBundle *bu = &v->p->bundles[q];
        formato(b, "%zu,0x%04zx,%zu", q, 16 * q, bu->bloque);
        for (unsigned s = 0; s < VLIW_SLOTS; ++s) {
            size_t i = bu->instrucciones[s]; texto(b, ",");
            csv_cadena(b, i == VLIW_VACIO ? "NOP" : v->e->instrucciones[i].texto);
        }
        formato(b, ",%u,%u,%u,%u,%s", bu->operaciones, q == v->p->inicio_por_bloque[bu->bloque], proteccion(v, q), control(v, q), clase(v, q));
        for (unsigned s = 0; s < VLIW_SLOTS; ++s) { texto(b, ","); if (bu->instrucciones[s] != VLIW_VACIO) formato(b, "%zu", bu->instrucciones[s]); }
        for (unsigned s = 0; s < VLIW_SLOTS; ++s) formato(b, ",%u", bu->instrucciones[s] != VLIW_VACIO && ausente(v->e->instrucciones[bu->instrucciones[s]].texto));
        texto(b, "\r\n");
    }
}
static void json(Buffer *b, const Vista *v)
{
    texto(b, "{\"version\":1,\"resumen\":{\"naturaleza\":\"estatica\",\"unidad\":\"bundle_IMEM\",\"origen_pc\":0,\"plan_verificado\":true},\"bundles\":[");
    for (size_t q = 0; q < v->p->cantidad_bundles; ++q) {
        const VliwBundle *bu = &v->p->bundles[q];
        formato(b, "%s{\"indice\":%zu,\"pc\":%zu,\"bloque\":%zu,\"operaciones\":%u,\"inicio_bloque\":%s,\"proteccion\":%s,\"control\":%s,\"clase\":\"%s\",\"slots\":[",
            q ? "," : "", q, 16*q, bu->bloque, bu->operaciones, q == v->p->inicio_por_bloque[bu->bloque] ? "true" : "false", proteccion(v, q) ? "true" : "false", control(v, q) ? "true" : "false", clase(v, q));
        for (unsigned s = 0; s < VLIW_SLOTS; ++s) {
            size_t i = bu->instrucciones[s]; const char *t = i == VLIW_VACIO ? NULL : v->e->instrucciones[i].texto;
            formato(b, "%s{\"slot\":\"%s\",\"instruccion\":", s ? "," : "", slots[s]); indice(b, i);
            texto(b, ",\"texto\":"); json_cadena(b, t);
            formato(b, ",\"texto_ausente\":%s}", i != VLIW_VACIO && ausente(t) ? "true" : "false");
        }
        texto(b, "]}");
    }
    texto(b, "],\"bloques\":[");
    for (size_t j = 0; j < v->e->cantidad_bloques; ++j) {
        const VliwBloque *bb = &v->e->bloques[j];
        formato(b, "%s{\"indice\":%zu,\"etiqueta\":\"B%zu\",\"inicio_bundle\":%zu,\"fin_bundle\":%zu,\"pc\":%zu,\"proteccion_bundles\":%u,\"sucesores\":[", j ? "," : "", j, j, v->p->inicio_por_bloque[j], j+1 < v->p->cantidad_bloques ? v->p->inicio_por_bloque[j+1] : v->p->cantidad_bundles, 16*v->p->inicio_por_bloque[j], j || v->protege_inicial ? 2u : 0u);
        for (size_t k = 0; k < bb->cantidad_sucesores; ++k) formato(b, "%s%zu", k ? "," : "", bb->sucesores[k]);
        texto(b, "]}");
    }
    texto(b, "],\"instrucciones\":[");
    for (size_t i = 0; i < v->e->cantidad_instrucciones; ++i) {
        const VliwInstruccion *in = &v->e->instrucciones[i]; const VliwDestino *a = destino(v, i);
        formato(b, "%s{\"indice\":%zu,\"id\":%zu,\"slot\":\"%s\",\"bloque\":%zu,\"bundle\":%zu,\"pc\":%zu,\"lee\":%" PRIu64 ",\"escribe\":%" PRIu64 ",\"efectos\":%u,\"control\":%u,\"texto\":", i ? "," : "", i, in->id, slots[in->slot], in->bloque, v->p->bundle_por_instruccion[i], 16*v->p->bundle_por_instruccion[i], in->lee, in->escribe, in->efectos, (unsigned)in->control);
        json_cadena(b, in->texto); formato(b, ",\"texto_ausente\":%s,\"destino\":", ausente(in->texto) ? "true" : "false");
        if (!a) texto(b, "null");
        else { formato(b, "{\"tipo\":\"%s\",\"tomado\":", destinos_tipo[a->tipo]); indice(b, a->tomado); texto(b, ",\"caida\":"); indice(b, a->caida); texto(b, "}"); }
        texto(b, "}");
    }
    texto(b, "],\"dependencias\":[");
    for (size_t j = 0; j < v->e->cantidad_dependencias; ++j) {
        const VliwDependencia *a = &v->e->dependencias[j];
        size_t p = v->p->bundle_por_instruccion[a->antes], c = v->p->bundle_por_instruccion[a->despues];
        formato(b, "%s{\"origen\":%zu,\"destino\":%zu,\"tipo\":\"%s\",\"requerida\":%zu,\"efectiva\":%zu,\"bundle_origen\":%zu,\"bundle_destino\":%zu,\"estado\":\"SATISFECHA\"}", j ? "," : "", a->antes, a->despues, tipos[a->tipo], a->distancia, c-p, p, c);
    }
    texto(b, "],\"estadisticas\":"); estadisticas_json(b, v); texto(b, "}\n");
}
/* salida debe apuntar inicialmente a NULL: recibe una cadena propia solo en
 * éxito. destinos puede ser NULL o contener anotaciones parciales por índice
 * original; no se infieren ni reescriben operandos. opciones=NULL usa 48,true,true.
 * CSV/JSON conservan textos completos UTF-8 válidos, incluidos controles
 * escapados. NULL/"" son texto ausente, distinto de un slot NOP. La tabla
 * escapa bytes no imprimibles/no ASCII y marca el truncamiento con "...".
 * Las opciones de secciones solo afectan la tabla; JSON siempre es completo.
 * No requiere enlazar el emisor, el codificador ni el simulador. */
VliwVisualEstado vliw_visualizar(const VliwEntrada *e, const VliwPlan *p,
    const VliwDestinos *ds, VliwVisualFormato f, const VliwVisualOpciones *op,
    char **salida, VliwVisualDiagnostico *d)
{
    iniciar_diagnostico(d);
    VliwVisualOpciones o = op ? *op : (VliwVisualOpciones){48, true, true};
    if (!salida || *salida || (unsigned)f > VLIW_VIS_JSON ||
        (o.ancho_slot && (o.ancho_slot < 8 || o.ancho_slot > 160)))
        return informar(d, VLIW_VIS_ARGUMENTO, VLIW_VACIO, "salida NULL inicial, formato y ancho válidos requeridos");
    Vista v;
    VliwVisualEstado s = preparar(e, p, &v, d);
    if (s != VLIW_VIS_OK) return s;
    s = validar_anotaciones(&v, ds, d);
    if (s != VLIW_VIS_OK) return s;
    for (size_t i = 0; i < e->cantidad_instrucciones; ++i)
        if (!utf8(e->instrucciones[i].texto)) return informar(d, VLIW_VIS_TEXTO, i, "texto no es UTF-8 válido");
    Buffer b = {0};
    if (f == VLIW_VIS_TABLA) tabla(&b, &v, &o);
    else if (f == VLIW_VIS_CSV) csv(&b, &v);
    else json(&b, &v);
    if (b.estado != VLIW_VIS_OK) {
        free(b.datos);
        return informar(d, b.estado, VLIW_VACIO, b.estado == VLIW_VIS_MEMORIA ? "sin memoria para el reporte" : "error al generar el reporte");
    }
    /* Único punto de publicación: no se devuelve un reporte parcial ni se
     * presenta como válido un plan que S7 rechazó. */
    *salida = b.datos;
    return VLIW_VIS_OK;
}
/* Libera el reporte propio del visualizador y deja *s en NULL.
 * Admite un doble puntero NULL y no modifica el plan ni sus anotaciones. */
void vliw_visual_liberar(char **s)
{
    if (s) { free(*s); *s = NULL; }
}
