/*
 * BAL IDE: la ventana del editor (GTK 3).
 *
 * Este archivo solo se encarga de la parte visual: dibujar el editor,
 * reaccionar a las teclas y mostrar resultados. Toda la lógica está en:
 *   - ide_nucleo.c   -> sugerencias, autocierre y delimitadores.
 *   - ide_analisis.c -> revisar el texto con el compilador.
 *   - ide_arreglos.c -> proponer y aplicar correcciones automáticas.
 *   - clones.c       -> buscar código copiado (clones tipo I y II).
 *
 * Qué hace el IDE:
 *   1. Sugerencias: al escribir una palabra aparece una lista para completarla.
 *      Solo salen las palabras que la gramática permite en ese lugar: se
 *      consulta la tabla LL(1) con el código que hay antes del cursor.
 *      Flechas para elegir, Enter o Tab para aceptar, Esc para cerrar.
 *      Ctrl+Espacio la abre a mano.
 *   2. Autocierre: al escribir ( [ { " $ se agrega el cierre solo. Si se
 *      escribe el cierre y ya estaba ahí, solo se avanza el cursor.
 *      Enter mantiene la sangría y, entre { }, abre una línea nueva adentro.
 *   3. Errores: medio segundo después de dejar de escribir, se revisa el
 *      código. Los errores se subrayan en rojo; al pasar el mouse encima se
 *      ve la explicación. Abajo hay una lista: un clic lleva al error.
 *   4. Corrección automática: si el IDE sabe cómo arreglar un error, la
 *      lista muestra EL arreglo y un botón "Corregir". Para los errores del
 *      parser, el arreglo es el token que predice la tabla LL(1) (ver
 *      ide_arreglos.c y src/LL1/). Ctrl+. corrige el error de la línea del cursor.
 *   5. Clones: el botón "Buscar clones" compara el archivo abierto con los
 *      demás .bal de su carpeta. Los clones se pintan de amarillo y la
 *      pestaña "Clones" dice dónde está cada copia.
 *   6. Números de línea: a la izquierda del editor. El de la línea del cursor
 *      va en negrita y los de las líneas con errores van en rojo, para que
 *      coincidan con lo que dicen la lista de errores y la barra de estado.
 *   7. Tabla LL(1): el botón "Tabla LL(1)" abre una ventana con la tabla
 *      M[A, a], las producciones y PRIMERO/SIGUIENTE, que son lo que usan
 *      las sugerencias y las correcciones (ver ide_tabla_ll1.c).
 *   8. Código muerto: el botón "Código muerto" hace la eliminación de código
 *      muerto con variables vivas sobre el código del editor (ide_dce.c).
 *      Las líneas eliminadas completas se tachan; las que solo perdieron una
 *      parte (por ejemplo, se fue la copia del resultado pero la llamada se
 *      queda) se marcan en gris. La pestaña "Código muerto" lista cada línea,
 *      y "Ver IR y variables vivas" abre el detalle (ide_dce_ventana.c).
 *      "Aplicar al código" cambia el código por el código sin el código
 *      muerto (ide_dce_codigo.c): el backend genera el ensamblador desde el
 *      código, así que el ensamblador y el binario salen de él.
 *   9. Ensamblador: el botón "Ensamblador" genera el ensamblador VLIW del
 *      código del editor, el mismo de "balc -s", y lo muestra con su
 *      codificación binaria (ide_ensamblador.c e ide_ensamblador_ventana.c).
 *  10. Colores: el código se colorea como en VS Code (tema Dark+): palabras
 *      de control, tipos, funciones, variables, números, textos, comentarios
 *      y pares de paréntesis por nivel (ide_colores.c).
 *
 * Atajos: Ctrl+N nuevo, Ctrl+O abrir, Ctrl+S guardar, Ctrl+. corregir.
 * Uso: ./balc-ide [archivo.bal]
 */
#include <gtk/gtk.h>
#include <string.h>

#include "clones.h"
#include "ide_analisis.h"
#include "ide_arreglos.h"
#include "ide_colores.h"
#include "ide_dce.h"
#include "ide_dce_ventana.h"
#include "ide_ensamblador.h"
#include "ide_ensamblador_ventana.h"
#include "ide_nucleo.h"
#include "ide_tabla_ll1.h"

/* Milisegundos que se espera, después de la última tecla, para revisar el código. */
#define ESPERA_ANALISIS_MS 500

/* Máximo de sugerencias que se muestran a la vez. */
#define MAX_SUGERENCIAS 12

/* Espacio (en píxeles) a cada lado de los números de línea. */
#define MARGEN_NUMEROS 8

/* Todo lo que la ventana necesita recordar. */
typedef struct {
    GtkWidget *ventana;
    GtkTextView *vista;            /* El editor de texto. */
    GtkTextBuffer *buffer;         /* El texto que se está editando. */
    GtkTextTag *marca_error;       /* Estilo del subrayado rojo. */
    GtkListBox *lista_errores;     /* Lista de errores de abajo. */
    GtkTextTag *marca_clon;        /* Estilo del fondo amarillo de los clones. */
    GtkListBox *lista_clones;      /* Lista de clones (pestaña "Clones"). */
    GtkLabel *resumen_clones;      /* "2 clones en este archivo...". */
    GtkTextTag *marca_color[IDE_COLOR_CANTIDAD];  /* Colores del código (ide_colores.c). */
    GtkTextTag *marca_muerta;      /* Línea eliminada completa: tachada y en gris. */
    GtkTextTag *marca_parcial;     /* Línea a la que se le quitó una parte: fondo gris. */
    GtkListBox *lista_dce;         /* Lista de la pestaña "Código muerto". */
    GtkLabel *resumen_dce;         /* "5 de 29 instrucciones eliminadas...". */
    GtkWidget *boton_detalle_dce;  /* "Ver IR y variables vivas". */
    GtkWidget *boton_aplicar_dce;  /* "Aplicar al código": el código sin el código muerto. */
    GtkNotebook *pestanas;         /* Pestañas de abajo: Errores, Clones y Código muerto. */
    GtkLabel *barra_estado;        /* "Línea X, columna Y · N errores". */
    GtkWidget *ventana_sugerencias;   /* Cuadro flotante con las sugerencias. */
    GtkListBox *lista_sugerencias;

    char *ruta;                 /* Archivo abierto; NULL si todavía no se guardó. */
    gboolean modificado;        /* TRUE si hay cambios sin guardar. */
    guint temporizador;         /* Cuenta regresiva para revisar el código. */
    DiagLista errores;          /* Errores de la última revisión. */
    IdeArreglo *arreglos;       /* Un arreglo por error (insertar == NULL: no tiene). */
    gboolean revision_vigente;  /* FALSE si el texto cambió después de revisarlo. */
    int largo_prefijo;          /* Letras ya escritas de la palabra que se sugiere. */
    int digitos_numeros;        /* Cifras que caben en la columna de números de línea. */
    IdeDce dce;                 /* Último resultado del botón "Código muerto". */
    gboolean dce_mostrado;      /* TRUE si ese resultado está marcado en el editor. */
} Ide;

/* ================================================================== */
/* Utilidades pequeñas                                                 */
/* ================================================================== */

/* El nombre con que se reporta el archivo actual en los errores. */
static const char *nombre_actual(const Ide *ide)
{
    return (ide->ruta != NULL) ? ide->ruta : IDE_SIN_NOMBRE;
}

/* Devuelve todo el texto del editor (liberar con g_free). */
static gchar *texto_del_editor(Ide *ide)
{
    GtkTextIter inicio, fin;
    gtk_text_buffer_get_bounds(ide->buffer, &inicio, &fin);
    return gtk_text_buffer_get_text(ide->buffer, &inicio, &fin, TRUE);
}

/* TRUE si el carácter puede ser parte de una palabra (letra, dígito o _). */
static gboolean es_de_palabra(gunichar c)
{
    return g_unichar_isalnum(c) || c == '_';
}

/* Nombre de la fase con tildes, para mostrarlo a la persona. */
static const char *fase_para_mostrar(DiagFase fase)
{
    switch (fase) {
        case DIAG_LEXICO: return "léxico";
        case DIAG_SINTACTICO: return "sintáctico";
        case DIAG_SEMANTICO: return "semántico";
        case DIAG_GENERAL: return "general";
    }
    return "general";
}

/* Borra todas las filas de una lista. */
static void vaciar_lista(GtkListBox *lista)
{
    GList *hijos = gtk_container_get_children(GTK_CONTAINER(lista));
    for (GList *h = hijos; h != NULL; h = h->next) {
        gtk_widget_destroy(GTK_WIDGET(h->data));
    }
    g_list_free(hijos);
}

/* Muestra un mensaje de error en un cuadro de diálogo. */
static void mostrar_aviso(Ide *ide, const char *mensaje)
{
    GtkWidget *dialogo = gtk_message_dialog_new(GTK_WINDOW(ide->ventana),
                                                GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_CLOSE, "%s", mensaje);
    gtk_dialog_run(GTK_DIALOG(dialogo));
    gtk_widget_destroy(dialogo);
}

/* ================================================================== */
/* Título y barra de estado                                            */
/* ================================================================== */

/* Título: "* factorial.bal — BAL IDE" (el * indica cambios sin guardar). */
static void actualizar_titulo(Ide *ide)
{
    gchar *base = g_path_get_basename(nombre_actual(ide));
    gchar *titulo = g_strdup_printf("%s%s — BAL IDE", ide->modificado ? "* " : "", base);
    gtk_window_set_title(GTK_WINDOW(ide->ventana), titulo);
    g_free(titulo);
    g_free(base);
}

/* Barra de abajo: posición del cursor y cantidad de errores. */
static void actualizar_estado(Ide *ide)
{
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(ide->buffer, &cursor,
                                     gtk_text_buffer_get_insert(ide->buffer));
    int linea = gtk_text_iter_get_line(&cursor) + 1;
    int columna = gtk_text_iter_get_line_offset(&cursor) + 1;

    gchar *texto;
    if (ide->errores.cantidad == 0) {
        texto = g_strdup_printf("Línea %d, columna %d · sin errores", linea, columna);
    } else {
        texto = g_strdup_printf("Línea %d, columna %d · %zu error%s", linea, columna,
                                ide->errores.cantidad,
                                ide->errores.cantidad == 1 ? "" : "es");
    }
    gtk_label_set_text(ide->barra_estado, texto);
    g_free(texto);
}

/* ================================================================== */
/* Números de línea                                                    */
/* ================================================================== */

/*
 * Cómo funciona: un GtkTextView puede tener una franja aparte a cada lado
 * del texto (una "ventana de borde"). Se usa la de la izquierda para los
 * números. GTK no escribe nada en ella; cada vez que el editor se redibuja
 * (al escribir, al mover el cursor o al desplazarse) se llama a
 * al_dibujar_numeros, que escribe el número de cada línea visible a la
 * misma altura que su texto.
 */

/*
 * Ajusta el ancho de la franja a la cantidad de cifras de la última línea
 * (mínimo 2). Así un archivo de 1000 líneas no corta el "1000". Solo
 * cambia el ancho cuando cambia la cantidad de cifras.
 */
static void ajustar_margen_numeros(Ide *ide)
{
    int lineas = gtk_text_buffer_get_line_count(ide->buffer);
    int digitos = 2;
    for (int n = lineas; n >= 100; n /= 10) digitos++;
    if (digitos == ide->digitos_numeros) return;
    ide->digitos_numeros = digitos;

    /* Se mide un número de ejemplo ("888...") con la letra del editor. */
    char muestra[16];
    memset(muestra, '8', (size_t)digitos);
    muestra[digitos] = '\0';
    PangoLayout *medida = gtk_widget_create_pango_layout(GTK_WIDGET(ide->vista), muestra);
    int ancho = 0;
    pango_layout_get_pixel_size(medida, &ancho, NULL);
    g_object_unref(medida);

    gtk_text_view_set_border_window_size(ide->vista, GTK_TEXT_WINDOW_LEFT,
                                         ancho + 2 * MARGEN_NUMEROS);
}

/* TRUE si la revisión encontró un error en esa línea (empieza en 1) de este archivo. */
static gboolean linea_con_error(const Ide *ide, size_t linea)
{
    for (size_t i = 0; i < ide->errores.cantidad; ++i) {
        const Diagnostico *d = &ide->errores.elementos[i];
        if (d->linea == linea && strcmp(d->archivo, nombre_actual(ide)) == 0) return TRUE;
    }
    return FALSE;
}

/*
 * Escribe los números de las líneas visibles en la franja izquierda.
 * GTK llama a "draw" una vez por cada parte del editor (el texto y cada
 * franja); solo se dibuja cuando le toca a la franja izquierda.
 */
