/*
 * Ventana "Métricas" (ver include/metricas_ventana.h).
 *
 * Solo muestra lo que calcula metricas.c con el simulador de Javier.
 * Las gráficas son barras horizontales dibujadas con cairo en un
 * GtkDrawingArea:
 *   - Cada barra empieza en la misma línea base (a la izquierda) y su
 *     largo es proporcional al valor. El valor va escrito al final de la
 *     barra, así que no hace falta leer un eje.
 *   - Colores (tema oscuro, como el editor): azul para una sola serie;
 *     azul = "con código muerto" y naranja = "sin código muerto" en la
 *     comparación, siempre con leyenda (no solo el color dice qué es cuál).
 *   - Al pasar el mouse sobre una barra, un tooltip dice su valor exacto.
 * En "Antes y después del DCE" cada fila (ciclos, IPC...) tiene su propia
 * escala, porque son cantidades de tamaños muy distintos: lo que se compara
 * es la barra de arriba con la de abajo de la MISMA fila.
 */
#include "metricas_ventana.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ide_dce.h"
#include "metricas.h"

/* ------------------------------------------------------------------ */
/* Colores (tema oscuro, Dark+ como el editor)                        */
/* ------------------------------------------------------------------ */

typedef struct { double r, g, b; } Color;
#define COLOR(hex) {((hex) >> 16 & 0xFF) / 255.0, ((hex) >> 8 & 0xFF) / 255.0, ((hex) & 0xFF) / 255.0}

static const Color FONDO = COLOR(0x1E1E1E);       /* Fondo de las gráficas (el del editor). */
static const Color TEXTO = COLOR(0xF0EFEC);       /* Títulos y nombres de las filas. */
static const Color TEXTO_SUAVE = COLOR(0xC3C2B7); /* Subtítulos, valores y notas. */
static const Color EJE = COLOR(0x4A4A4A);         /* La línea base de las barras. */
static const Color SERIES[2] = {COLOR(0x3987E5),  /* Azul: serie 1. */
                                COLOR(0xD95926)}; /* Naranja: serie 2. */

/* ------------------------------------------------------------------ */
/* Una gráfica de barras horizontales                                 */
/* ------------------------------------------------------------------ */

#define MAX_FILAS 6

/* Una fila: su nombre y una barra por serie (1 o 2). */
typedef struct {
    char nombre[64];
    char nota[64];         /* Segunda línea debajo del nombre (por ejemplo "−64.9 %"). */
    double valor[2];       /* Largo de cada barra (en las unidades de la gráfica). */
    char texto[2][64];     /* Lo que se escribe al final de cada barra. */
    char tip[2][200];      /* Tooltip de cada barra. */
} Fila;

typedef struct {
    char titulo[96];
    char subtitulo[200];
    int series;                 /* 1 o 2. */
    const char *leyenda[2];     /* Nombre de cada serie (solo con 2). */
    int escala_por_fila;        /* 1: cada fila con su propio máximo. 0: "maximo" para todas. */
    double maximo;
    Fila filas[MAX_FILAS];
    int cantidad;
    /* Dónde quedó cada barra al dibujar, para el tooltip. */
    GdkRectangle zonas[MAX_FILAS * 2];
    const char *tips[MAX_FILAS * 2];
    int cantidad_zonas;
} Grafica;

/* Medidas en píxeles. */
enum {
    MARGEN = 16,
    ALTO_TITULO = 22,
    ALTO_SUBTITULO = 20,
    ALTO_LEYENDA = 24,
    ANCHO_NOMBRE = 230,    /* Columna de los nombres, a la izquierda de las barras. */
    ANCHO_VALOR = 170,     /* Espacio a la derecha para el texto de la barra más larga. */
    ALTO_BARRA = 16,
    HUECO_BARRAS = 2,      /* Entre las dos barras de una misma fila. */
    ESPACIO_FILAS = 16
};

