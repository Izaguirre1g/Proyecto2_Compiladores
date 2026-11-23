/*
 * Ventana "Tabla LL(1)" del IDE.
 *
 * Solo MUESTRA lo que ya calcula src/LL1/ll1_tabla.c (no calcula nada):
 *   - ll1_producciones: las producciones de la gramática.
 *   - ll1_tabla(): PRIMERO, SIGUIENTE y la tabla M[A, a].
 *
 * Es la misma información que "make tabla-ll1" escribe en docs/ll1_tabla.md,
 * pero dentro del IDE y con la tabla en forma de matriz, como en las
 * diapositivas: filas = no terminales (A), columnas = terminales (a).
 *
 * Notación, igual que en las diapositivas:
 *   - cada celda M[A, a] tiene el LADO DERECHO de la producción (o ε);
 *     una celda vacía es un error de sintaxis;
 *   - los no terminales van en cursiva: sentencia, expresion, bloque;
 *   - los terminales van en negrita y como se escriben en BAL: create_funk,
 *     #, (, ;. Los que no tienen un texto fijo se abrevian: id (un nombre),
 *     num (un número), cadena, car (un carácter) y $ (fin del archivo).
 *
 * La matriz tiene 62 filas y muchas columnas, así que se ve con dos listas
 * una al lado de la otra que se desplazan juntas hacia abajo:
 *                  | create_funk         | id                      | ...
 *     programa     | elemento_superior.. |                         | ...
 *     sentencia    |                     | id resto_identificador  | ...
 * La de la izquierda (los no terminales) queda fija al moverse a la
 * derecha, para no perder de vista en qué fila se está.
 */
#include "ide_tabla_ll1.h"

#include "ll1.h"

/* La ventana abierta (solo hay una): NULL si está cerrada. */
static GtkWidget *ventana_tabla = NULL;

/* Dónde se escribe la producción de la celda elegida. */
static GtkLabel *detalle_celda = NULL;

/* ------------------------------------------------------------------ */
/* Textos                                                              */
/* ------------------------------------------------------------------ */

/* Cómo se muestra un terminal: como se escribe en BAL (";", "create_funk"),
 * o una abreviatura si no tiene un texto fijo. */
static const char *texto_terminal(int t)
{
    switch (t) {
        case LL1_FIN: return "$";
        case TOKEN_IDENTIFIER: return "id";
        case TOKEN_INTEGER: return "num";
        case TOKEN_STRING: return "cadena";
        case TOKEN_CHARACTER: return "car";
        default: return ll1_texto_fijo(t) ? ll1_texto(t) : ll1_nombre(t);
    }
}

/* Agrega un símbolo con formato (Pango markup): terminal en negrita y no
 * terminal en cursiva, como en las diapositivas. */
static void agregar_simbolo(GString *texto, int simbolo)
{
    gchar *escapado;
    if (LL1_ES_TERMINAL(simbolo)) {
        escapado = g_markup_escape_text(texto_terminal(simbolo), -1);
        g_string_append_printf(texto, "<b>%s</b>", escapado);
    } else {
        escapado = g_markup_escape_text(ll1_nombre(simbolo), -1);
        g_string_append_printf(texto, "<i>%s</i>", escapado);
    }
    g_free(escapado);
}

/* El lado derecho de una producción ("X Y Z"), o "ε" si es vacío. Es lo
 * que va en la celda de la tabla. */
static void agregar_cuerpo(GString *texto, int p)
{
    const Ll1Produccion *prod = &ll1_producciones[p];
    int largo = ll1_largo_cuerpo(prod);
    if (largo == 0) g_string_append(texto, "ε");
    for (int i = 0; i < largo; ++i) {
        if (i > 0) g_string_append_c(texto, ' ');
        agregar_simbolo(texto, prod->cuerpo[i]);
    }
}

/* El lado derecho solo (para la celda). */
static gchar *texto_cuerpo(int p)
{
    GString *texto = g_string_new(NULL);
    agregar_cuerpo(texto, p);
    return g_string_free(texto, FALSE);
}

/* La producción completa: "A → X Y Z". */
static gchar *texto_produccion(int p)
{
    GString *texto = g_string_new(NULL);
    agregar_simbolo(texto, ll1_producciones[p].cabeza);
    g_string_append(texto, " → ");
    agregar_cuerpo(texto, p);
    return g_string_free(texto, FALSE);
}

/* Un conjunto como "{ a, b, ε }". */
static gchar *texto_conjunto(const Ll1Conjunto *conjunto)
{
    GString *texto = g_string_new("{ ");
    const char *separador = "";
    for (int t = 0; t < LL1_TERMINALES; ++t) {
        if (!conjunto->tiene[t]) continue;
        g_string_append(texto, separador);
        agregar_simbolo(texto, t);
        separador = ", ";
    }
    if (conjunto->epsilon) g_string_append_printf(texto, "%sε", separador);
    g_string_append(texto, " }");
    return g_string_free(texto, FALSE);
}