static gboolean al_dibujar_numeros(GtkWidget *widget, cairo_t *cr, gpointer datos)
{
    Ide *ide = datos;
    GtkTextView *vista = GTK_TEXT_VIEW(widget);
    GdkWindow *franja = gtk_text_view_get_window(vista, GTK_TEXT_WINDOW_LEFT);
    if (franja == NULL || !gtk_cairo_should_draw_window(cr, franja)) return FALSE;

    cairo_save(cr);
    gtk_cairo_transform_to_window(cr, widget, franja);  /* (0, 0) = esquina de la franja. */
    int ancho = gdk_window_get_width(franja);
    int alto = gdk_window_get_height(franja);

    /* Se usa el color de la letra del tema, así se ve bien en claro y en oscuro. */
    GdkRGBA letra;
    gtk_style_context_get_color(gtk_widget_get_style_context(widget),
                                gtk_widget_get_state_flags(widget), &letra);

    /* Fondo un poco más oscuro que el texto y una raya que separa la franja. */
    cairo_set_source_rgba(cr, letra.red, letra.green, letra.blue, 0.05);
    cairo_rectangle(cr, 0, 0, ancho, alto);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, letra.red, letra.green, letra.blue, 0.20);
    cairo_rectangle(cr, ancho - 1, 0, 1, alto);
    cairo_fill(cr);

    /* Línea del cursor (empieza en 0, como las de GTK). */
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(ide->buffer, &cursor,
                                     gtk_text_buffer_get_insert(ide->buffer));
    int linea_cursor = gtk_text_iter_get_line(&cursor);

    /* Parte del texto que se ve ahora, en coordenadas del texto completo. */
    GdkRectangle visible;
    gtk_text_view_get_visible_rect(vista, &visible);

    /* Se empieza en la primera línea visible y se baja hasta salir de la vista. */
    GtkTextIter linea;
    gtk_text_view_get_line_at_y(vista, &linea, visible.y, NULL);
    PangoLayout *numero = gtk_widget_create_pango_layout(widget, NULL);
    GdkRGBA rojo = {0.96, 0.30, 0.30, 1.0};  /* Claro: se ve sobre el fondo oscuro. */

    for (;;) {
        /* Altura de la línea en el texto completo -> altura dentro de la franja. */
        int y_texto, alto_linea, y_franja;
        gtk_text_view_get_line_yrange(vista, &linea, &y_texto, &alto_linea);
        if (y_texto > visible.y + visible.height) break;
        gtk_text_view_buffer_to_window_coords(vista, GTK_TEXT_WINDOW_LEFT, 0, y_texto,
                                              NULL, &y_franja);

        int indice = gtk_text_iter_get_line(&linea);
        char texto[16];
        g_snprintf(texto, sizeof texto, "%d", indice + 1);  /* Se muestra desde 1. */
        pango_layout_set_text(numero, texto, -1);

        /* La línea del cursor en negrita. */
        PangoAttrList *atributos = pango_attr_list_new();
        if (indice == linea_cursor) {
            pango_attr_list_insert(atributos, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        }
        pango_layout_set_attributes(numero, atributos);
        pango_attr_list_unref(atributos);

        /* Rojo si tiene error; color normal si es la del cursor; tenue las demás. */
        if (linea_con_error(ide, (size_t)indice + 1)) {
            gdk_cairo_set_source_rgba(cr, &rojo);
        } else {
            double opacidad = (indice == linea_cursor) ? 1.0 : 0.45;
            cairo_set_source_rgba(cr, letra.red, letra.green, letra.blue, opacidad);
        }

        /* Alineado a la derecha y centrado en la altura de la línea. */
        int ancho_numero, alto_numero;
        pango_layout_get_pixel_size(numero, &ancho_numero, &alto_numero);
        cairo_move_to(cr, ancho - MARGEN_NUMEROS - ancho_numero,
                      y_franja + (alto_linea - alto_numero) / 2);
        pango_cairo_show_layout(cr, numero);

        /* Pasar a la siguiente. No se usa lo que devuelve forward_line porque
         * da FALSE al llegar a una última línea vacía, y esa también se numera. */
        gtk_text_iter_forward_line(&linea);
        if (gtk_text_iter_get_line(&linea) == indice) break;  /* Era la última línea. */
    }

    g_object_unref(numero);
    cairo_restore(cr);
    return FALSE;  /* FALSE: GTK sigue dibujando lo demás normalmente. */
}

/* ================================================================== */
/* Revisión de errores                                                 */
/* ================================================================== */

/*
 * Pone "posicion" en la línea y columna indicadas (empiezan en 1).
 * Si la posición se sale del texto, se ajusta al final de esa línea.
 * Devuelve FALSE si el error no tiene posición (línea 0).
 */
static gboolean ir_a_posicion(GtkTextBuffer *buffer, size_t linea, size_t columna,
                              GtkTextIter *posicion)
{
    if (linea == 0) return FALSE;

    int total_lineas = gtk_text_buffer_get_line_count(buffer);
    if ((int)linea > total_lineas) linea = (size_t)total_lineas;
    gtk_text_buffer_get_iter_at_line(buffer, posicion, (int)linea - 1);

    /* Cuántos caracteres tiene la línea (sin contar el salto de línea). */
    GtkTextIter fin_linea = *posicion;
    if (!gtk_text_iter_ends_line(&fin_linea)) gtk_text_iter_forward_to_line_end(&fin_linea);
    int largo_linea = gtk_text_iter_get_line_offset(&fin_linea);

    int desplazamiento = (columna > 0) ? (int)columna - 1 : 0;
    if (desplazamiento > largo_linea) desplazamiento = largo_linea;
    gtk_text_iter_set_line_offset(posicion, desplazamiento);
    return TRUE;
}

/*
 * Subraya en rojo el lugar de un error: la palabra completa si el error
 * empieza en una palabra, o un solo carácter si es un símbolo.
 */
static void subrayar_error(Ide *ide, const Diagnostico *d)
{
    GtkTextIter inicio, fin;
    if (!ir_a_posicion(ide->buffer, d->linea, d->columna, &inicio)) return;

    fin = inicio;
    if (es_de_palabra(gtk_text_iter_get_char(&fin))) {
        while (es_de_palabra(gtk_text_iter_get_char(&fin))) gtk_text_iter_forward_char(&fin);
    } else if (!gtk_text_iter_ends_line(&fin)) {
        gtk_text_iter_forward_char(&fin);
    }

    /* Si el error está justo al final de la línea, subrayar el carácter anterior. */
    if (gtk_text_iter_equal(&inicio, &fin)) {
        if (!gtk_text_iter_backward_char(&inicio)) return;  /* El archivo está vacío. */
    }
    gtk_text_buffer_apply_tag(ide->buffer, ide->marca_error, &inicio, &fin);
}

/* ¿Tiene arreglo el error número "indice"? */
static gboolean tiene_arreglo(const Ide *ide, size_t indice)
{
    return ide->arreglos != NULL && indice < ide->errores.cantidad &&
           ide->arreglos[indice].insertar != NULL;
}

/* Libera los arreglos de la revisión anterior. */
static void liberar_arreglos(Ide *ide)
{
    for (size_t i = 0; ide->arreglos != NULL && i < ide->errores.cantidad; ++i) {
        ide_liberar_arreglo(&ide->arreglos[i]);
    }
    g_free(ide->arreglos);
    ide->arreglos = NULL;
}

static void al_pulsar_corregir(GtkWidget *boton, gpointer datos);
static void limpiar_dce(Ide *ide, const char *mensaje);
static void al_pulsar_ensamblador(GtkWidget *boton, gpointer datos);
static void aplicar_colores(Ide *ide);

/* Lo que dice la pestaña "Código muerto" antes de usar el botón. */
#define DCE_SIN_ANALIZAR "Pulsa \"Código muerto\" para eliminar el código muerto de este archivo " \
                         "con el análisis de variables vivas."
#define DCE_CODIGO_CAMBIO "El código cambió: pulsa \"Código muerto\" para analizarlo otra vez."


/* Agrega una fila a la lista de errores de abajo. */
static void agregar_fila_error(Ide *ide, size_t indice)
{
    const Diagnostico *d = &ide->errores.elementos[indice];
    gchar *texto;

    if (strcmp(d->archivo, nombre_actual(ide)) != 0) {
        /* El error está en otro archivo (un módulo importado con bring). */
        texto = g_strdup_printf("%s, línea %zu, columna %zu · %s: %s", d->archivo,
                                d->linea, d->columna, fase_para_mostrar(d->fase), d->mensaje);
    } else if (d->linea == 0) {
        texto = g_strdup_printf("%s: %s", fase_para_mostrar(d->fase), d->mensaje);
    } else {
        texto = g_strdup_printf("Línea %zu, columna %zu · %s: %s", d->linea, d->columna,
                                fase_para_mostrar(d->fase), d->mensaje);
    }

    GtkWidget *etiqueta = gtk_label_new(texto);
    gtk_label_set_xalign(GTK_LABEL(etiqueta), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(etiqueta), FALSE);
    gtk_widget_set_margin_start(etiqueta, 6);
    gtk_widget_set_margin_top(etiqueta, 2);
    gtk_widget_set_margin_bottom(etiqueta, 2);
    g_free(texto);

    /*
     * La fila es una caja horizontal:
     *   [ mensaje del error                      ] [Corregir]
     *   [ Arreglo: agregar ';' al final de ...   ]
     * La segunda línea y el botón solo aparecen si el error tiene arreglo.
     */
    GtkWidget *textos = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(textos), etiqueta, FALSE, FALSE, 0);

    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(caja), textos, TRUE, TRUE, 0);

    if (tiene_arreglo(ide, indice)) {
        gchar *propuesta = g_markup_printf_escaped("<small>Arreglo: %s</small>",
                                                   ide->arreglos[indice].descripcion);
        GtkWidget *detalle = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(detalle), propuesta);
        gtk_label_set_xalign(GTK_LABEL(detalle), 0.0f);
        gtk_widget_set_margin_start(detalle, 18);
        gtk_widget_set_margin_bottom(detalle, 2);
        gtk_box_pack_start(GTK_BOX(textos), detalle, FALSE, FALSE, 0);
        g_free(propuesta);

        GtkWidget *boton = gtk_button_new_with_label("Corregir");
        gtk_widget_set_valign(boton, GTK_ALIGN_CENTER);
        gtk_widget_set_margin_end(boton, 6);
        gtk_widget_set_tooltip_text(boton, ide->arreglos[indice].descripcion);
        /* El botón recuerda qué error corrige. */
        g_object_set_data(G_OBJECT(boton), "indice", GSIZE_TO_POINTER(indice));
        g_signal_connect(boton, "clicked", G_CALLBACK(al_pulsar_corregir), ide);
        gtk_box_pack_end(GTK_BOX(caja), boton, FALSE, FALSE, 0);
    }

    GtkWidget *fila = gtk_list_box_row_new();
    gtk_container_add(GTK_CONTAINER(fila), caja);
    /* Guardamos en la fila qué error es, para saber a dónde ir con el clic. */
    g_object_set_data(G_OBJECT(fila), "indice", GSIZE_TO_POINTER(indice));
    gtk_list_box_insert(ide->lista_errores, fila, -1);
    gtk_widget_show_all(fila);
}

/*
 * Revisa el código. Se llama sola medio segundo después de la última tecla.
 * Devuelve G_SOURCE_REMOVE para que GTK no la vuelva a llamar por su cuenta.
 */