/* Alto de una fila: sus barras, o dos líneas de texto si tiene nota. */
static int alto_fila(const Grafica *g, const Fila *f)
{
    int barras = g->series * ALTO_BARRA + (g->series - 1) * HUECO_BARRAS;
    int texto = f->nota[0] != '\0' ? 34 : 18;
    return barras > texto ? barras : texto;
}

/* Alto total de la gráfica (para pedirle ese alto al GtkDrawingArea). */
static int alto_grafica(const Grafica *g)
{
    int alto = MARGEN + ALTO_TITULO + ALTO_SUBTITULO + 8 + MARGEN;
    if (g->series == 2) alto += ALTO_LEYENDA;
    for (int i = 0; i < g->cantidad; ++i) alto += alto_fila(g, &g->filas[i]) + ESPACIO_FILAS;
    return alto;
}

/* Escribe un texto con su centro vertical en y. Si "derecha", x es donde
 * termina el texto. Devuelve el ancho en píxeles. */
static int escribir(cairo_t *cr, double x, double y, const char *texto, Color c, int negrita,
                    int puntos, int derecha)
{
    PangoLayout *capa = pango_cairo_create_layout(cr);
    PangoFontDescription *letra = pango_font_description_from_string("Sans");
    pango_font_description_set_size(letra, puntos * PANGO_SCALE);
    if (negrita) pango_font_description_set_weight(letra, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(capa, letra);
    pango_layout_set_text(capa, texto, -1);
    int ancho = 0, alto = 0;
    pango_layout_get_pixel_size(capa, &ancho, &alto);
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_move_to(cr, derecha ? x - ancho : x, y - alto / 2.0);
    pango_cairo_show_layout(cr, capa);
    pango_font_description_free(letra);
    g_object_unref(capa);
    return ancho;
}

/* Una barra que nace en la línea base (x) y tiene la punta redondeada
 * (radio 4) solo del lado del valor. */
static void barra(cairo_t *cr, double x, double y, double ancho, double alto, Color c)
{
    double r = fmin(4.0, fmin(ancho, alto / 2.0));
    cairo_new_path(cr);
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x + ancho - r, y);
    cairo_arc(cr, x + ancho - r, y + r, r, -G_PI / 2, 0);
    cairo_line_to(cr, x + ancho, y + alto - r);
    cairo_arc(cr, x + ancho - r, y + alto - r, r, 0, G_PI / 2);
    cairo_line_to(cr, x, y + alto);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_fill(cr);
}