/* El nombre de un símbolo solo, con formato. */
static gchar *texto_simbolo(int simbolo)
{
    GString *texto = g_string_new(NULL);
    agregar_simbolo(texto, simbolo);
    return g_string_free(texto, FALSE);
}

/* ------------------------------------------------------------------ */
/* Ayudas para armar listas (GtkTreeView)                              */
/* ------------------------------------------------------------------ */

/*
 * Agrega a "vista" una columna que muestra la columna "indice" del modelo.
 * Tanto el título como las celdas usan formato (negrita, cursiva), así que
 * el texto debe venir escapado (ver agregar_simbolo).
 */
static GtkTreeViewColumn *agregar_columna(GtkTreeView *vista, const char *titulo, int indice,
                                          float alineacion, int ancho_ajuste)
{
    GtkCellRenderer *celda = gtk_cell_renderer_text_new();
    g_object_set(celda, "xalign", alineacion, NULL);
    if (ancho_ajuste > 0) {
        /* Textos largos (conjuntos, producciones): se parten en varias líneas. */
        g_object_set(celda, "wrap-mode", PANGO_WRAP_WORD, "wrap-width", ancho_ajuste, NULL);
    }
    GtkTreeViewColumn *columna =
        gtk_tree_view_column_new_with_attributes(NULL, celda, "markup", indice, NULL);
    /* El título va en una etiqueta propia: si se pasa como texto, GTK toma
     * el '_' como atajo de teclado y lo esconde (create_funk -> createfunk). */
    GtkWidget *etiqueta = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(etiqueta), titulo);
    gtk_widget_show(etiqueta);
    gtk_tree_view_column_set_widget(columna, etiqueta);
    gtk_tree_view_column_set_resizable(columna, TRUE);
    gtk_tree_view_append_column(vista, columna);
    return columna;
}

/* Pone una lista dentro de un recuadro con barras de desplazamiento. */
static GtkWidget *con_desplazamiento(GtkWidget *hijo)
{
    GtkWidget *recuadro = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(recuadro), hijo);
    return recuadro;
}

/* Una lista nueva sin modelo, con líneas de cuadrícula y letra monoespaciada. */
static GtkTreeView *lista_nueva(GtkTreeModel *modelo)
{
    GtkWidget *vista = gtk_tree_view_new_with_model(modelo);
    g_object_unref(modelo);  /* La lista se queda con el modelo. */
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(vista), GTK_TREE_VIEW_GRID_LINES_BOTH);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(vista), FALSE);
    return GTK_TREE_VIEW(vista);
}

/* ------------------------------------------------------------------ */
/* Pestaña 1: la tabla M[A, a]                                         */
/* ------------------------------------------------------------------ */

/*
 * Al hacer clic en una celda de la matriz, se escribe abajo qué dice:
 *   M[sentencia, id] = producción 37:  sentencia → id resto_identificador
 * o que está vacía (error). La columna guarda su terminal en "terminal".
 */
static gboolean al_hacer_clic_en_celda(GtkWidget *widget, GdkEventButton *evento, gpointer datos)
{
    (void)datos;
    GtkTreeView *vista = GTK_TREE_VIEW(widget);
    GtkTreePath *camino = NULL;
    GtkTreeViewColumn *columna = NULL;
    if (!gtk_tree_view_get_path_at_pos(vista, (gint)evento->x, (gint)evento->y,
                                       &camino, &columna, NULL, NULL)) {
        return FALSE;
    }
    int fila = gtk_tree_path_get_indices(camino)[0];  /* Fila = no terminal A. */
    gtk_tree_path_free(camino);
    int terminal = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(columna), "terminal"));

    gchar *a = texto_simbolo(fila + LL1_TERMINALES);
    gchar *t = texto_simbolo(terminal);
    int p = ll1_tabla()->m[fila][terminal];
    gchar *mensaje;
    if (p < 0) {
        mensaje = g_strdup_printf(
            "M[%s, %s] está vacía: si en la cima de la pila está %s y llega %s, "
            "es un <b>error de sintaxis</b>.", a, t, a, t);
    } else {
        gchar *prod = texto_produccion(p);
        mensaje = g_strdup_printf(
            "M[%s, %s] = producción %d:   %s\n"
            "Si en la cima de la pila está %s y llega %s, se cambia %s por el "
            "lado derecho de la producción.", a, t, p, prod, a, t, a);
        g_free(prod);
    }
    g_free(a);
    g_free(t);
    gtk_label_set_markup(detalle_celda, mensaje);
    g_free(mensaje);
    return FALSE;  /* Que la lista también marque la fila elegida. */
}