static gboolean revisar_codigo(gpointer datos)
{
    Ide *ide = datos;
    ide->temporizador = 0;

    /* Paso 1: pedirle al compilador los errores del texto actual. */
    liberar_arreglos(ide);
    diag_liberar(&ide->errores);
    gchar *texto = texto_del_editor(ide);
    size_t largo = strlen(texto);
    int resultado = ide_analizar_texto(texto, largo, ide->ruta, &ide->errores);

    /* Paso 1b: pedir un arreglo para cada error de este archivo. Se calculan
     * ahora, con el mismo texto que se analizó, para que las posiciones
     * coincidan. Los que no tienen arreglo quedan en cero (insertar == NULL). */
    if (resultado == 0 && ide->errores.cantidad > 0) {
        ide->arreglos = g_new0(IdeArreglo, ide->errores.cantidad);
        for (size_t i = 0; i < ide->errores.cantidad; ++i) {
            const Diagnostico *d = &ide->errores.elementos[i];
            if (strcmp(d->archivo, nombre_actual(ide)) != 0) continue;  /* Otro archivo. */
            if (ide_proponer_arreglo(texto, largo, d, &ide->arreglos[i]) != 1) {
                ide_liberar_arreglo(&ide->arreglos[i]);  /* Sin arreglo (o sin memoria). */
            }
        }
    }
    g_free(texto);
    ide->revision_vigente = TRUE;

    /* Paso 2: quitar los subrayados y la lista de la revisión anterior. */
    GtkTextIter inicio, fin;
    gtk_text_buffer_get_bounds(ide->buffer, &inicio, &fin);
    gtk_text_buffer_remove_tag(ide->buffer, ide->marca_error, &inicio, &fin);
    vaciar_lista(ide->lista_errores);
    gtk_widget_queue_draw(GTK_WIDGET(ide->vista));  /* Repintar los números en rojo. */

    if (resultado != 0) {
        gtk_label_set_text(ide->barra_estado,
                           "No se pudo revisar el código (¿la carpeta permite escribir?)");
        return G_SOURCE_REMOVE;
    }

    /* Paso 3: subrayar y listar los errores nuevos. */
    for (size_t i = 0; i < ide->errores.cantidad; ++i) {
        const Diagnostico *d = &ide->errores.elementos[i];
        if (strcmp(d->archivo, nombre_actual(ide)) == 0) subrayar_error(ide, d);
        agregar_fila_error(ide, i);
    }
    actualizar_estado(ide);
    return G_SOURCE_REMOVE;
}

/* Reinicia la cuenta regresiva: el código se revisa cuando se deja de escribir. */
static void programar_revision(Ide *ide)
{
    if (ide->temporizador != 0) g_source_remove(ide->temporizador);
    ide->temporizador = g_timeout_add(ESPERA_ANALISIS_MS, revisar_codigo, ide);
}

/* Clic en un error de la lista: mover el cursor a ese error. */
static void al_elegir_error(GtkListBox *lista, GtkListBoxRow *fila, gpointer datos)
{
    (void)lista;
    Ide *ide = datos;
    size_t indice = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(fila), "indice"));
    if (indice >= ide->errores.cantidad) return;

    const Diagnostico *d = &ide->errores.elementos[indice];
    GtkTextIter posicion;
    if (strcmp(d->archivo, nombre_actual(ide)) != 0) return;  /* Está en otro archivo. */
    if (!ir_a_posicion(ide->buffer, d->linea, d->columna, &posicion)) return;

    gtk_text_buffer_place_cursor(ide->buffer, &posicion);
    gtk_text_view_scroll_to_iter(ide->vista, &posicion, 0.2, FALSE, 0, 0);
    gtk_widget_grab_focus(GTK_WIDGET(ide->vista));
}

/*
 * Aplica el arreglo del error número "indice" en el editor.
 *
 * El arreglo dice "en esta línea y columna, borrar N caracteres y escribir
 * este texto". Se hace como una sola acción, así Ctrl+Z la deshace entera.
 */
static void aplicar_arreglo(Ide *ide, size_t indice)
{
    /* Si se escribió algo después de la última revisión, las posiciones
     * pueden haber cambiado: revisar otra vez y que la persona vuelva a elegir. */
    if (!ide->revision_vigente) {
        if (ide->temporizador != 0) g_source_remove(ide->temporizador);
        revisar_codigo(ide);
        gtk_label_set_text(ide->barra_estado,
                           "El código cambió: la lista de errores se actualizó, intenta de nuevo");
        return;
    }
    if (!tiene_arreglo(ide, indice)) return;

    /* Copiar el arreglo: al cambiar el texto se vuelve a revisar y la lista
     * de arreglos se reemplaza. */
    IdeArreglo arreglo = ide->arreglos[indice];
    gchar *insertar = g_strdup(arreglo.insertar);

    GtkTextIter inicio, fin;
    if (!ir_a_posicion(ide->buffer, arreglo.linea, arreglo.columna, &inicio)) {
        g_free(insertar);
        return;
    }
    fin = inicio;
    gtk_text_iter_forward_chars(&fin, (gint)arreglo.borrar);

    gtk_text_buffer_begin_user_action(ide->buffer);
    gtk_text_buffer_delete(ide->buffer, &inicio, &fin);   /* "inicio" sigue siendo válido. */
    gtk_text_buffer_insert(ide->buffer, &inicio, insertar, -1);  /* Queda después de lo insertado. */
    gtk_text_buffer_end_user_action(ide->buffer);

    /* Dejar el cursor justo después del arreglo. */
    gtk_text_buffer_place_cursor(ide->buffer, &inicio);
    gtk_text_view_scroll_to_iter(ide->vista, &inicio, 0.2, FALSE, 0, 0);
    gtk_widget_grab_focus(GTK_WIDGET(ide->vista));
    g_free(insertar);
}

/* Botón "Corregir" de una fila de la lista. */
static void al_pulsar_corregir(GtkWidget *boton, gpointer datos)
{
    size_t indice = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(boton), "indice"));
    aplicar_arreglo(datos, indice);
}

/*
 * Ctrl+. : corregir el error de la línea del cursor.
 * Sirve tanto la línea donde se marcó el error como la línea donde va el
 * arreglo (para el ';' que falta, el parser marca la línea de abajo).
 */
static void corregir_en_cursor(Ide *ide)
{
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(ide->buffer, &cursor, gtk_text_buffer_get_insert(ide->buffer));
    size_t linea = (size_t)gtk_text_iter_get_line(&cursor) + 1;

    for (size_t i = 0; i < ide->errores.cantidad; ++i) {
        if (!tiene_arreglo(ide, i)) continue;
        if (ide->errores.elementos[i].linea == linea || ide->arreglos[i].linea == linea) {
            aplicar_arreglo(ide, i);
            return;
        }
    }
    gtk_label_set_text(ide->barra_estado, "No hay ninguna corrección para esta línea");
}

/*
 * Mouse encima del texto: si está sobre un subrayado, mostrar la
 * explicación de los errores de esa línea.
 */
static gboolean al_pedir_ayuda(GtkWidget *widget, gint x, gint y, gboolean con_teclado,
                               GtkTooltip *ayuda, gpointer datos)
{
    (void)widget;
    Ide *ide = datos;
    if (con_teclado) return FALSE;

    /* Convertir la posición del mouse en una posición dentro del texto. */
    gint bx, by;
    GtkTextIter posicion;
    gtk_text_view_window_to_buffer_coords(ide->vista, GTK_TEXT_WINDOW_WIDGET, x, y, &bx, &by);
    if (!gtk_text_view_get_iter_at_location(ide->vista, &posicion, bx, by)) return FALSE;
    if (!gtk_text_iter_has_tag(&posicion, ide->marca_error)) return FALSE;

    /* Juntar los mensajes de los errores de esa línea. */
    size_t linea = (size_t)gtk_text_iter_get_line(&posicion) + 1;
    GString *mensajes = g_string_new(NULL);
    for (size_t i = 0; i < ide->errores.cantidad; ++i) {
        const Diagnostico *d = &ide->errores.elementos[i];
        if (d->linea != linea || strcmp(d->archivo, nombre_actual(ide)) != 0) continue;
        if (mensajes->len > 0) g_string_append_c(mensajes, '\n');
        g_string_append_printf(mensajes, "Error %s: %s", fase_para_mostrar(d->fase), d->mensaje);
        if (tiene_arreglo(ide, i)) {
            g_string_append_printf(mensajes, "\n    Arreglo (Ctrl+.): %s", ide->arreglos[i].descripcion);
        }
    }

    gboolean hay_ayuda = mensajes->len > 0;
    if (hay_ayuda) gtk_tooltip_set_text(ayuda, mensajes->str);
    g_string_free(mensajes, TRUE);
    return hay_ayuda;
}

/* ================================================================== */
/* Sugerencias                                                         */
/* ================================================================== */

static void ocultar_sugerencias(Ide *ide)
{
    if (gtk_widget_get_visible(ide->ventana_sugerencias)) {
        gtk_popover_popdown(GTK_POPOVER(ide->ventana_sugerencias));
    }
}

/*
 * Busca la palabra que se está escribiendo (lo que hay justo antes del
 * cursor) y, si hay sugerencias para ella, las muestra junto al cursor.
 */
static void actualizar_sugerencias(Ide *ide)
{
    /* Paso 1: retroceder desde el cursor mientras haya letras, dígitos o _. */
    GtkTextIter cursor, inicio;
    gtk_text_buffer_get_iter_at_mark(ide->buffer, &cursor,
                                     gtk_text_buffer_get_insert(ide->buffer));
    inicio = cursor;
    int letras = 0;
    while (!gtk_text_iter_starts_line(&inicio)) {
        GtkTextIter anterior = inicio;
        gtk_text_iter_backward_char(&anterior);
        if (!es_de_palabra(gtk_text_iter_get_char(&anterior))) break;
        inicio = anterior;
        letras++;
    }

    /* Sin palabra, o si empieza con un número, no hay nada que sugerir. */
    if (letras == 0 || g_unichar_isdigit(gtk_text_iter_get_char(&inicio))) {
        ocultar_sugerencias(ide);
        return;
    }

    /* Paso 2: pedirle las sugerencias al núcleo. */
    /* Se usa la predicción LL(1): el núcleo analiza el código que hay ANTES
     * de la palabra y solo sugiere lo que la tabla permite ahí. Para eso
     * necesita saber dónde empieza la palabra, en bytes. */
    gchar *prefijo = gtk_text_buffer_get_text(ide->buffer, &inicio, &cursor, FALSE);
    gchar *texto = texto_del_editor(ide);
    GtkTextIter comienzo;
    gtk_text_buffer_get_start_iter(ide->buffer, &comienzo);
    gchar *antes = gtk_text_buffer_get_text(ide->buffer, &comienzo, &inicio, TRUE);
    IdeSugerencias sugerencias = {0};
    int resultado = ide_sugerencias_predictivas(texto, strlen(texto), strlen(antes),
                                                prefijo, &sugerencias);
    g_free(antes);
    g_free(texto);
    g_free(prefijo);

    if (resultado != 0 || sugerencias.cantidad == 0) {
        ide_liberar_sugerencias(&sugerencias);
        ocultar_sugerencias(ide);
        return;
    }
    ide->largo_prefijo = letras;

    /* Paso 3: llenar la lista flotante (como mucho MAX_SUGERENCIAS filas). */
    vaciar_lista(ide->lista_sugerencias);
    for (size_t i = 0; i < sugerencias.cantidad && i < MAX_SUGERENCIAS; ++i) {
        const IdeSugerencia *s = &sugerencias.elementos[i];
        gchar *marcado = g_markup_printf_escaped("<b>%s</b>   <small>%s</small>",
                                                 s->texto, s->descripcion);
        GtkWidget *etiqueta = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(etiqueta), marcado);
        gtk_label_set_xalign(GTK_LABEL(etiqueta), 0.0f);
        g_free(marcado);

        GtkWidget *fila = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(fila), etiqueta);
        /* La fila guarda su propia copia de la palabra, y la libera al destruirse. */
        g_object_set_data_full(G_OBJECT(fila), "palabra", g_strdup(s->texto), g_free);
        gtk_list_box_insert(ide->lista_sugerencias, fila, -1);
    }
    ide_liberar_sugerencias(&sugerencias);
    gtk_widget_show_all(GTK_WIDGET(ide->lista_sugerencias));
    gtk_list_box_select_row(ide->lista_sugerencias,
                            gtk_list_box_get_row_at_index(ide->lista_sugerencias, 0));

    /* Paso 4: mostrar el cuadro justo debajo del cursor. */
    GdkRectangle lugar;
    gtk_text_view_get_iter_location(ide->vista, &cursor, &lugar);
    gtk_text_view_buffer_to_window_coords(ide->vista, GTK_TEXT_WINDOW_WIDGET,
                                          lugar.x, lugar.y, &lugar.x, &lugar.y);
    gtk_popover_set_pointing_to(GTK_POPOVER(ide->ventana_sugerencias), &lugar);
    gtk_popover_popup(GTK_POPOVER(ide->ventana_sugerencias));
}

/* Cambia la sugerencia elegida hacia arriba (-1) o hacia abajo (+1). */
static void mover_seleccion(Ide *ide, int paso)
{
    GtkListBoxRow *actual = gtk_list_box_get_selected_row(ide->lista_sugerencias);
    int indice = (actual != NULL) ? gtk_list_box_row_get_index(actual) + paso : 0;
    GtkListBoxRow *nueva = gtk_list_box_get_row_at_index(ide->lista_sugerencias, indice);
    if (nueva != NULL) gtk_list_box_select_row(ide->lista_sugerencias, nueva);
}