/* Dibuja la gráfica "datos" en el GtkDrawingArea. */
static gboolean al_dibujar(GtkWidget *area, cairo_t *cr, gpointer datos)
{
    Grafica *g = datos;
    int ancho = gtk_widget_get_allocated_width(area);
    int alto = gtk_widget_get_allocated_height(area);

    cairo_set_source_rgb(cr, FONDO.r, FONDO.g, FONDO.b);
    cairo_rectangle(cr, 0, 0, ancho, alto);
    cairo_fill(cr);

    /* Título y subtítulo. */
    double y = MARGEN;
    escribir(cr, MARGEN, y + ALTO_TITULO / 2.0, g->titulo, TEXTO, 1, 12, 0);
    y += ALTO_TITULO;
    escribir(cr, MARGEN, y + ALTO_SUBTITULO / 2.0, g->subtitulo, TEXTO_SUAVE, 0, 9, 0);
    y += ALTO_SUBTITULO;

    /* Leyenda: un cuadrito del color de cada serie y su nombre. */
    if (g->series == 2) {
        double x = MARGEN;
        double centro = y + ALTO_LEYENDA / 2.0 + 2;
        for (int s = 0; s < 2; ++s) {
            barra(cr, x, centro - 5, 10, 10, SERIES[s]);
            x += 16;
            x += escribir(cr, x, centro, g->leyenda[s], TEXTO, 0, 9, 0) + 22;
        }
        y += ALTO_LEYENDA;
    }
    y += 8;

    /* Las filas. */
    const double base = MARGEN + ANCHO_NOMBRE;
    double disponible = ancho - base - ANCHO_VALOR - MARGEN;
    if (disponible < 40) disponible = 40;
    double primera = y;
    g->cantidad_zonas = 0;

    for (int i = 0; i < g->cantidad; ++i) {
        Fila *f = &g->filas[i];
        int alto_f = alto_fila(g, f);

        /* Nombre (y nota debajo, si hay). */
        if (f->nota[0] != '\0') {
            escribir(cr, MARGEN, y + 9, f->nombre, TEXTO, 0, 10, 0);
            escribir(cr, MARGEN, y + 26, f->nota, TEXTO_SUAVE, 1, 9, 0);
        } else {
            escribir(cr, MARGEN, y + alto_f / 2.0, f->nombre, TEXTO, 0, 10, 0);
        }

        /* Máximo de la escala: el de la gráfica, o el de esta fila. */
        double maximo = g->maximo;
        if (g->escala_por_fila) maximo = fmax(f->valor[0], g->series == 2 ? f->valor[1] : 0);

        int alto_barras = g->series * ALTO_BARRA + (g->series - 1) * HUECO_BARRAS;
        double yb = y + (alto_f - alto_barras) / 2.0;
        for (int s = 0; s < g->series; ++s) {
            double largo = maximo > 0 ? f->valor[s] / maximo * disponible : 0;
            if (f->valor[s] > 0 && largo < 2) largo = 2;   /* Que un valor pequeño se vea. */
            if (largo > 0) barra(cr, base, yb, largo, ALTO_BARRA, SERIES[s]);
            escribir(cr, base + largo + 6, yb + ALTO_BARRA / 2.0, f->texto[s], TEXTO_SUAVE, 0, 9, 0);

            /* Zona del tooltip: toda la franja de la barra (más fácil de apuntar). */
            GdkRectangle *z = &g->zonas[g->cantidad_zonas];
            z->x = MARGEN;
            z->y = (int)yb - HUECO_BARRAS / 2;
            z->width = ancho - 2 * MARGEN;
            z->height = ALTO_BARRA + HUECO_BARRAS;
            g->tips[g->cantidad_zonas++] = f->tip[s];
            yb += ALTO_BARRA + HUECO_BARRAS;
        }
        y += alto_f + ESPACIO_FILAS;
    }

    /* Línea base, discreta, donde nacen todas las barras. */
    cairo_set_source_rgb(cr, EJE.r, EJE.g, EJE.b);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, base - 0.5, primera - 4);
    cairo_line_to(cr, base - 0.5, y - ESPACIO_FILAS + 4);
    cairo_stroke(cr);
    return FALSE;
}

/* Tooltip: el valor exacto de la barra bajo el mouse. */
static gboolean al_pedir_tooltip(GtkWidget *area, gint x, gint y, gboolean teclado,
                                 GtkTooltip *tooltip, gpointer datos)
{
    (void)area;
    (void)teclado;
    Grafica *g = datos;
    for (int i = 0; i < g->cantidad_zonas; ++i) {
        const GdkRectangle *z = &g->zonas[i];
        if (x >= z->x && x < z->x + z->width && y >= z->y && y < z->y + z->height) {
            gtk_tooltip_set_text(tooltip, g->tips[i]);
            gtk_tooltip_set_tip_area(tooltip, z);
            return TRUE;
        }
    }
    return FALSE;
}

/* El widget de una gráfica. "g" debe seguir vivo mientras la ventana exista. */
static GtkWidget *widget_grafica(Grafica *g)
{
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 560, alto_grafica(g));
    gtk_widget_set_has_tooltip(area, TRUE);
    g_signal_connect(area, "draw", G_CALLBACK(al_dibujar), g);
    g_signal_connect(area, "query-tooltip", G_CALLBACK(al_pedir_tooltip), g);
    return area;
}

/* ------------------------------------------------------------------ */
/* Los datos de la ventana                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    MetMedicion actual;         /* El código del editor. */
    int hay_dce;                /* 1: también se midió el código sin el código muerto. */
    MetMedicion sin_muerto;
    char nota_dce[400];         /* Si hay_dce es 0: por qué no hay comparación. */
    Grafica slots, bundles, dce;
} Datos;