static GtkWidget *pestana_matriz(void)
{
    const Ll1Tabla *tabla = ll1_tabla();

    /* Solo se muestran los terminales que tienen al menos una celda llena:
     * un terminal sin ninguna sería una columna completamente vacía. */
    int columnas[LL1_TERMINALES];
    int cuantas = 0;
    for (int i = 0; i < LL1_TERMINALES; ++i) {
        /* El $ es el token 0, pero en las diapositivas va en la última
         * columna: se recorre desde el 1 y el $ se deja para el final. */
        int t = (i + 1) % LL1_TERMINALES;
        for (int a = 0; a < LL1_NO_TERMINALES; ++a) {
            if (tabla->m[a][t] >= 0) {
                columnas[cuantas++] = t;
                break;
            }
        }
    }

    /* Lista de la izquierda: el nombre de cada no terminal. */
    GtkListStore *nombres = gtk_list_store_new(1, G_TYPE_STRING);
    /* Matriz: una columna de texto por terminal mostrado. */
    GType tipos[LL1_TERMINALES];
    for (int i = 0; i < cuantas; ++i) tipos[i] = G_TYPE_STRING;
    GtkListStore *matriz = gtk_list_store_newv(cuantas, tipos);

    for (int a = 0; a < LL1_NO_TERMINALES; ++a) {
        GtkTreeIter fila;
        gchar *nombre = texto_simbolo(a + LL1_TERMINALES);
        gtk_list_store_insert_with_values(nombres, &fila, -1, 0, nombre, -1);
        g_free(nombre);

        gtk_list_store_append(matriz, &fila);
        for (int i = 0; i < cuantas; ++i) {
            int p = tabla->m[a][columnas[i]];
            if (p < 0) continue;  /* Vacía = error: se deja en blanco. */
            gchar *cuerpo = texto_cuerpo(p);  /* El lado derecho, como en las diapositivas. */
            gtk_list_store_set(matriz, &fila, i, cuerpo, -1);
            g_free(cuerpo);
        }
    }

    GtkTreeView *vista_nombres = lista_nueva(GTK_TREE_MODEL(nombres));
    agregar_columna(vista_nombres, "", 0, 0.0f, 0);

    GtkTreeView *vista_matriz = lista_nueva(GTK_TREE_MODEL(matriz));
    for (int i = 0; i < cuantas; ++i) {
        gchar *titulo = texto_simbolo(columnas[i]);
        GtkTreeViewColumn *c = agregar_columna(vista_matriz, titulo, i, 0.5f, 0);
        g_free(titulo);
        g_object_set_data(G_OBJECT(c), "terminal", GINT_TO_POINTER(columnas[i]));
    }
    g_signal_connect(vista_matriz, "button-press-event", G_CALLBACK(al_hacer_clic_en_celda), NULL);

    /* Las dos listas se desplazan juntas hacia abajo: la de los nombres usa
     * la misma barra vertical que la matriz (y no muestra la suya). */
    GtkWidget *recuadro_matriz = con_desplazamiento(GTK_WIDGET(vista_matriz));
    GtkWidget *recuadro_nombres = gtk_scrolled_window_new(
        NULL, gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(recuadro_matriz)));
    gtk_container_add(GTK_CONTAINER(recuadro_nombres), GTK_WIDGET(vista_nombres));
    /* Los nombres también tienen barra horizontal (vacía, porque caben
     * completos): así reservan abajo el mismo alto que la barra de la matriz
     * y la última fila no queda escondida. */
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(recuadro_nombres),
                                   GTK_POLICY_ALWAYS, GTK_POLICY_EXTERNAL);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(recuadro_nombres), TRUE);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(recuadro_nombres), FALSE);
    /* La matriz siempre tiene barra horizontal. */
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(recuadro_matriz),
                                   GTK_POLICY_ALWAYS, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(recuadro_matriz), FALSE);

    GtkWidget *listas = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(listas), recuadro_nombres, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(listas), recuadro_matriz, TRUE, TRUE, 0);

    /* Debajo de la matriz: qué dice la celda elegida. */
    GtkWidget *detalle = gtk_label_new(
        "Cada celda tiene el lado derecho de la producción que se usa. "
        "Haz clic en una celda para ver la producción completa; una celda vacía es error de sintaxis.");
    gtk_label_set_xalign(GTK_LABEL(detalle), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(detalle), TRUE);
    gtk_label_set_selectable(GTK_LABEL(detalle), TRUE);
    detalle_celda = GTK_LABEL(detalle);

    GtkWidget *pestana = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(pestana), 6);
    gtk_box_pack_start(GTK_BOX(pestana), listas, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pestana), detalle, FALSE, FALSE, 0);
    return pestana;
}