/* Reemplaza la palabra a medio escribir por la sugerencia elegida. */
static void aceptar_sugerencia(Ide *ide)
{
    GtkListBoxRow *fila = gtk_list_box_get_selected_row(ide->lista_sugerencias);
    if (fila == NULL) return;
    const char *palabra = g_object_get_data(G_OBJECT(fila), "palabra");

    /* Borrar lo que ya se había escrito de la palabra y poner la palabra completa. */
    GtkTextIter cursor, inicio;
    gtk_text_buffer_get_iter_at_mark(ide->buffer, &cursor,
                                     gtk_text_buffer_get_insert(ide->buffer));
    inicio = cursor;
    gtk_text_iter_backward_chars(&inicio, ide->largo_prefijo);

    gtk_text_buffer_begin_user_action(ide->buffer);
    gtk_text_buffer_delete(ide->buffer, &inicio, &cursor);
    gtk_text_buffer_insert(ide->buffer, &inicio, palabra, -1);
    gtk_text_buffer_end_user_action(ide->buffer);

    ocultar_sugerencias(ide);
    gtk_widget_grab_focus(GTK_WIDGET(ide->vista));
}

/* Clic sobre una sugerencia: aceptarla. */
static void al_elegir_sugerencia(GtkListBox *lista, GtkListBoxRow *fila, gpointer datos)
{
    gtk_list_box_select_row(lista, fila);
    aceptar_sugerencia(datos);
}

/* ================================================================== */
/* Teclado: autocierre, sangría y control de las sugerencias           */
/* ================================================================== */

/* Devuelve los espacios y tabuladores con que empieza la línea del cursor. */
static gchar *sangria_de_la_linea(GtkTextBuffer *buffer, const GtkTextIter *cursor)
{
    GtkTextIter inicio = *cursor, fin;
    gtk_text_iter_set_line_offset(&inicio, 0);
    fin = inicio;
    while (gtk_text_iter_get_char(&fin) == ' ' || gtk_text_iter_get_char(&fin) == '\t') {
        gtk_text_iter_forward_char(&fin);
    }
    return gtk_text_buffer_get_text(buffer, &inicio, &fin, FALSE);
}

/*
 * Enter: la línea nueva conserva la sangría de la actual.
 * Si el cursor está justo después de '{', se agrega un nivel más (4 espacios),
 * y si además hay un '}' justo después, este baja a su propia línea:
 *
 *     whale (i < n) {|}      ->      whale (i < n) {
 *                                        |
 *                                    }
 */
static void insertar_salto_con_sangria(Ide *ide)
{
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(ide->buffer, &cursor,
                                     gtk_text_buffer_get_insert(ide->buffer));
    gchar *sangria = sangria_de_la_linea(ide->buffer, &cursor);

    GtkTextIter anterior = cursor;
    gboolean despues_de_llave = gtk_text_iter_backward_char(&anterior) &&
                                gtk_text_iter_get_char(&anterior) == '{';
    gboolean antes_de_cierre = gtk_text_iter_get_char(&cursor) == '}';

    gtk_text_buffer_begin_user_action(ide->buffer);
    gchar *primera = g_strdup_printf("\n%s%s", sangria, despues_de_llave ? "    " : "");
    gtk_text_buffer_insert_at_cursor(ide->buffer, primera, -1);

    if (despues_de_llave && antes_de_cierre) {
        /* Insertar el salto del '}' y devolver el cursor a la línea del medio. */
        gchar *segunda = g_strdup_printf("\n%s", sangria);
        gtk_text_buffer_insert_at_cursor(ide->buffer, segunda, -1);
        GtkTextIter medio;
        gtk_text_buffer_get_iter_at_mark(ide->buffer, &medio,
                                         gtk_text_buffer_get_insert(ide->buffer));
        gtk_text_iter_backward_chars(&medio, (gint)g_utf8_strlen(segunda, -1));
        gtk_text_buffer_place_cursor(ide->buffer, &medio);
        g_free(segunda);
    }
    gtk_text_buffer_end_user_action(ide->buffer);

    g_free(primera);
    g_free(sangria);
}

/*
 * Se llama ANTES de que el editor procese cada tecla.
 * Devuelve TRUE si ya nos encargamos de la tecla (el editor la ignora),
 * o FALSE para que el editor haga lo normal.
 */
static gboolean al_presionar_tecla(GtkWidget *widget, GdkEventKey *evento, gpointer datos)
{
    (void)widget;
    Ide *ide = datos;
    guint tecla = evento->keyval;
    gboolean con_ctrl = (evento->state & GDK_CONTROL_MASK) != 0;

    /* 1. Si las sugerencias están abiertas, algunas teclas las controlan. */
    if (gtk_widget_get_visible(ide->ventana_sugerencias)) {
        switch (tecla) {
            case GDK_KEY_Down: mover_seleccion(ide, +1); return TRUE;
            case GDK_KEY_Up: mover_seleccion(ide, -1); return TRUE;
            case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_Tab:
                aceptar_sugerencia(ide);
                return TRUE;
            case GDK_KEY_Escape: ocultar_sugerencias(ide); return TRUE;
            default: break;
        }
    }

    /* 2. Ctrl+Espacio abre las sugerencias a mano. */
    if (con_ctrl && tecla == GDK_KEY_space) {
        actualizar_sugerencias(ide);
        return TRUE;
    }
    if (con_ctrl || (evento->state & GDK_MOD1_MASK)) return FALSE;  /* Otros atajos. */

    /* 3. Enter: salto de línea con sangría. */
    if (tecla == GDK_KEY_Return || tecla == GDK_KEY_KP_Enter) {
        insertar_salto_con_sangria(ide);
        return TRUE;
    }

    gunichar c = gdk_keyval_to_unicode(tecla);
    if (c == 0 || c > 127) return FALSE;  /* Solo nos interesan caracteres simples. */

    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(ide->buffer, &cursor,
                                     gtk_text_buffer_get_insert(ide->buffer));
    gboolean hay_seleccion = gtk_text_buffer_get_has_selection(ide->buffer);

    /* 4. Si se escribe un cierre que ya está justo adelante, solo avanzar. */
    gboolean es_cierre = (c == ')' || c == ']' || c == '}' || c == '"' || c == '$');
    if (es_cierre && !hay_seleccion && gtk_text_iter_get_char(&cursor) == c) {
        gtk_text_iter_forward_char(&cursor);
        gtk_text_buffer_place_cursor(ide->buffer, &cursor);
        return TRUE;
    }

    /* 5. Autocierre: escribir la apertura y su cierre, con el cursor en medio. */
    char cierre = ide_cierre_de((char)c);
    if (cierre != '\0') {
        char par[3] = {(char)c, cierre, '\0'};
        gtk_text_buffer_begin_user_action(ide->buffer);
        gtk_text_buffer_delete_selection(ide->buffer, TRUE, TRUE);
        gtk_text_buffer_insert_at_cursor(ide->buffer, par, 2);
        gtk_text_buffer_get_iter_at_mark(ide->buffer, &cursor,
                                         gtk_text_buffer_get_insert(ide->buffer));
        gtk_text_iter_backward_char(&cursor);
        gtk_text_buffer_place_cursor(ide->buffer, &cursor);
        gtk_text_buffer_end_user_action(ide->buffer);
        return TRUE;
    }
    return FALSE;
}

/*
 * Se llama DESPUÉS de soltar cada tecla, cuando el texto ya cambió.
 * Al escribir letras o borrar se actualizan las sugerencias; con
 * cualquier otra tecla (espacio, punto y coma, flechas...) se cierran.
 */
static gboolean al_soltar_tecla(GtkWidget *widget, GdkEventKey *evento, gpointer datos)
{
    (void)widget;
    Ide *ide = datos;
    guint tecla = evento->keyval;

    /* Estas teclas ya las manejó al_presionar_tecla, o no cambian nada. */
    if (evento->is_modifier || tecla == GDK_KEY_Up || tecla == GDK_KEY_Down ||
        tecla == GDK_KEY_Return || tecla == GDK_KEY_KP_Enter || tecla == GDK_KEY_Tab ||
        tecla == GDK_KEY_Escape || (evento->state & GDK_CONTROL_MASK)) {
        return FALSE;
    }

    gunichar c = gdk_keyval_to_unicode(tecla);
    if (tecla == GDK_KEY_BackSpace || (c != 0 && es_de_palabra(c))) {
        actualizar_sugerencias(ide);
    } else {
        ocultar_sugerencias(ide);
    }
    return FALSE;
}

/* Un clic en el texto mueve el cursor: las sugerencias ya no aplican. */
static gboolean al_hacer_clic(GtkWidget *widget, GdkEventButton *evento, gpointer datos)
{
    (void)widget;
    (void)evento;
    ocultar_sugerencias(datos);
    return FALSE;
}

/* El texto cambió: marcar cambios sin guardar y programar la revisión. */
static void al_cambiar_texto(GtkTextBuffer *buffer, gpointer datos)
{
    (void)buffer;
    Ide *ide = datos;
    ide->revision_vigente = FALSE;  /* Las posiciones de los arreglos ya no sirven. */
    if (!ide->modificado) {
        ide->modificado = TRUE;
        actualizar_titulo(ide);
    }
    ajustar_margen_numeros(ide);  /* Por si ahora hacen falta más cifras. */
    aplicar_colores(ide);         /* Volver a colorear el código. */
    /* Las líneas marcadas por el DCE ya pueden no coincidir: se borran. */
    if (ide->dce_mostrado) limpiar_dce(ide, DCE_CODIGO_CAMBIO);
    programar_revision(ide);
}

/* El cursor se movió: actualizar "Línea X, columna Y" y el número en negrita. */
static void al_mover_marca(GtkTextBuffer *buffer, GtkTextIter *posicion, GtkTextMark *marca,
                           gpointer datos)
{
    (void)posicion;
    Ide *ide = datos;
    if (marca != gtk_text_buffer_get_insert(buffer)) return;
    actualizar_estado(ide);
    gtk_widget_queue_draw(GTK_WIDGET(ide->vista));
}

/* ================================================================== */
/* Clones                                                              */
/* ================================================================== */

/* Página de cada pestaña de abajo. */
enum { PESTANA_ERRORES = 0, PESTANA_CLONES = 1, PESTANA_DCE = 2 };

/* TRUE si el nombre es un programa BAL visible ("x.bal", pero no ".ide_x.bal"). */
static gboolean es_archivo_bal(const char *nombre)
{
    size_t largo = strlen(nombre);
    return nombre[0] != '.' && largo > 4 && strcmp(nombre + largo - 4, ".bal") == 0;
}

/* Quita los clones marcados: el fondo amarillo y la lista. */
static void limpiar_clones(Ide *ide)
{
    GtkTextIter inicio, fin;
    gtk_text_buffer_get_bounds(ide->buffer, &inicio, &fin);
    gtk_text_buffer_remove_tag(ide->buffer, ide->marca_clon, &inicio, &fin);
    vaciar_lista(ide->lista_clones);
    gtk_label_set_text(ide->resumen_clones,
                       "Pulsa \"Buscar clones\" para comparar este archivo con los demás .bal de su carpeta.");
}

/* Pinta de amarillo un fragmento del archivo abierto. */
static void marcar_clon(Ide *ide, const ClonFragmento *f)
{
    GtkTextIter inicio, fin;
    if (!ir_a_posicion(ide->buffer, f->linea, f->columna, &inicio)) return;
    if (!ir_a_posicion(ide->buffer, f->linea_fin, f->columna_fin, &fin)) return;
    gtk_text_buffer_apply_tag(ide->buffer, ide->marca_clon, &inicio, &fin);
}

/* Agrega "llamadas.bal, líneas 2 a 4" (o "este archivo, ...") a un texto. */
static void describir_fragmento(GString *texto, const ClonFragmento *f, gboolean es_este)
{
    if (es_este) {
        g_string_append(texto, "este archivo, ");
    } else {
        gchar *base = g_path_get_basename(f->archivo);
        g_string_append_printf(texto, "%s, ", base);
        g_free(base);
    }
    if (f->linea == f->linea_fin) g_string_append_printf(texto, "línea %zu", f->linea);
    else g_string_append_printf(texto, "líneas %zu a %zu", f->linea, f->linea_fin);
}

/*
 * Agrega una fila a la lista de clones, para el fragmento número "propio"
 * del clon (uno que está en el archivo abierto):
 *
 *   Líneas 2 a 4 · tipo I (idéntico) · funcion
 *      También en: llamadas.bal, líneas 2 a 4
 */