static GtkWidget *ventana_met = NULL;
static Datos *datos_met = NULL;

static void al_destruir_metricas(GtkWidget *widget, gpointer datos)
{
    (void)widget;
    (void)datos;
    if (datos_met != NULL) {
        met_liberar(&datos_met->actual);
        met_liberar(&datos_met->sin_muerto);
        g_free(datos_met);
    }
    datos_met = NULL;
    ventana_met = NULL;
}

/* Un número con espacios cada tres cifras: 1234567 -> "1 234 567". */
static void miles(uint64_t n, char *texto, size_t largo)
{
    char cifras[32];
    snprintf(cifras, sizeof cifras, "%llu", (unsigned long long)n);
    size_t total = strlen(cifras), j = 0;
    for (size_t i = 0; i < total && j + 2 < largo; ++i) {
        if (i > 0 && (total - i) % 3 == 0) texto[j++] = ' ';
        texto[j++] = cifras[i];
    }
    texto[j] = '\0';
}

/* Cuánto cambió de "antes" a "despues", como "−64.9 %" o "sin cambio". */
static void cambio(double antes, double despues, char *texto, size_t largo)
{
    if (antes == despues) snprintf(texto, largo, "sin cambio");
    else if (antes == 0) snprintf(texto, largo, "antes era 0");
    else snprintf(texto, largo, "%s%.1f %%", despues < antes ? "−" : "+",
                  fabs(100.0 * (despues - antes) / antes));
}

/* Gráfica "Uso de cada slot": % de los bundles ejecutados con una
 * instrucción en ese slot. Escala común de 0 a 100 %. */
static void armar_slots(Grafica *g, const SimMetricasResumen *s)
{
    snprintf(g->titulo, sizeof g->titulo, "Uso de cada slot del bundle");
    snprintf(g->subtitulo, sizeof g->subtitulo,
             "Porcentaje de los bundles ejecutados que traían una instrucción en ese slot");
    g->series = 1;
    g->maximo = 100.0;
    g->cantidad = 4;
    char total[32];
    miles(s->completados, total, sizeof total);
    for (int i = 0; i < 4; ++i) {
        Fila *f = &g->filas[i];
        double pct = met_porcentaje(s->instrucciones_por_slot[i], s->completados);
        char n[32];
        miles(s->instrucciones_por_slot[i], n, sizeof n);
        snprintf(f->nombre, sizeof f->nombre, "%s", met_nombre_slot(i));
        f->valor[0] = pct;
        snprintf(f->texto[0], sizeof f->texto[0], "%.1f %%", pct);
        snprintf(f->tip[0], sizeof f->tip[0], "%s: %s instrucciones en %s bundles ejecutados (%.1f %%)",
                 met_nombre_slot(i), n, total, pct);
    }
}

/* Gráfica "Bundles ejecutados": con trabajo y solo NOP. */
static void armar_bundles(Grafica *g, const SimMetricasResumen *s)
{
    snprintf(g->titulo, sizeof g->titulo, "Bundles ejecutados");
    snprintf(g->subtitulo, sizeof g->subtitulo,
             "Los bundles solo NOP son esperas: por el resultado de la instrucción anterior o "
             "después de un salto");
    g->series = 1;
    g->maximo = (double)s->completados;
    g->cantidad = 2;
    const char *nombres[2] = {"Con al menos una instrucción", "Solo NOP"};
    uint64_t valores[2] = {s->bundles_utiles, s->bundles_nop};
    for (int i = 0; i < 2; ++i) {
        Fila *f = &g->filas[i];
        char n[32];
        miles(valores[i], n, sizeof n);
        double pct = met_porcentaje(valores[i], s->completados);
        snprintf(f->nombre, sizeof f->nombre, "%s", nombres[i]);
        f->valor[0] = (double)valores[i];
        snprintf(f->texto[0], sizeof f->texto[0], "%s (%.1f %%)", n, pct);
        snprintf(f->tip[0], sizeof f->tip[0], "%s: %s bundles, el %.1f %% de los ejecutados",
                 nombres[i], n, pct);
    }
}