/* ------------------------------------------------------------------ */
/* Pestaña 2: las producciones                                         */
/* ------------------------------------------------------------------ */

static GtkWidget *pestana_producciones(void)
{
    GtkListStore *modelo = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    for (int p = 0; p < ll1_cantidad_producciones; ++p) {
        gchar *texto = texto_produccion(p);
        gchar numero[16];
        g_snprintf(numero, sizeof numero, "%d", p);
        gtk_list_store_insert_with_values(modelo, NULL, -1, 0, numero, 1, texto, -1);
        g_free(texto);
    }
    GtkTreeView *vista = lista_nueva(GTK_TREE_MODEL(modelo));
    agregar_columna(vista, "#", 0, 1.0f, 0);
    agregar_columna(vista, "Producción", 1, 0.0f, 0);
    return con_desplazamiento(GTK_WIDGET(vista));
}

/* ------------------------------------------------------------------ */
/* Pestaña 3: PRIMERO y SIGUIENTE                                      */
/* ------------------------------------------------------------------ */

static GtkWidget *pestana_conjuntos(void)
{
    const Ll1Tabla *tabla = ll1_tabla();
    GtkListStore *modelo = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    for (int a = 0; a < LL1_NO_TERMINALES; ++a) {
        gchar *primero = texto_conjunto(&tabla->primero[a]);
        gchar *siguiente = texto_conjunto(&tabla->siguiente[a]);
        gchar *nombre = texto_simbolo(a + LL1_TERMINALES);
        gtk_list_store_insert_with_values(modelo, NULL, -1, 0, nombre,
                                          1, primero, 2, siguiente, -1);
        g_free(nombre);
        g_free(primero);
        g_free(siguiente);
    }
    GtkTreeView *vista = lista_nueva(GTK_TREE_MODEL(modelo));
    agregar_columna(vista, "No terminal", 0, 0.0f, 0);
    agregar_columna(vista, "PRIMERO", 1, 0.0f, 380);
    agregar_columna(vista, "SIGUIENTE", 2, 0.0f, 380);
    return con_desplazamiento(GTK_WIDGET(vista));
}

/* ------------------------------------------------------------------ */
/* La ventana                                                          */
/* ------------------------------------------------------------------ */

/* Al cerrarla se olvida, para que el botón la vuelva a crear. */
static void al_destruir_tabla(GtkWidget *widget, gpointer datos)
{
    (void)widget;
    (void)datos;
    ventana_tabla = NULL;
    detalle_celda = NULL;
}

void ide_mostrar_tabla_ll1(GtkWindow *padre)
{
    /* Si ya está abierta, solo se trae al frente. */
    if (ventana_tabla != NULL) {
        gtk_window_present(GTK_WINDOW(ventana_tabla));
        return;
    }

    const Ll1Tabla *tabla = ll1_tabla();

    ventana_tabla = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ventana_tabla), "Tabla LL(1) de BAL");
    gtk_window_set_transient_for(GTK_WINDOW(ventana_tabla), padre);
    gtk_window_set_default_size(GTK_WINDOW(ventana_tabla), 1000, 620);
    g_signal_connect(ventana_tabla, "destroy", G_CALLBACK(al_destruir_tabla), NULL);

    /* Arriba, el resumen: si hay 0 conflictos, la gramática es LL(1). */
    gchar *resumen;
    if (tabla->conflictos == 0) {
        resumen = g_strdup_printf(
            "%d producciones · %d no terminales · <b>0 conflictos</b> "
            "(ninguna celda tiene dos producciones) → <b>la gramática es LL(1)</b>",
            ll1_cantidad_producciones, LL1_NO_TERMINALES);
    } else {
        resumen = g_strdup_printf(
            "%d producciones · %d no terminales · "
            "<span foreground=\"red\"><b>%d conflictos</b> → la gramática NO es LL(1)</span>",
            ll1_cantidad_producciones, LL1_NO_TERMINALES, tabla->conflictos);
    }
    GtkWidget *encabezado = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(encabezado), resumen);
    gtk_label_set_xalign(GTK_LABEL(encabezado), 0.0f);
    g_free(resumen);

    GtkWidget *pestanas = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_matriz(),
                             gtk_label_new("Tabla M[A, a]"));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_producciones(),
                             gtk_label_new("Producciones"));
    gtk_notebook_append_page(GTK_NOTEBOOK(pestanas), pestana_conjuntos(),
                             gtk_label_new("PRIMERO y SIGUIENTE"));

    GtkWidget *columna = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(columna), 8);
    gtk_box_pack_start(GTK_BOX(columna), encabezado, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(columna), pestanas, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(ventana_tabla), columna);

    gtk_widget_show_all(ventana_tabla);
}