static void agregar_fila_clon(Ide *ide, const Clon *c, size_t propio)
{
    const ClonFragmento *f = &c->fragmentos[propio];

    GString *titulo = g_string_new(NULL);
    if (f->linea == f->linea_fin) g_string_append_printf(titulo, "Línea %zu", f->linea);
    else g_string_append_printf(titulo, "Líneas %zu a %zu", f->linea, f->linea_fin);
    g_string_append_printf(titulo, " · tipo %s · %s",
                           c->tipo == CLON_TIPO_1 ? "I (idéntico)" : "II (otros nombres o valores)",
                           c->que);

    /* Dónde están las otras copias. */
    GString *copias = g_string_new("También en: ");
    gboolean primera = TRUE;
    for (size_t j = 0; j < c->cantidad; ++j) {
        if (j == propio) continue;
        if (!primera) g_string_append(copias, "; ");
        describir_fragmento(copias, &c->fragmentos[j],
                            strcmp(c->fragmentos[j].archivo, nombre_actual(ide)) == 0);
        primera = FALSE;
    }

    GtkWidget *etiqueta = gtk_label_new(titulo->str);
    gtk_label_set_xalign(GTK_LABEL(etiqueta), 0.0f);
    gtk_widget_set_margin_start(etiqueta, 6);
    gtk_widget_set_margin_top(etiqueta, 2);

    gchar *marcado = g_markup_printf_escaped("<small>%s</small>", copias->str);
    GtkWidget *detalle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(detalle), marcado);
    gtk_label_set_xalign(GTK_LABEL(detalle), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(detalle), TRUE);
    gtk_widget_set_margin_start(detalle, 18);
    gtk_widget_set_margin_bottom(detalle, 2);
    g_free(marcado);

    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(caja), etiqueta, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caja), detalle, FALSE, FALSE, 0);

    GtkWidget *fila = gtk_list_box_row_new();
    gtk_container_add(GTK_CONTAINER(fila), caja);
    /* La fila recuerda dónde empieza el fragmento, para ir ahí con un clic. */
    g_object_set_data(G_OBJECT(fila), "linea", GSIZE_TO_POINTER(f->linea));
    g_object_set_data(G_OBJECT(fila), "columna", GSIZE_TO_POINTER(f->columna));
    gtk_list_box_insert(ide->lista_clones, fila, -1);
    gtk_widget_show_all(fila);

    g_string_free(titulo, TRUE);
    g_string_free(copias, TRUE);
}

/* Clic en un clon de la lista: llevar el cursor a ese fragmento. */
static void al_elegir_clon(GtkListBox *lista, GtkListBoxRow *fila, gpointer datos)
{
    (void)lista;
    Ide *ide = datos;
    size_t linea = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(fila), "linea"));
    size_t columna = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(fila), "columna"));

    GtkTextIter posicion;
    if (!ir_a_posicion(ide->buffer, linea, columna, &posicion)) return;
    gtk_text_buffer_place_cursor(ide->buffer, &posicion);
    gtk_text_view_scroll_to_iter(ide->vista, &posicion, 0.2, FALSE, 0, 0);
    gtk_widget_grab_focus(GTK_WIDGET(ide->vista));
}

/*
 * Botón "Buscar clones".
 *
 *   1. Junta los archivos: el texto del editor (aunque no esté guardado) y
 *      los demás .bal de la misma carpeta, leídos del disco.
 *   2. Busca los clones con clones_buscar (clones.c), igual que balc --clones.
 *   3. Muestra solo los clones que tocan el archivo abierto: los pinta de
 *      amarillo y los pone en la pestaña "Clones".
 */
static void al_pulsar_buscar_clones(GtkWidget *boton, gpointer datos)
{
    (void)boton;
    Ide *ide = datos;
    limpiar_clones(ide);

    /* Un archivo que nunca se guardó no tiene carpeta: no hay con qué
     * compararlo. Se avisa en vez de buscar en una carpeta cualquiera. */
    if (ide->ruta == NULL) {
        gtk_label_set_text(ide->resumen_clones,
                           "Este archivo no está guardado. Guárdalo (o abre uno) en la carpeta "
                           "de los otros .bal para compararlo con ellos.");
        gtk_notebook_set_current_page(ide->pestanas, PESTANA_CLONES);
        return;
    }

    /* Paso 1: el archivo abierto va primero, con el texto del editor. */
    GArray *fuentes = g_array_new(FALSE, TRUE, sizeof(ClonFuente));
    GPtrArray *por_liberar = g_ptr_array_new_with_free_func(g_free);
    gchar *texto = texto_del_editor(ide);
    g_ptr_array_add(por_liberar, texto);
    ClonFuente actual = {nombre_actual(ide), texto, strlen(texto)};
    g_array_append_val(fuentes, actual);

    /* ...y después los demás .bal de su carpeta (sin repetir el abierto). */
    gchar *carpeta = g_path_get_dirname(ide->ruta);
    gchar *base_actual = g_path_get_basename(ide->ruta);
    GDir *dir = g_dir_open(carpeta, 0, NULL);
    const char *nombre;
    while (dir != NULL && (nombre = g_dir_read_name(dir)) != NULL) {
        if (!es_archivo_bal(nombre)) continue;
        if (strcmp(nombre, base_actual) == 0) continue;

        gchar *ruta = g_build_filename(carpeta, nombre, NULL);
        gchar *contenido = NULL;
        gsize largo = 0;
        if (!g_file_get_contents(ruta, &contenido, &largo, NULL)) {
            g_free(ruta);  /* No se pudo leer: se salta. */
            continue;
        }
        g_ptr_array_add(por_liberar, ruta);
        g_ptr_array_add(por_liberar, contenido);
        ClonFuente otra = {ruta, contenido, largo};
        g_array_append_val(fuentes, otra);
    }
    if (dir != NULL) g_dir_close(dir);

    /* Paso 2: buscar. */
    ClonLista lista = {0};
    int resultado = clones_buscar((const ClonFuente *)(void *)fuentes->data, fuentes->len,
                                  CLONES_MINIMO_TOKENS, &lista);

    /* Paso 3: mostrar los clones del archivo abierto. */
    size_t mostrados = 0;
    gboolean abierto_omitido = FALSE;
    for (size_t i = 0; i < lista.omitidos; ++i) {
        if (strcmp(lista.nombres_omitidos[i], nombre_actual(ide)) == 0) abierto_omitido = TRUE;
    }
    for (size_t i = 0; resultado == 0 && i < lista.cantidad; ++i) {
        const Clon *c = &lista.elementos[i];
        gboolean cuenta = FALSE;
        for (size_t j = 0; j < c->cantidad; ++j) {
            if (strcmp(c->fragmentos[j].archivo, nombre_actual(ide)) != 0) continue;
            marcar_clon(ide, &c->fragmentos[j]);
            agregar_fila_clon(ide, c, j);
            cuenta = TRUE;
        }
        if (cuenta) mostrados++;
    }

    /* El resumen de arriba de la lista. */
    gchar *resumen;
    if (resultado != 0) {
        resumen = g_strdup("No se pudieron buscar los clones (falta de memoria).");
    } else if (abierto_omitido) {
        resumen = g_strdup("Este archivo tiene errores de sintaxis: corrígelos para buscar clones.");
    } else if (fuentes->len == 1) {
        /* Solo está el archivo abierto: se dice en qué carpeta se buscó. */
        resumen = g_strdup_printf("%zu clon%s en este archivo · no hay otros .bal en la carpeta %s",
                                  mostrados, mostrados == 1 ? "" : "es", carpeta);
    } else {
        resumen = g_strdup_printf("%zu clon%s en este archivo · se compararon %u archivo%s .bal de la carpeta",
                                  mostrados, mostrados == 1 ? "" : "es",
                                  fuentes->len, fuentes->len == 1 ? "" : "s");
    }
    gtk_label_set_text(ide->resumen_clones, resumen);
    gtk_notebook_set_current_page(ide->pestanas, PESTANA_CLONES);

    g_free(resumen);
    clones_liberar(&lista);
    g_free(base_actual);
    g_free(carpeta);
    g_array_free(fuentes, TRUE);
    g_ptr_array_free(por_liberar, TRUE);
}

/* ================================================================== */
/* Colores del código (como VS Code, tema Dark+)                       */
/* ================================================================== */

/*
 * Colores del tema Dark+ de VS Code, uno por categoría de ide_colores.h,
 * en el mismo orden que IdeColor.
 */
static const char *const COLORES_DARK_PLUS[IDE_COLOR_CANTIDAD] = {
    [IDE_COLOR_CONTROL] = "#C586C0",     /* como return, if, while */
    [IDE_COLOR_PALABRA] = "#569CD6",     /* como static, const */
    [IDE_COLOR_TIPO] = "#4EC9B0",        /* como AstNodo */
    [IDE_COLOR_FUNCION] = "#DCDCAA",     /* como ast_crear */
    [IDE_COLOR_VARIABLE] = "#9CDCFE",    /* como a, tipo, texto */
    [IDE_COLOR_NUMERO] = "#B5CEA8",
    [IDE_COLOR_TEXTO] = "#CE9178",
    [IDE_COLOR_COMENTARIO] = "#6A9955",
    [IDE_COLOR_PAR_1] = "#FFD700",       /* pares por nivel: dorado, */
    [IDE_COLOR_PAR_2] = "#DA70D6",       /* rosado */
    [IDE_COLOR_PAR_3] = "#179FFF",       /* y azul, y se repite */
};

/* El fondo, la letra y la selección del editor, como en VS Code. */
static const char *const CSS_EDITOR =
    "textview, textview text, textview border {"
    "  background-color: #1E1E1E; color: #D4D4D4; caret-color: #AEAFAD; }"
    "textview text selection { background-color: #264F78; color: #D4D4D4; }";

/*
 * El panel de abajo (Errores, Clones y Código muerto) con los colores del
 * panel "Problemas" de VS Code. Se aplica a toda la pantalla, pero los
 * selectores empiezan con #panel_inferior o #editor (los nombres que se les
 * dan en construir_ventana), así que la barra de botones no cambia.
 */
static const char *const CSS_PANEL =
    /* Fondo y letra del panel, sus listas y sus filas. */
    "#panel_inferior, #panel_inferior stack, #panel_inferior scrolledwindow,"
    "#panel_inferior viewport, #panel_inferior list, #panel_inferior row {"
    "  background-color: #1E1E1E; color: #CCCCCC; }"
    "#panel_inferior label { color: #CCCCCC; }"
    /* Las pestañas: la elegida más clara, con la raya azul de VS Code. */
    "#panel_inferior header { background-color: #252526; border-color: #3C3C3C; }"
    "#panel_inferior tab { color: #969696; }"
    "#panel_inferior tab label { color: #969696; }"
    "#panel_inferior tab:checked label { color: #E7E7E7; }"
    "#panel_inferior tab:checked { box-shadow: inset 0 -2px #0078D4; }"
    /* El fondo de las pestañas en TODOS sus estados: si no, GTK pone su fondo
     * claro al pasar el mouse sobre la pestaña elegida y el texto no se ve. */
    "#panel_inferior tab, #panel_inferior tab:checked {"
    "  background-color: #252526; background-image: none; }"
    "#panel_inferior tab:hover, #panel_inferior tab:checked:hover {"
    "  background-color: #2A2D2E; background-image: none; }"
    "#panel_inferior tab:hover label { color: #E7E7E7; }"
    /* Las filas al pasar el mouse y la elegida. */
    "#panel_inferior row:hover { background-color: #2A2D2E; }"
    "#panel_inferior row:selected { background-color: #04395E; }"
    /* Botones (Corregir, Ver IR y variables vivas): el azul de VS Code. */
    "#panel_inferior button { background-image: none; background-color: #0E639C;"
    "  border-color: #0E639C; box-shadow: none; text-shadow: none; }"
    "#panel_inferior button label { color: #FFFFFF; }"
    "#panel_inferior button:hover { background-color: #1177BB; }"
    "#panel_inferior button:disabled { background-color: #3C3C3C; border-color: #3C3C3C; }"
    "#panel_inferior button:disabled label { color: #8C8C8C; }"
    /* Barras de desplazamiento oscuras, en el panel y en el editor. */
    "#panel_inferior scrollbar, #editor scrollbar {"
    "  background-color: #1E1E1E; border-color: #1E1E1E; }"
    "#panel_inferior scrollbar slider, #editor scrollbar slider {"
    "  background-color: #424242; border-color: #424242; }"
    "#panel_inferior scrollbar slider:hover, #editor scrollbar slider:hover {"
    "  background-color: #4F4F4F; }";