/* Una fila de la comparación antes/después del DCE. "decimales" < 0: es un
 * conteo (se escribe con miles); si no, un número con esos decimales. */
static void fila_dce(Grafica *g, const char *nombre, double antes, double despues, int decimales)
{
    Fila *f = &g->filas[g->cantidad++];
    snprintf(f->nombre, sizeof f->nombre, "%s", nombre);
    cambio(antes, despues, f->nota, sizeof f->nota);
    double valores[2] = {antes, despues};
    for (int s = 0; s < 2; ++s) {
        char n[32];
        if (decimales < 0) miles((uint64_t)valores[s], n, sizeof n);
        else snprintf(n, sizeof n, "%.*f", decimales, valores[s]);
        f->valor[s] = valores[s];
        snprintf(f->texto[s], sizeof f->texto[s], "%s", n);
        snprintf(f->tip[s], sizeof f->tip[s], "%s, %s: %s", nombre, g->leyenda[s], n);
    }
}

static void armar_dce(Grafica *g, const MetMedicion *antes, const MetMedicion *despues)
{
    snprintf(g->titulo, sizeof g->titulo, "Antes y después de eliminar el código muerto");
    snprintf(g->subtitulo, sizeof g->subtitulo,
             "Cada fila tiene su propia escala: se compara la barra azul con la naranja de la "
             "misma fila");
    g->series = 2;
    g->leyenda[0] = "Con código muerto (el código del editor)";
    g->leyenda[1] = "Sin código muerto";
    g->escala_por_fila = 1;
    const SimMetricasResumen *a = &antes->sim, *d = &despues->sim;
    fila_dce(g, "Ciclos", (double)a->ciclos, (double)d->ciclos, -1);
    fila_dce(g, "Instrucciones ejecutadas", (double)a->instrucciones_utiles,
             (double)d->instrucciones_utiles, -1);
    fila_dce(g, "Bundles solo NOP ejecutados", (double)a->bundles_nop, (double)d->bundles_nop, -1);
    fila_dce(g, "IPC (instrucciones por ciclo)", a->ipc, d->ipc, 3);
    fila_dce(g, "Tamaño del programa (bundles)", (double)antes->bundles, (double)despues->bundles, -1);
}

/* Mide el código del editor y, si tiene código muerto, también el código
 * sin él (el mismo que da "Aplicar al código" del botón Código muerto). */
static void calcular(Datos *d, const char *texto, const char *ruta)
{
    if (met_medir(texto, strlen(texto), ruta, 0, &d->actual) != 0) {
        d->actual.estado = MET_FALLO_SIMULADOR;
        return;
    }
    if (d->actual.estado != MET_LISTO) {
        snprintf(d->nota_dce, sizeof d->nota_dce,
                 "La comparación necesita que el código del editor termine en el simulador.");
        return;
    }

    IdeDce dce;
    if (ide_dce_analizar(texto, strlen(texto), ruta, &dce) != 0) {
        snprintf(d->nota_dce, sizeof d->nota_dce, "No se pudo hacer el DCE (falta de memoria).");
        return;
    }
    if (dce.estado == IDE_DCE_SIN_IR) {
        snprintf(d->nota_dce, sizeof d->nota_dce,
                 "El DCE no se pudo hacer: la IR todavía no soporta este programa (%s).",
                 dce.motivo ? dce.motivo : "");
    } else if (dce.estado != IDE_DCE_LISTO || dce.codigo_nuevo == NULL) {
        snprintf(d->nota_dce, sizeof d->nota_dce,
                 "Este código no tiene código muerto que se pueda quitar: no hay nada que comparar.");
    } else if (met_medir(dce.codigo_nuevo, strlen(dce.codigo_nuevo), ruta, 0, &d->sin_muerto) != 0 ||
               d->sin_muerto.estado != MET_LISTO) {
        snprintf(d->nota_dce, sizeof d->nota_dce,
                 "El código sin el código muerto no se pudo ejecutar en el simulador%s%s.",
                 d->sin_muerto.motivo ? ": " : "", d->sin_muerto.motivo ? d->sin_muerto.motivo : "");
    } else {
        d->hay_dce = 1;
        armar_dce(&d->dce, &d->actual, &d->sin_muerto);
    }
    ide_dce_liberar(&dce);
}

