/*
 * BAL IDE: la ventana del editor (GTK 3).
 *
 * Este archivo solo se encarga de la parte visual: dibujar el editor,
 * reaccionar a las teclas y mostrar resultados. Toda la lógica está en:
 *   - ide_nucleo.c   -> sugerencias, autocierre y delimitadores.
 *   - ide_analisis.c -> revisar el texto con el compilador.
 *
 * Qué hace el IDE:
 *   1. Sugerencias: al escribir una palabra aparece una lista para completarla.
 *      Flechas para elegir, Enter o Tab para aceptar, Esc para cerrar.
 *      Ctrl+Espacio la abre a mano.
 *   2. Autocierre: al escribir ( [ { " $ se agrega el cierre solo. Si se
 *      escribe el cierre y ya estaba ahí, solo se avanza el cursor.
 *      Enter mantiene la sangría y, entre { }, abre una línea nueva adentro.
 *   3. Errores: medio segundo después de dejar de escribir, se revisa el
 *      código. Los errores se subrayan en rojo; al pasar el mouse encima se
 *      ve la explicación. Abajo hay una lista: un clic lleva al error.
 *
 * Atajos: Ctrl+N nuevo, Ctrl+O abrir, Ctrl+S guardar.
 * Uso: ./balc-ide [archivo.bal]
 */
#include <gtk/gtk.h>
#include <string.h>

#include "ide_analisis.h"
#include "ide_nucleo.h"

/* Milisegundos que se espera, después de la última tecla, para revisar el código. */
#define ESPERA_ANALISIS_MS 500

/* Máximo de sugerencias que se muestran a la vez. */
#define MAX_SUGERENCIAS 12

/* Todo lo que la ventana necesita recordar. */
typedef struct {
    GtkWidget *ventana;
    GtkTextView *vista;            /* El editor de texto. */
    GtkTextBuffer *buffer;         /* El texto que se está editando. */
    GtkTextTag *marca_error;       /* Estilo del subrayado rojo. */
    GtkListBox *lista_errores;     /* Lista de errores de abajo. */
    GtkLabel *barra_estado;        /* "Línea X, columna Y · N errores". */
    GtkWidget *ventana_sugerencias;   /* Cuadro flotante con las sugerencias. */
    GtkListBox *lista_sugerencias;

    char *ruta;                 /* Archivo abierto; NULL si todavía no se guardó. */
    gboolean modificado;        /* TRUE si hay cambios sin guardar. */
    guint temporizador;         /* Cuenta regresiva para revisar el código. */
    DiagLista errores;          /* Errores de la última revisión. */
    int largo_prefijo;          /* Letras ya escritas de la palabra que se sugiere. */
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

    GtkWidget *fila = gtk_list_box_row_new();
    gtk_container_add(GTK_CONTAINER(fila), etiqueta);
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
    diag_liberar(&ide->errores);
    gchar *texto = texto_del_editor(ide);
    int resultado = ide_analizar_texto(texto, strlen(texto), ide->ruta, &ide->errores);
    g_free(texto);

    /* Paso 2: quitar los subrayados y la lista de la revisión anterior. */
    GtkTextIter inicio, fin;
    gtk_text_buffer_get_bounds(ide->buffer, &inicio, &fin);
    gtk_text_buffer_remove_tag(ide->buffer, ide->marca_error, &inicio, &fin);
    vaciar_lista(ide->lista_errores);

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
    gchar *prefijo = gtk_text_buffer_get_text(ide->buffer, &inicio, &cursor, FALSE);
    gchar *texto = texto_del_editor(ide);
    IdeSugerencias sugerencias = {0};
    int resultado = ide_sugerencias(texto, strlen(texto), prefijo, &sugerencias);
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
    if (!ide->modificado) {
        ide->modificado = TRUE;
        actualizar_titulo(ide);
    }
    programar_revision(ide);
}

/* El cursor se movió: actualizar "Línea X, columna Y". */
static void al_mover_marca(GtkTextBuffer *buffer, GtkTextIter *posicion, GtkTextMark *marca,
                           gpointer datos)
{
    (void)posicion;
    Ide *ide = datos;
    if (marca == gtk_text_buffer_get_insert(buffer)) actualizar_estado(ide);
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

/* Atajos de teclado de la ventana: Ctrl+N, Ctrl+O, Ctrl+S. */
static gboolean al_presionar_atajo(GtkWidget *widget, GdkEventKey *evento, gpointer datos)
{
    (void)widget;
    if (!(evento->state & GDK_CONTROL_MASK)) return FALSE;
    switch (gdk_keyval_to_lower(evento->keyval)) {
        case GDK_KEY_n: al_pulsar_nuevo(NULL, datos); return TRUE;
        case GDK_KEY_o: al_pulsar_abrir(NULL, datos); return TRUE;
        case GDK_KEY_s: al_pulsar_guardar(NULL, datos); return TRUE;
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
    diag_liberar(&ide->errores);
    g_free(ide->ruta);
    gtk_main_quit();
}

/* ================================================================== */
/* Construcción de la ventana                                          */
/* ================================================================== */

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
    g_signal_connect(ide->buffer, "changed", G_CALLBACK(al_cambiar_texto), ide);
    g_signal_connect(ide->buffer, "mark-set", G_CALLBACK(al_mover_marca), ide);

    GtkWidget *desplazar_editor = gtk_scrolled_window_new(NULL, NULL);
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
    gtk_paned_pack2(GTK_PANED(divisor), desplazar_errores, FALSE, FALSE);
    gtk_paned_set_position(GTK_PANED(divisor), 500);

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