/* Vuelve a colorear todo el código del editor (se llama en cada cambio). */
static void aplicar_colores(Ide *ide)
{
    GtkTextIter inicio, fin;
    gtk_text_buffer_get_bounds(ide->buffer, &inicio, &fin);
    for (int i = 0; i < IDE_COLOR_CANTIDAD; ++i) {
        gtk_text_buffer_remove_tag(ide->buffer, ide->marca_color[i], &inicio, &fin);
    }

    gchar *texto = texto_del_editor(ide);
    IdeColores colores;
    if (ide_colorear(texto, strlen(texto), &colores) == 0) {
        for (size_t i = 0; i < colores.cantidad; ++i) {
            const IdeTramoColor *t = &colores.elementos[i];
            GtkTextIter desde, hasta;
            if (!ir_a_posicion(ide->buffer, t->linea, t->columna, &desde)) continue;
            if (!ir_a_posicion(ide->buffer, t->linea_fin, t->columna_fin, &hasta)) {
                gtk_text_buffer_get_end_iter(ide->buffer, &hasta);
            }
            gtk_text_buffer_apply_tag(ide->buffer, ide->marca_color[t->color], &desde, &hasta);
        }
        ide_liberar_colores(&colores);
    }
    g_free(texto);
}

/* ================================================================== */
/* Código muerto (DCE con variables vivas)                             */
/* ================================================================== */

/* Quita las marcas y la lista del DCE, y deja "mensaje" en el resumen. */
static void limpiar_dce(Ide *ide, const char *mensaje)
{
    GtkTextIter inicio, fin;
    gtk_text_buffer_get_bounds(ide->buffer, &inicio, &fin);
    gtk_text_buffer_remove_tag(ide->buffer, ide->marca_muerta, &inicio, &fin);
    gtk_text_buffer_remove_tag(ide->buffer, ide->marca_parcial, &inicio, &fin);
    vaciar_lista(ide->lista_dce);
    gtk_label_set_text(ide->resumen_dce, mensaje);
    gtk_widget_set_sensitive(ide->boton_detalle_dce, FALSE);
    gtk_widget_set_sensitive(ide->boton_aplicar_dce, FALSE);
    ide_dce_liberar(&ide->dce);
    ide->dce_mostrado = FALSE;
}

/*
 * Marca el CÓDIGO de una línea del editor: sin la sangría del inicio y sin
 * el comentario del final (%% ...), que no es código y no se elimina.
 *     "    b * declare_int : a pitagoras 2;   %% muerta"
 *          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^  solo esto
 */
static void marcar_linea(Ide *ide, size_t linea, GtkTextTag *marca)
{
    if (linea == 0 || linea > (size_t)gtk_text_buffer_get_line_count(ide->buffer)) return;
    GtkTextIter inicio, fin;
    gtk_text_buffer_get_iter_at_line(ide->buffer, &inicio, (gint)linea - 1);
    while (!gtk_text_iter_ends_line(&inicio) && g_unichar_isspace(gtk_text_iter_get_char(&inicio))) {
        gtk_text_iter_forward_char(&inicio);
    }

    /* Avanzar hasta el final de la línea o hasta un "%%" que no esté
     * dentro de un texto entre comillas. */
    fin = inicio;
    gboolean en_texto = FALSE;
    while (!gtk_text_iter_ends_line(&fin)) {
        gunichar c = gtk_text_iter_get_char(&fin);
        if (c == '"') en_texto = !en_texto;
        if (c == '%' && !en_texto) {
            GtkTextIter siguiente = fin;
            gtk_text_iter_forward_char(&siguiente);
            if (gtk_text_iter_get_char(&siguiente) == '%') break;
        }
        gtk_text_iter_forward_char(&fin);
    }
    /* Sin los espacios que quedan antes del comentario. */
    while (gtk_text_iter_compare(&fin, &inicio) > 0) {
        GtkTextIter anterior = fin;
        gtk_text_iter_backward_char(&anterior);
        if (!g_unichar_isspace(gtk_text_iter_get_char(&anterior))) break;
        fin = anterior;
    }
    gtk_text_buffer_apply_tag(ide->buffer, marca, &inicio, &fin);
}

/*
 * Agrega una fila a la lista del DCE:
 *
 *   Línea 12 · eliminada completa
 *      $t5 = a pitagoras 2; b = $t5
 *   Línea 13 · se eliminó una parte (1 de 4 instrucciones)
 *      c = $t6 · lo demás se conserva (tiene efectos o se usa después)
 */
static void agregar_fila_dce(Ide *ide, const IdeDceLinea *l)
{
    /* El tipo de código muerto: inalcanzable (tipo 2) o no usado (tipo 1). */
    gchar *titulo = (l->inalcanzable && l->completa)
        ? g_strdup_printf("Línea %zu · inalcanzable: nunca se ejecuta", l->linea)
        : l->inalcanzable
        ? g_strdup_printf("Línea %zu · una rama es inalcanzable; la sentencia se queda", l->linea)
        : l->completa
        ? g_strdup_printf("Línea %zu · eliminada completa", l->linea)
        : g_strdup_printf("Línea %zu · se eliminó una parte (%zu de %zu instrucciones)",
                          l->linea, l->eliminadas, l->total);
    gchar *marcado = l->completa
        ? g_markup_printf_escaped("<small><tt>%s</tt></small>", l->detalle)
        : l->inalcanzable
        ? g_markup_printf_escaped("<small><tt>%s</tt> · la sentencia sigue en el código "
                                  "(otra parte sí se ejecuta)</small>", l->detalle)
        : g_markup_printf_escaped("<small><tt>%s</tt> · lo demás se conserva "
                                  "(tiene efectos o se usa después)</small>", l->detalle);

    GtkWidget *etiqueta = gtk_label_new(titulo);
    gtk_label_set_xalign(GTK_LABEL(etiqueta), 0.0f);
    gtk_widget_set_margin_start(etiqueta, 6);
    gtk_widget_set_margin_top(etiqueta, 2);

    GtkWidget *detalle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(detalle), marcado);
    gtk_label_set_xalign(GTK_LABEL(detalle), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(detalle), TRUE);
    gtk_widget_set_margin_start(detalle, 18);
    gtk_widget_set_margin_bottom(detalle, 2);

    GtkWidget *caja = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(caja), etiqueta, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caja), detalle, FALSE, FALSE, 0);

    GtkWidget *fila = gtk_list_box_row_new();
    gtk_container_add(GTK_CONTAINER(fila), caja);
    /* Igual que en los clones: la fila recuerda su línea para ir ahí con
     * un clic (al_elegir_clon sirve para las dos listas). */
    g_object_set_data(G_OBJECT(fila), "linea", GSIZE_TO_POINTER(l->linea));
    g_object_set_data(G_OBJECT(fila), "columna", GSIZE_TO_POINTER((gsize)1));
    gtk_list_box_insert(ide->lista_dce, fila, -1);
    gtk_widget_show_all(fila);

    g_free(titulo);
    g_free(marcado);
}

/*
 * Botón "Código muerto".
 *   1. Corre semántico -> IR -> variables vivas -> DCE sobre el texto del
 *      editor (ide_dce.c), aunque no esté guardado.
 *   2. Tacha las líneas eliminadas completas y pone en gris las que solo
 *      perdieron una parte.
 *   3. Llena la pestaña "Código muerto" con una fila por línea y el resumen.
 */
static void al_pulsar_dce(GtkWidget *boton, gpointer datos)
{
    (void)boton;
    Ide *ide = datos;
    limpiar_dce(ide, "");
    gtk_notebook_set_current_page(ide->pestanas, PESTANA_DCE);

    gchar *texto = texto_del_editor(ide);
    int estado = ide_dce_analizar(texto, strlen(texto), ide->ruta, &ide->dce);
    g_free(texto);

    if (estado != 0) {
        gtk_label_set_text(ide->resumen_dce, "No se pudo analizar (falta de memoria o "
                                             "no se pudo escribir el archivo temporal).");
        return;
    }
    if (ide->dce.estado == IDE_DCE_CON_ERRORES) {
        gtk_label_set_text(ide->resumen_dce, "Este archivo tiene errores: corrígelos "
                                             "(pestaña Errores) para eliminar el código muerto.");
        ide_dce_liberar(&ide->dce);
        return;
    }
    if (ide->dce.estado == IDE_DCE_SIN_IR) {
        gchar *mensaje = g_strdup_printf("La IR todavía no soporta este programa (%s), "
                                         "así que no se puede hacer el DCE.", ide->dce.motivo);
        gtk_label_set_text(ide->resumen_dce, mensaje);
        g_free(mensaje);
        ide_dce_liberar(&ide->dce);
        return;
    }

    /* Marcar las líneas y llenar la lista. */
    for (size_t i = 0; i < ide->dce.cantidad_lineas; ++i) {
        const IdeDceLinea *l = &ide->dce.lineas[i];
        marcar_linea(ide, l->linea, l->completa ? ide->marca_muerta : ide->marca_parcial);
        agregar_fila_dce(ide, l);
    }

    /* El resumen: cuánto se eliminó y qué se conservó. */
    const DceResultado *r = &ide->dce.resultado;
    GString *resumen = g_string_new(NULL);
    if (r->cantidad_eliminadas == 0) {
        g_string_append(resumen, "No hay código muerto: cada asignación se lee después "
                                 "o es parte del resultado final de main.");
    } else {
        g_string_append_printf(resumen, "%zu de %zu instrucciones de la IR eliminadas, en %zu pasada%s",
                               r->cantidad_eliminadas, r->instrucciones_antes, r->pasadas,
                               r->pasadas == 1 ? "" : "s");
        /* De qué tipo: no usadas (tipo 1) e inalcanzables (tipo 2). */
        if (r->inalcanzables > 0) {
            g_string_append_printf(resumen, " (%zu no usada%s, %zu inalcanzable%s)",
                                   r->cantidad_eliminadas - r->inalcanzables,
                                   r->cantidad_eliminadas - r->inalcanzables == 1 ? "" : "s",
                                   r->inalcanzables, r->inalcanzables == 1 ? "" : "s");
        }
    }
    if (r->llamadas_conservadas > 0) {
        g_string_append_printf(resumen, " · %zu llamada%s conservada%s por tener efectos",
                               r->llamadas_conservadas, r->llamadas_conservadas == 1 ? "" : "s",
                               r->llamadas_conservadas == 1 ? "" : "s");
    }
    if (r->riesgosas_conservadas > 0) {
        g_string_append_printf(resumen, " · %zu operaci%s conservada%s porque puede%s fallar",
                               r->riesgosas_conservadas,
                               r->riesgosas_conservadas == 1 ? "ón" : "ones",
                               r->riesgosas_conservadas == 1 ? "" : "s",
                               r->riesgosas_conservadas == 1 ? "" : "n");
    }
    /* Y qué pasa con el ensamblador si se aplica el código nuevo. */
    if (ide->dce.codigo_nuevo != NULL) {
        if (ide->dce.hay_ensamblador == 1) {
            g_string_append_printf(resumen, "\nAl aplicarlo al código, el ensamblador pasa de "
                                            "%zu a %zu bundles.",
                                   ide->dce.bundles_antes, ide->dce.bundles_despues);
        } else if (ide->dce.hay_ensamblador == 2) {
            g_string_append_printf(resumen, "\nEl código original no se puede compilar (%s); "
                                            "al aplicarle el DCE sí: %zu bundles.",
                                   ide->dce.motivo_ensamblador, ide->dce.bundles_despues);
        } else if (ide->dce.motivo_ensamblador != NULL) {
            g_string_append_printf(resumen, "\nSe puede aplicar al código, pero el ensamblador "
                                            "no se puede generar: %s.", ide->dce.motivo_ensamblador);
        }
    }
    gtk_label_set_text(ide->resumen_dce, resumen->str);
    g_string_free(resumen, TRUE);

    gtk_widget_set_sensitive(ide->boton_detalle_dce, TRUE);
    gtk_widget_set_sensitive(ide->boton_aplicar_dce, ide->dce.codigo_nuevo != NULL);
    ide->dce_mostrado = TRUE;
}

/*
 * Botón "Aplicar al código": cambia el código del editor por el código nuevo
 * sin el código muerto (ide_dce_codigo.c). El backend genera el ensamblador
 * desde el código fuente, así que desde aquí el ensamblador y el binario
 * salen sin el código muerto: si la ventana del ensamblador está abierta se
 * actualiza sola, y al guardar, balc genera el .bin nuevo.
 */