/* ------------------------------------------------------------------ */
/* Las partes de la ventana                                            */
/* ------------------------------------------------------------------ */

/* Un número grande con su nombre debajo, para la fila de arriba. */
static GtkWidget *numero_principal(const char *valor, const char *nombre)
{
    GtkWidget *etiqueta = gtk_label_new(NULL);
    gchar *marcado = g_markup_printf_escaped(
        "<span size='xx-large' weight='bold'>%s</span>\n<span size='small'>%s</span>", valor, nombre);
    gtk_label_set_markup(GTK_LABEL(etiqueta), marcado);
    g_free(marcado);
    gtk_label_set_justify(GTK_LABEL(etiqueta), GTK_JUSTIFY_CENTER);
    return etiqueta;
}

static GtkWidget *fila_principal(const MetMedicion *m)
{
    char ciclos[32], instrucciones[32], bundles[32], ipc[32];
    miles(m->sim.ciclos, ciclos, sizeof ciclos);
    miles(m->sim.instrucciones_utiles, instrucciones, sizeof instrucciones);
    miles(m->bundles, bundles, sizeof bundles);
    snprintf(ipc, sizeof ipc, "%.3f", m->sim.ipc);
    GtkWidget *fila = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous(GTK_BOX(fila), TRUE);
    gtk_box_pack_start(GTK_BOX(fila), numero_principal(ciclos, "ciclos"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(fila), numero_principal(ipc, "IPC (instrucciones por ciclo)"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(fila), numero_principal(instrucciones, "instrucciones ejecutadas"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(fila), numero_principal(bundles, "bundles en el programa"), TRUE, TRUE, 0);
    return fila;
}

/* Un texto que ocupa el ancho y se parte en líneas. */
static GtkWidget *parrafo(const char *marcado)
{
    GtkWidget *etiqueta = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(etiqueta), marcado);
    gtk_label_set_xalign(GTK_LABEL(etiqueta), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(etiqueta), TRUE);
    return etiqueta;
}

/* Varias gráficas una debajo de otra, con barra de desplazamiento. */
static GtkWidget *columna_graficas(Grafica **graficas, int cantidad)
{
    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    /* La última se estira hasta abajo: así el fondo oscuro llena la pestaña. */
    for (int i = 0; i < cantidad; ++i) {
        int ultima = i == cantidad - 1;
        gtk_box_pack_start(GTK_BOX(caja), widget_grafica(graficas[i]), ultima, ultima, 0);
    }
    GtkWidget *recuadro = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(recuadro), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(recuadro), caja);
    return recuadro;
}

/* Una fila de la pestaña Tabla. "otra" es NULL si no hay comparación. */
typedef struct { const char *nombre; uint64_t valor[2]; int es_ipc; double ipc[2]; } FilaTabla;

static GtkWidget *pestana_tabla(const MetMedicion *m, const MetMedicion *otra)
{
    const SimMetricasResumen *a = &m->sim;
    const SimMetricasResumen *b = otra ? &otra->sim : &m->sim;
    const FilaTabla filas[] = {
        {"Ciclos", {a->ciclos, b->ciclos}, 0, {0, 0}},
        {"IPC (instrucciones por ciclo)", {0, 0}, 1, {a->ipc, b->ipc}},
        {"Instrucciones ejecutadas", {a->instrucciones_utiles, b->instrucciones_utiles}, 0, {0, 0}},
        {"Bundles ejecutados", {a->completados, b->completados}, 0, {0, 0}},
        {"   con al menos una instrucción", {a->bundles_utiles, b->bundles_utiles}, 0, {0, 0}},
        {"   solo NOP", {a->bundles_nop, b->bundles_nop}, 0, {0, 0}},
        {"Instrucciones en el slot ALU", {a->instrucciones_por_slot[0], b->instrucciones_por_slot[0]}, 0, {0, 0}},
        {"Instrucciones en el slot LSU", {a->instrucciones_por_slot[1], b->instrucciones_por_slot[1]}, 0, {0, 0}},
        {"Instrucciones en el slot BRU", {a->instrucciones_por_slot[2], b->instrucciones_por_slot[2]}, 0, {0, 0}},
        {"Instrucciones en el slot CRIPTO", {a->instrucciones_por_slot[3], b->instrucciones_por_slot[3]}, 0, {0, 0}},
        {"Saltos tomados", {a->saltos_tomados, b->saltos_tomados}, 0, {0, 0}},
        {"Saltos no tomados", {a->saltos_no_tomados, b->saltos_no_tomados}, 0, {0, 0}},
        {"Bundles descartados por un salto (flush)", {a->anulados_salto, b->anulados_salto}, 0, {0, 0}},
        {"Lecturas de memoria (load)", {a->lsu_lecturas_ex, b->lsu_lecturas_ex}, 0, {0, 0}},
        {"Escrituras de memoria (store)", {a->lsu_escrituras_ex, b->lsu_escrituras_ex}, 0, {0, 0}},
        {"Excepciones (traps)", {a->traps, b->traps}, 0, {0, 0}},
        {"Bundles en el programa", {m->bundles, otra ? otra->bundles : m->bundles}, 0, {0, 0}},
    };
    int columnas = otra ? 4 : 2;
    GtkListStore *modelo = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    for (size_t i = 0; i < sizeof filas / sizeof filas[0]; ++i) {
        const FilaTabla *f = &filas[i];
        char v[2][32], dif[32] = "";
        for (int s = 0; s < 2; ++s) {
            if (f->es_ipc) snprintf(v[s], sizeof v[s], "%.3f", f->ipc[s]);
            else miles(f->valor[s], v[s], sizeof v[s]);
        }
        if (otra) {
            if (f->es_ipc) cambio(f->ipc[0], f->ipc[1], dif, sizeof dif);
            else cambio((double)f->valor[0], (double)f->valor[1], dif, sizeof dif);
        }
        gtk_list_store_insert_with_values(modelo, NULL, -1, 0, f->nombre, 1, v[0], 2, v[1], 3, dif, -1);
    }
    GtkWidget *vista = gtk_tree_view_new_with_model(GTK_TREE_MODEL(modelo));
    g_object_unref(modelo);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(vista), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    const char *titulos[4] = {"Métrica", otra ? "Con código muerto" : "Valor", "Sin código muerto", "Cambio"};
    for (int i = 0; i < columnas; ++i) {
        GtkCellRenderer *celda = gtk_cell_renderer_text_new();
        if (i > 0) g_object_set(celda, "xalign", 1.0, NULL);   /* Números alineados a la derecha. */
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(titulos[i], celda, "text", i, NULL);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(vista), col);
    }
    GtkWidget *recuadro = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(recuadro), vista);
    return recuadro;
}

/* Contenido cuando no hay métricas que mostrar. */
static const char *sin_metricas(const MetMedicion *m)
{
    switch (m->estado) {
        case MET_CON_ERRORES:
            return "Este archivo tiene errores: corrígelos (pestaña Errores) para medirlo.";
        case MET_SIN_BACKEND:
            return "El código es válido, pero no se pudo generar el binario:";
        case MET_FALLO_SIMULADOR:
            return "El simulador se detuvo con un error:";
        default:
            return NULL;
    }
}

void ide_mostrar_metricas(GtkWindow *padre, const char *texto, const char *ruta, const char *nombre)
{
    if (ventana_met != NULL) gtk_widget_destroy(ventana_met);

    /* Puede tardar un poco (compila y simula hasta dos veces): cursor de espera. */
    GdkWindow *gdk_padre = gtk_widget_get_window(GTK_WIDGET(padre));
    GdkCursor *espera = gdk_padre ? gdk_cursor_new_for_display(gdk_window_get_display(gdk_padre), GDK_WATCH) : NULL;
    if (gdk_padre) {
        gdk_window_set_cursor(gdk_padre, espera);
        while (gtk_events_pending()) gtk_main_iteration();
    }
    datos_met = g_new0(Datos, 1);
    calcular(datos_met, texto, ruta);
    if (gdk_padre) gdk_window_set_cursor(gdk_padre, NULL);
    if (espera) g_object_unref(espera);

    gchar *base = g_path_get_basename(nombre);
    gchar *titulo = g_strdup_printf("Métricas — %s", base);
    ventana_met = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ventana_met), titulo);
    gtk_window_set_transient_for(GTK_WINDOW(ventana_met), padre);
    gtk_window_set_default_size(GTK_WINDOW(ventana_met), 900, 660);
    g_signal_connect(ventana_met, "destroy", G_CALLBACK(al_destruir_metricas), NULL);
    g_free(titulo);
    g_free(base);

    GtkWidget *columna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(columna), 10);
    gtk_container_add(GTK_CONTAINER(ventana_met), columna);
    gtk_box_pack_start(GTK_BOX(columna), parrafo(
        "Se compila el código del editor y se ejecuta en el <b>simulador VLIW</b> "
        "(<tt>src/Simulador_VLIW/</tt>) hasta que <tt>main</tt> termina. El recolector de "
        "métricas del simulador cuenta cada ciclo. Pasa el mouse sobre una barra para ver su valor."),
        FALSE, FALSE, 0);

    const MetMedicion *m = &datos_met->actual;
    const char *problema = sin_metricas(m);
    if (problema != NULL) {
        gchar *marcado = g_markup_printf_escaped("<b>%s</b> %s", problema, m->motivo ? m->motivo : "");
        gtk_box_pack_start(GTK_BOX(columna), parrafo(marcado), FALSE, FALSE, 0);
        g_free(marcado);
        gtk_widget_show_all(ventana_met);
        return;
    }
    if (m->estado == MET_SIN_TERMINAR) {
        gchar *marcado = g_markup_printf_escaped(
            "<b>Atención:</b> %s. Las métricas son solo de esos ciclos.", m->motivo ? m->motivo : "");
        gtk_box_pack_start(GTK_BOX(columna), parrafo(marcado), FALSE, FALSE, 0);
        g_free(marcado);
    }

    gtk_box_pack_start(GTK_BOX(columna), fila_principal(m), FALSE, FALSE, 4);

    /* Pestañas. */
    armar_slots(&datos_met->slots, &m->sim);
    armar_bundles(&datos_met->bundles, &m->sim);
    Grafica *ejecucion[2] = {&datos_met->slots, &datos_met->bundles};
    GtkWidget *pestanas = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), columna_graficas(ejecucion, 2),
                             gtk_label_new("Ejecución"));
    if (datos_met->hay_dce) {
        Grafica *dce[1] = {&datos_met->dce};
        gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), columna_graficas(dce, 1),
                                 gtk_label_new("Antes y después del DCE"));
    } else {
        gchar *escapada = g_markup_escape_text(datos_met->nota_dce, -1);  /* Puede traer '<'. */
        GtkWidget *nota = parrafo(escapada);
        g_free(escapada);
        gtk_widget_set_valign(nota, GTK_ALIGN_START);
        gtk_widget_set_margin_top(nota, 12);
        gtk_widget_set_margin_start(nota, 12);
        gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), nota, gtk_label_new("Antes y después del DCE"));
    }
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas),
                             pestana_tabla(m, datos_met->hay_dce ? &datos_met->sin_muerto : NULL),
                             gtk_label_new("Tabla"));
    gtk_box_pack_start(GTK_BOX(columna), pestanas, TRUE, TRUE, 0);
    gtk_widget_show_all(ventana_met);
}