static void al_pulsar_aplicar_dce(GtkWidget *boton, gpointer datos)
{
    (void)boton;
    Ide *ide = datos;
    if (!ide->dce_mostrado || ide->dce.codigo_nuevo == NULL) return;

    /* Copiar lo necesario: al cambiar el texto, el resultado del DCE se libera. */
    gchar *nuevo = g_strdup(ide->dce.codigo_nuevo);
    size_t borradas = ide->dce.sentencias_borradas, cambiadas = ide->dce.sentencias_cambiadas;
    int hay_asm = ide->dce.hay_ensamblador;
    size_t antes = ide->dce.bundles_antes, despues = ide->dce.bundles_despues;

    /* Reemplazar el texto en una sola acción. */
    GtkTextIter inicio, fin;
    gtk_text_buffer_get_bounds(ide->buffer, &inicio, &fin);
    gtk_text_buffer_begin_user_action(ide->buffer);
    gtk_text_buffer_delete(ide->buffer, &inicio, &fin);
    gtk_text_buffer_insert(ide->buffer, &inicio, nuevo, -1);
    gtk_text_buffer_end_user_action(ide->buffer);
    gtk_text_buffer_get_start_iter(ide->buffer, &inicio);
    gtk_text_buffer_place_cursor(ide->buffer, &inicio);
    g_free(nuevo);

    /* Contar qué se hizo (el cambio de texto ya borró las marcas). */
    gchar *cuenta = g_strdup_printf("Se aplicó el DCE al código: %zu sentencia%s borrada%s y "
                                    "%zu reescrita%s.",
                                    borradas, borradas == 1 ? "" : "s", borradas == 1 ? "" : "s",
                                    cambiadas, cambiadas == 1 ? "" : "s");
    gchar *mensaje = (hay_asm == 1)
        ? g_strdup_printf("%s El ensamblador pasó de %zu a %zu bundles (botón Ensamblador). "
                          "Guarda el archivo para que balc genere el .bin nuevo.", cuenta, antes, despues)
        : (hay_asm == 2)
        ? g_strdup_printf("%s Ahora el código sí se puede compilar: %zu bundles (botón "
                          "Ensamblador). Guarda el archivo para que balc genere el .bin.", cuenta, despues)
        : g_strdup(cuenta);
    g_free(cuenta);
    gtk_label_set_text(ide->resumen_dce, mensaje);
    g_free(mensaje);

    /* Si la ventana del ensamblador está abierta, mostrar el del código nuevo. */
    if (ide_ensamblador_abierta()) al_pulsar_ensamblador(NULL, ide);
}

/* Botón "Ver IR y variables vivas": el detalle del último DCE. */
static void al_pulsar_detalle_dce(GtkWidget *boton, gpointer datos)
{
    (void)boton;
    Ide *ide = datos;
    if (ide->dce_mostrado) ide_mostrar_detalle_dce(GTK_WINDOW(ide->ventana), &ide->dce);
}

/* ================================================================== */
/* Archivos: nuevo, abrir y guardar                                    */
/* ================================================================== */

/* Pone un texto en el editor como si estuviera recién abierto (sin cambios). */
static void cargar_texto(Ide *ide, const char *texto, const char *ruta)
{
    g_free(ide->ruta);
    ide->ruta = (ruta != NULL) ? g_strdup(ruta) : NULL;

    gtk_text_buffer_set_text(ide->buffer, texto, -1);
    limpiar_clones(ide);  /* Los clones eran del archivo anterior. */
    limpiar_dce(ide, DCE_SIN_ANALIZAR);  /* Y el código muerto también. */
    GtkTextIter inicio;
    gtk_text_buffer_get_start_iter(ide->buffer, &inicio);
    gtk_text_buffer_place_cursor(ide->buffer, &inicio);

    ide->modificado = FALSE;  /* set_text lo marcó como modificado; no lo está. */
    actualizar_titulo(ide);
    programar_revision(ide);
}

/* Abre un archivo. Devuelve TRUE si se pudo. */
static gboolean abrir_archivo(Ide *ide, const char *ruta)
{
    gchar *contenido = NULL;
    gsize largo = 0;
    GError *error = NULL;

    if (!g_file_get_contents(ruta, &contenido, &largo, &error)) {
        gchar *mensaje = g_strdup_printf("No se pudo abrir %s:\n%s", ruta, error->message);
        mostrar_aviso(ide, mensaje);
        g_free(mensaje);
        g_error_free(error);
        return FALSE;
    }
    /* El editor solo acepta texto UTF-8 válido. */
    if (!g_utf8_validate(contenido, (gssize)largo, NULL)) {
        mostrar_aviso(ide, "El archivo no es texto UTF-8 válido.");
        g_free(contenido);
        return FALSE;
    }
    cargar_texto(ide, contenido, ruta);
    g_free(contenido);
    return TRUE;
}

/* Guarda el texto en "ruta". Devuelve TRUE si se pudo. */
static gboolean guardar_en(Ide *ide, const char *ruta)
{
    gchar *texto = texto_del_editor(ide);
    GError *error = NULL;
    gboolean ok = g_file_set_contents(ruta, texto, -1, &error);
    g_free(texto);

    if (!ok) {
        gchar *mensaje = g_strdup_printf("No se pudo guardar %s:\n%s", ruta, error->message);
        mostrar_aviso(ide, mensaje);
        g_free(mensaje);
        g_error_free(error);
        return FALSE;
    }
    if (ide->ruta != ruta) {
        g_free(ide->ruta);
        ide->ruta = g_strdup(ruta);
    }
    ide->modificado = FALSE;
    actualizar_titulo(ide);
    programar_revision(ide);  /* El nombre pudo cambiar: revisar otra vez. */
    return TRUE;
}

/*
 * Muestra el cuadro para elegir un archivo.
 * "para_guardar" decide si es el cuadro de abrir o el de guardar.
 * Devuelve la ruta elegida (liberar con g_free), o NULL si se canceló.
 */
static gchar *elegir_archivo(Ide *ide, gboolean para_guardar)
{
    GtkWidget *dialogo = gtk_file_chooser_dialog_new(
        para_guardar ? "Guardar archivo" : "Abrir archivo", GTK_WINDOW(ide->ventana),
        para_guardar ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancelar", GTK_RESPONSE_CANCEL,
        para_guardar ? "_Guardar" : "_Abrir", GTK_RESPONSE_ACCEPT, NULL);

    /* Mostrar solo los archivos .bal. */
    GtkFileFilter *filtro = gtk_file_filter_new();
    gtk_file_filter_set_name(filtro, "Programas BAL (*.bal)");
    gtk_file_filter_add_pattern(filtro, "*.bal");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialogo), filtro);

    if (para_guardar) {
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialogo), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialogo), "programa.bal");
    }

    gchar *ruta = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialogo)) == GTK_RESPONSE_ACCEPT) {
        ruta = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialogo));
    }
    gtk_widget_destroy(dialogo);
    return ruta;
}

/* Guardar: si el archivo no tiene nombre todavía, se pregunta dónde. */
static gboolean guardar(Ide *ide, gboolean preguntar_nombre)
{
    if (ide->ruta != NULL && !preguntar_nombre) return guardar_en(ide, ide->ruta);

    gchar *ruta = elegir_archivo(ide, TRUE);
    if (ruta == NULL) return FALSE;  /* Se canceló. */
    gboolean ok = guardar_en(ide, ruta);
    g_free(ruta);
    return ok;
}

/*
 * Antes de cerrar o reemplazar el archivo: si hay cambios sin guardar,
 * preguntar qué hacer. Devuelve TRUE si se puede continuar.
 */
static gboolean puede_descartar(Ide *ide)
{
    if (!ide->modificado) return TRUE;

    GtkWidget *dialogo = gtk_message_dialog_new(GTK_WINDOW(ide->ventana), GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                                "¿Guardar los cambios de %s?",
                                                nombre_actual(ide));
    gtk_dialog_add_buttons(GTK_DIALOG(dialogo), "_Descartar", GTK_RESPONSE_REJECT,
                           "_Cancelar", GTK_RESPONSE_CANCEL,
                           "_Guardar", GTK_RESPONSE_ACCEPT, NULL);
    int respuesta = gtk_dialog_run(GTK_DIALOG(dialogo));
    gtk_widget_destroy(dialogo);

    if (respuesta == GTK_RESPONSE_ACCEPT) return guardar(ide, FALSE);
    return respuesta == GTK_RESPONSE_REJECT;
}

/* Botones de la barra de arriba. */
static void al_pulsar_nuevo(GtkWidget *boton, gpointer datos)
{
    (void)boton;
    Ide *ide = datos;
    if (puede_descartar(ide)) cargar_texto(ide, "", NULL);
}

static void al_pulsar_abrir(GtkWidget *boton, gpointer datos)
{
    (void)boton;
    Ide *ide = datos;
    if (!puede_descartar(ide)) return;
    gchar *ruta = elegir_archivo(ide, FALSE);
    if (ruta != NULL) abrir_archivo(ide, ruta);
    g_free(ruta);
}

static void al_pulsar_guardar(GtkWidget *boton, gpointer datos)
{
    (void)boton;
    guardar(datos, FALSE);
}

static void al_pulsar_guardar_como(GtkWidget *boton, gpointer datos)
{
    (void)boton;
    guardar(datos, TRUE);
}

/* Atajos de teclado de la ventana: Ctrl+N, Ctrl+O, Ctrl+S y Ctrl+. (corregir). */
static gboolean al_presionar_atajo(GtkWidget *widget, GdkEventKey *evento, gpointer datos)
{
    (void)widget;
    if (!(evento->state & GDK_CONTROL_MASK)) return FALSE;
    switch (gdk_keyval_to_lower(evento->keyval)) {
        case GDK_KEY_n: al_pulsar_nuevo(NULL, datos); return TRUE;
        case GDK_KEY_o: al_pulsar_abrir(NULL, datos); return TRUE;
        case GDK_KEY_s: al_pulsar_guardar(NULL, datos); return TRUE;
        case GDK_KEY_period: corregir_en_cursor(datos); return TRUE;
        default: return FALSE;
    }
}

/* La persona quiere cerrar la ventana: preguntar si hay cambios sin guardar. */
static gboolean al_cerrar(GtkWidget *widget, GdkEvent *evento, gpointer datos)
{
    (void)widget;
    (void)evento;
    return !puede_descartar(datos);  /* TRUE = no cerrar todavía. */
}

/* La ventana se cerró: liberar todo y terminar el programa. */
static void al_destruir(GtkWidget *widget, gpointer datos)
{
    (void)widget;
    Ide *ide = datos;
    if (ide->temporizador != 0) g_source_remove(ide->temporizador);
    liberar_arreglos(ide);
    diag_liberar(&ide->errores);
    ide_dce_liberar(&ide->dce);
    g_free(ide->ruta);
    gtk_main_quit();
}

/* ================================================================== */
/* Construcción de la ventana                                          */
/* ================================================================== */

/*
 * Botón "Ensamblador": genera el ensamblador del código del editor (aunque
 * no esté guardado) con los mismos pasos que "balc -s" y lo muestra en una
 * ventana con su codificación binaria. Si el código tiene errores, o el
 * backend todavía no soporta algo, se avisa.
 */
static void al_pulsar_ensamblador(GtkWidget *boton, gpointer datos)
{
    (void)boton;
    Ide *ide = datos;
    gchar *texto = texto_del_editor(ide);
    IdeEnsamblador a;
    int estado = ide_ensamblador_generar(texto, strlen(texto), ide->ruta, &a);
    g_free(texto);

    if (estado != 0) {
        mostrar_aviso(ide, "No se pudo generar el ensamblador (falta de memoria o no se "
                           "pudo escribir el archivo temporal).");
        return;
    }
    if (a.estado == IDE_ASM_CON_ERRORES) {
        mostrar_aviso(ide, "Este archivo tiene errores: corrígelos (pestaña Errores) "
                           "para generar el ensamblador.");
    } else if (a.estado == IDE_ASM_SIN_BACKEND) {
        gchar *mensaje = g_strdup_printf("El código no tiene errores, pero el backend no "
                                         "pudo generar el ensamblador:\n%s", a.motivo);
        mostrar_aviso(ide, mensaje);
        g_free(mensaje);
    } else {
        ide_mostrar_ensamblador(GTK_WINDOW(ide->ventana), &a, nombre_actual(ide));
    }
    ide_ensamblador_liberar(&a);
}

/* Botón "Tabla LL(1)": abre la ventana con la tabla (ide_tabla_ll1.c). */
static void al_pulsar_tabla_ll1(GtkWidget *boton, gpointer datos)
{
    (void)boton;
    Ide *ide = datos;
    ide_mostrar_tabla_ll1(GTK_WINDOW(ide->ventana));
}

/* Crea un botón para la barra de arriba y lo conecta a su función. */
static void agregar_boton(GtkWidget *barra, const char *texto, GCallback funcion, Ide *ide)
{
    GtkWidget *boton = gtk_button_new_with_mnemonic(texto);
    g_signal_connect(boton, "clicked", funcion, ide);
    gtk_box_pack_start(GTK_BOX(barra), boton, FALSE, FALSE, 0);
}

static void construir_ventana(Ide *ide)
{
    /* La ventana principal. */
    ide->ventana = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(ide->ventana), 950, 680);
    g_signal_connect(ide->ventana, "delete-event", G_CALLBACK(al_cerrar), ide);
    g_signal_connect(ide->ventana, "destroy", G_CALLBACK(al_destruir), ide);
    g_signal_connect(ide->ventana, "key-press-event", G_CALLBACK(al_presionar_atajo), ide);

    /* Todo va apilado de arriba hacia abajo. */
    GtkWidget *columna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(ide->ventana), columna);

    /* Barra de botones. */
    GtkWidget *barra = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(barra), 4);
    agregar_boton(barra, "_Nuevo", G_CALLBACK(al_pulsar_nuevo), ide);
    agregar_boton(barra, "_Abrir", G_CALLBACK(al_pulsar_abrir), ide);
    agregar_boton(barra, "_Guardar", G_CALLBACK(al_pulsar_guardar), ide);
    agregar_boton(barra, "Guardar _como", G_CALLBACK(al_pulsar_guardar_como), ide);
    agregar_boton(barra, "Buscar c_lones", G_CALLBACK(al_pulsar_buscar_clones), ide);
    agregar_boton(barra, "_Tabla LL(1)", G_CALLBACK(al_pulsar_tabla_ll1), ide);
    agregar_boton(barra, "Código _muerto", G_CALLBACK(al_pulsar_dce), ide);
    agregar_boton(barra, "_Ensamblador", G_CALLBACK(al_pulsar_ensamblador), ide);
    gtk_box_pack_start(GTK_BOX(columna), barra, FALSE, FALSE, 0);

    /* El editor (arriba) y la lista de errores (abajo), con un divisor movible. */
    GtkWidget *divisor = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(columna), divisor, TRUE, TRUE, 0);

    /* --- El editor --- */
    GtkWidget *vista = gtk_text_view_new();
    ide->vista = GTK_TEXT_VIEW(vista);
    ide->buffer = gtk_text_view_get_buffer(ide->vista);
    gtk_text_view_set_monospace(ide->vista, TRUE);
    gtk_text_view_set_left_margin(ide->vista, 8);
    gtk_text_view_set_top_margin(ide->vista, 6);
    gtk_widget_set_has_tooltip(vista, TRUE);

    /* Tema oscuro del editor (Dark+ de VS Code) y los estilos de los colores
     * del código. Se crean PRIMERO: en GTK manda el estilo creado después,
     * así que los clones, el código muerto y los errores quedan encima. */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, CSS_EDITOR, -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(vista),
                                   GTK_STYLE_PROVIDER(css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /* El panel de abajo y las barras de desplazamiento, también oscuros. */
    GtkCssProvider *css_panel = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_panel, CSS_PANEL, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(css_panel),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_panel);
    for (int i = 0; i < IDE_COLOR_CANTIDAD; ++i) {
        ide->marca_color[i] = gtk_text_buffer_create_tag(ide->buffer, NULL, "foreground",
                                                         COLORES_DARK_PLUS[i], NULL);
    }

    /* Estilo de los clones: fondo amarillo suave. Se crea ANTES que el de
     * errores para que, si se juntan, el rojo del error quede encima. */
    GdkRGBA amarillo = {1.0, 0.85, 0.0, 0.25};
    ide->marca_clon = gtk_text_buffer_create_tag(ide->buffer, "clon",
                                                 "background-rgba", &amarillo, NULL);

    /* Estilos del código muerto: la línea eliminada completa va tachada y
     * en gris; la que solo perdió una parte, con fondo gris suave. */
    GdkRGBA gris = {0.45, 0.45, 0.45, 1.0};
    GdkRGBA fondo_gris = {0.5, 0.5, 0.5, 0.18};
    ide->marca_muerta = gtk_text_buffer_create_tag(ide->buffer, "codigo_muerto",
                                                   "strikethrough", TRUE,
                                                   "foreground-rgba", &gris,
                                                   "background-rgba", &fondo_gris, NULL);
    ide->marca_parcial = gtk_text_buffer_create_tag(ide->buffer, "codigo_muerto_parcial",
                                                    "background-rgba", &fondo_gris, NULL);

    /* Estilo de los errores: subrayado ondulado rojo con fondo rojo suave. */
    GdkRGBA rojo = {0.85, 0.10, 0.10, 1.0};
    GdkRGBA fondo = {1.0, 0.0, 0.0, 0.10};
    ide->marca_error = gtk_text_buffer_create_tag(ide->buffer, "error",
                                                  "underline", PANGO_UNDERLINE_ERROR,
                                                  "underline-rgba", &rojo,
                                                  "background-rgba", &fondo, NULL);

    g_signal_connect(vista, "key-press-event", G_CALLBACK(al_presionar_tecla), ide);
    g_signal_connect(vista, "key-release-event", G_CALLBACK(al_soltar_tecla), ide);
    g_signal_connect(vista, "button-press-event", G_CALLBACK(al_hacer_clic), ide);
    g_signal_connect(vista, "query-tooltip", G_CALLBACK(al_pedir_ayuda), ide);

    /* Números de línea: se reserva la franja izquierda y se dibujan en ella.
     * "_after" para escribir encima de lo que GTK ya pintó en la franja. */
    ajustar_margen_numeros(ide);
    g_signal_connect_after(vista, "draw", G_CALLBACK(al_dibujar_numeros), ide);
    g_signal_connect(ide->buffer, "changed", G_CALLBACK(al_cambiar_texto), ide);
    g_signal_connect(ide->buffer, "mark-set", G_CALLBACK(al_mover_marca), ide);

    GtkWidget *desplazar_editor = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_name(desplazar_editor, "editor");  /* Para el CSS (CSS_PANEL). */
    gtk_container_add(GTK_CONTAINER(desplazar_editor), vista);
    gtk_paned_pack1(GTK_PANED(divisor), desplazar_editor, TRUE, FALSE);

    /* --- La lista de errores --- */
    GtkWidget *lista = gtk_list_box_new();
    ide->lista_errores = GTK_LIST_BOX(lista);
    gtk_list_box_set_activate_on_single_click(ide->lista_errores, TRUE);
    GtkWidget *vacia = gtk_label_new("Sin errores");
    gtk_widget_show(vacia);
    gtk_list_box_set_placeholder(ide->lista_errores, vacia);
    g_signal_connect(lista, "row-activated", G_CALLBACK(al_elegir_error), ide);

    GtkWidget *desplazar_errores = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(desplazar_errores), lista);

    /* --- La lista de clones, con un resumen arriba --- */
    GtkWidget *lista_cl = gtk_list_box_new();
    ide->lista_clones = GTK_LIST_BOX(lista_cl);
    gtk_list_box_set_activate_on_single_click(ide->lista_clones, TRUE);
    g_signal_connect(lista_cl, "row-activated", G_CALLBACK(al_elegir_clon), ide);

    GtkWidget *resumen = gtk_label_new("");
    ide->resumen_clones = GTK_LABEL(resumen);
    gtk_label_set_xalign(ide->resumen_clones, 0.0f);
    gtk_widget_set_margin_start(resumen, 6);
    gtk_widget_set_margin_top(resumen, 4);
    gtk_widget_set_margin_bottom(resumen, 4);

    GtkWidget *desplazar_clones = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(desplazar_clones), lista_cl);
    GtkWidget *caja_clones = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(caja_clones), resumen, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caja_clones), desplazar_clones, TRUE, TRUE, 0);

    /* --- La lista del código muerto: resumen y botón arriba, filas abajo --- */
    GtkWidget *lista_muerto = gtk_list_box_new();
    ide->lista_dce = GTK_LIST_BOX(lista_muerto);
    gtk_list_box_set_activate_on_single_click(ide->lista_dce, TRUE);
    g_signal_connect(lista_muerto, "row-activated", G_CALLBACK(al_elegir_clon), ide);

    GtkWidget *resumen_dce = gtk_label_new(DCE_SIN_ANALIZAR);
    ide->resumen_dce = GTK_LABEL(resumen_dce);
    gtk_label_set_xalign(ide->resumen_dce, 0.0f);
    gtk_label_set_line_wrap(ide->resumen_dce, TRUE);
    GtkWidget *detalle_dce = gtk_button_new_with_label("Ver IR y variables vivas");
    ide->boton_detalle_dce = detalle_dce;
    gtk_widget_set_sensitive(detalle_dce, FALSE);
    g_signal_connect(detalle_dce, "clicked", G_CALLBACK(al_pulsar_detalle_dce), ide);
    GtkWidget *aplicar_dce = gtk_button_new_with_label("Aplicar al código");
    ide->boton_aplicar_dce = aplicar_dce;
    gtk_widget_set_sensitive(aplicar_dce, FALSE);
    gtk_widget_set_tooltip_text(aplicar_dce, "Cambia el código por el código sin el código muerto: "
                                             "el ensamblador y el binario salen de él.");
    g_signal_connect(aplicar_dce, "clicked", G_CALLBACK(al_pulsar_aplicar_dce), ide);

    GtkWidget *encabezado_dce = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(encabezado_dce, 6);
    gtk_widget_set_margin_end(encabezado_dce, 6);
    gtk_widget_set_margin_top(encabezado_dce, 4);
    gtk_widget_set_margin_bottom(encabezado_dce, 4);
    gtk_box_pack_start(GTK_BOX(encabezado_dce), resumen_dce, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(encabezado_dce), aplicar_dce, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(encabezado_dce), detalle_dce, FALSE, FALSE, 0);

    GtkWidget *desplazar_dce = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(desplazar_dce), lista_muerto);
    GtkWidget *caja_dce = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(caja_dce), encabezado_dce, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caja_dce), desplazar_dce, TRUE, TRUE, 0);

    /* --- Pestañas de abajo: Errores, Clones y Código muerto --- */
    GtkWidget *pestanas = gtk_notebook_new();
    gtk_widget_set_name(pestanas, "panel_inferior");  /* Para el CSS (CSS_PANEL). */
    ide->pestanas = GTK_NOTEBOOK(pestanas);
    gtk_notebook_append_page(ide->pestanas, desplazar_errores, gtk_label_new("Errores"));
    gtk_notebook_append_page(ide->pestanas, caja_clones, gtk_label_new("Clones"));
    gtk_notebook_append_page(ide->pestanas, caja_dce, gtk_label_new("Código muerto"));
    gtk_paned_pack2(GTK_PANED(divisor), pestanas, FALSE, FALSE);
    /* El editor ocupa 400 px; el resto queda para la lista de errores. */
    gtk_paned_set_position(GTK_PANED(divisor), 400);

    /* --- Barra de estado --- */
    GtkWidget *estado = gtk_label_new("");
    ide->barra_estado = GTK_LABEL(estado);
    gtk_label_set_xalign(ide->barra_estado, 0.0f);
    gtk_widget_set_margin_start(estado, 6);
    gtk_widget_set_margin_bottom(estado, 4);
    gtk_box_pack_start(GTK_BOX(columna), estado, FALSE, FALSE, 0);

    /* --- Cuadro flotante de sugerencias (no roba el teclado al editor) --- */
    ide->ventana_sugerencias = gtk_popover_new(vista);
    gtk_popover_set_modal(GTK_POPOVER(ide->ventana_sugerencias), FALSE);
    gtk_popover_set_position(GTK_POPOVER(ide->ventana_sugerencias), GTK_POS_BOTTOM);
    GtkWidget *lista_sug = gtk_list_box_new();
    ide->lista_sugerencias = GTK_LIST_BOX(lista_sug);
    gtk_list_box_set_activate_on_single_click(ide->lista_sugerencias, TRUE);
    g_signal_connect(lista_sug, "row-activated", G_CALLBACK(al_elegir_sugerencia), ide);
    gtk_container_add(GTK_CONTAINER(ide->ventana_sugerencias), lista_sug);

    gtk_widget_show_all(ide->ventana);
    gtk_widget_grab_focus(vista);
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    Ide ide;
    memset(&ide, 0, sizeof ide);
    construir_ventana(&ide);

    /* Si se pasó un archivo (./balc-ide archivo.bal), abrirlo. */
    if (argc > 1) {
        if (g_file_test(argv[1], G_FILE_TEST_EXISTS)) {
            abrir_archivo(&ide, argv[1]);
        } else {
            cargar_texto(&ide, "", argv[1]);  /* Archivo nuevo con ese nombre. */
        }
    } else {
        cargar_texto(&ide, "", NULL);
    }

    gtk_main();
    return 0;
}
