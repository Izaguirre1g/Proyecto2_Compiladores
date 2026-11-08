/* Incluye las estructuras, los tipos y las declaraciones de funciones del árbol sintáctico abstracto (AST). */
#include "ast.h"

/* Incluye las funciones de reserva y liberación de memoria: calloc, malloc y free. */
#include <stdlib.h>
/* Incluye las funciones strlen y memcpy para medir y copiar cadenas. */
#include <string.h>

/* Crea un nodo del tipo indicado, con texto opcional y ubicación en el código fuente. */
AstNodo *ast_crear(Ast *arbol, AstTipo tipo, const char *texto, AstUbicacion ubicacion) {
    /* Reserva memoria para un nodo e inicializa todos sus bytes en cero. */
    AstNodo *nodo = calloc(1, sizeof(*nodo));
    /* Devuelve NULL si no se pudo reservar memoria para el nodo. */
    if (!nodo) return NULL;
    /* Copia el texto únicamente cuando se recibe una cadena distinta de NULL. */
    if (texto) {
        /* Calcula la cantidad de bytes del texto, sin contar el terminador nulo. */
        size_t longitud = strlen(texto);
        /* Reserva espacio para el texto y un byte adicional para su terminador nulo. */
        nodo->texto = malloc(longitud + 1);
        /* Si falla la reserva del texto, libera el nodo y devuelve NULL para evitar una fuga de memoria. */
        if (!nodo->texto) { free(nodo); return NULL; }
        /* Copia el texto completo, incluido el terminador nulo, en la memoria propia del nodo. */
        memcpy(nodo->texto, texto, longitud + 1);
    /* Finaliza la copia opcional del texto. */
    }
    /* Guarda el tipo de construcción sintáctica que representa el nodo. */
    nodo->tipo = tipo;
    /* Guarda la ubicación del nodo en el código fuente. */
    nodo->ubicacion = ubicacion;
    /* Enlaza el nodo con el inicio actual de la lista privada de reservas del árbol. */
    nodo->reservado_siguiente = arbol->reservados;
    /* Coloca el nuevo nodo al inicio de la lista que se usará para liberar la memoria. */
    arbol->reservados = nodo;
    /* Devuelve el nodo creado e incorporado a la lista de reservas. */
    return nodo;
/* Finaliza la función de creación de nodos. */
}

/* Añade un hijo al final de la lista de hijos del padre; el hijo debe estar sin padre ni hermanos. */
void ast_agregar_hijo(AstNodo *padre, AstNodo *hijo) {
    /* No realiza ninguna operación si el hijo recibido es NULL. */
    if (!hijo) return;
    /* Establece el enlace del hijo hacia su padre. */
    hijo->padre = padre;
    /* Si ya hay hijos, enlaza el último con el nuevo hijo como siguiente hermano. */
    if (padre->ultimo_hijo) padre->ultimo_hijo->siguiente = hijo;
    /* Si no había hijos, establece el nuevo nodo como primer hijo del padre. */
    else padre->primer_hijo = hijo;
    /* Actualiza el último hijo del padre para que sea el nodo recién añadido. */
    padre->ultimo_hijo = hijo;
/* Finaliza la función que añade hijos. */
}

/* Libera todos los nodos reservados por el árbol, incluso los que no estén conectados a la raíz. */
void ast_liberar(Ast *arbol) {
    /* Comienza el recorrido por el primer nodo de la lista privada de reservas. */
    AstNodo *nodo = arbol->reservados;
    /* Repite la liberación mientras queden nodos en la lista de reservas. */
    while (nodo) {
        /* Guarda el enlace al próximo nodo antes de liberar el nodo actual. */
        AstNodo *siguiente = nodo->reservado_siguiente;
        /* Libera la copia del texto del nodo; free también acepta NULL. */
        free(nodo->texto);
        /* Libera la memoria ocupada por el nodo actual. */
        free(nodo);
        /* Avanza al nodo cuyo enlace se guardó antes de la liberación. */
        nodo = siguiente;
    /* Finaliza el recorrido de la lista de reservas. */
    }
    /* Restablece el árbol a su estado vacío, con raíz y lista de reservas nulas. */
    *arbol = (Ast){0};
/* Finaliza la función de liberación del árbol. */
}

/* Obtiene el nombre legible correspondiente a un valor de AstTipo. */
const char *ast_nombre_tipo(AstTipo tipo) {
    /* Define una tabla persistente de punteros constantes a cadenas constantes, en el orden de AstTipo. */
    static const char *const nombres[] = {
        /* Nombres de los tipos de programa, función, parámetros, parámetro y tipo. */
        "Programa", "Funcion", "Parametros", "Parametro", "Tipo",
        /* Nombres de bloque, declaración, asignación y operaciones binarias y unarias. */
        "Bloque", "Declaracion", "Asignacion", "Binario", "Unario",
        /* Nombres de identificador y de los literales entero, cadena, carácter y booleano. */
        "Identificador", "Entero", "Cadena", "Caracter", "Booleano",
        /* Nombres de condicional, ciclo mientras, retorno, llamada y argumentos. */
        "Si", "Mientras", "Retorno", "Llamada", "Argumentos",
        /* Nombres de los tipos restantes, desde importación hasta captura, en el mismo orden del enumerado. */
        "Importacion", "Constante", "Declaraciones", "TipoArreglo", "TipoLista", "Dimensiones", "Dimension", "Indice", "AccesoMiembro", "LiteralColeccion", "Lista", "Agregar", "Eliminar", "Tamano", "Recorrido", "Intentar", "Capturas", "Captura"
    /* Finaliza la tabla de nombres de los tipos de nodo. */
    };
    /* Comprueba que el índice convertido a unsigned sea menor que la cantidad de nombres disponibles. */
    return (unsigned)tipo < sizeof(nombres) / sizeof(nombres[0])
        /* Devuelve el nombre asociado si el índice es válido; en otro caso, devuelve Desconocido. */
        ? nombres[tipo] : "Desconocido";
/* Finaliza la función que obtiene el nombre del tipo. */
}

/* Imprime una cadena entre comillas con escapes para ciertos caracteres; esta función es privada del archivo. */
static void imprimir_texto(FILE *salida, const char *texto) {
    /* Escribe la comilla doble que abre el texto. */
    fputc('"', salida);
    /* Recorre la cadena byte a byte como valores sin signo hasta encontrar el terminador nulo. */
    for (const unsigned char *p = (const unsigned char *)texto; *p; ++p) {
        /* Ante una comilla doble o barra inversa, escribe una barra inversa de escape y después el byte original. */
        if (*p == '"' || *p == '\\') { fputc('\\', salida); fputc(*p, salida); }
        /* Representa un salto de línea mediante la secuencia visible de barra inversa y letra n. */
        else if (*p == '\n') fputs("\\n", salida);
        /* Representa un retorno de carro mediante la secuencia visible de barra inversa y letra r. */
        else if (*p == '\r') fputs("\\r", salida);
        /* Representa una tabulación mediante la secuencia visible de barra inversa y letra t. */
        else if (*p == '\t') fputs("\\t", salida);
        /* Representa los demás controles ASCII, incluido DEL, con un escape hexadecimal de dos dígitos. */
        else if (*p < 0x20 || *p == 0x7f) fprintf(salida, "\\x%02X", *p);
        /* Escribe directamente los bytes que no necesitan ninguna de las representaciones anteriores. */
        else fputc(*p, salida);
    /* Finaliza el recorrido de los bytes del texto. */
    }
    /* Escribe la comilla doble que cierra el texto. */
    fputc('"', salida);
/* Finaliza la función privada de impresión de cadenas. */
}

/* Imprime el árbol mediante un recorrido iterativo en profundidad, visitando cada padre antes que sus hijos. */
void ast_imprimir(const Ast *arbol, FILE *salida) {
    /* Inicia el recorrido en la raíz del árbol sin modificar sus nodos. */
    const AstNodo *nodo = arbol->raiz;
    /* Inicializa en cero la profundidad, que determina la sangría de la raíz. */
    size_t profundidad = 0;
    /* Continúa el recorrido mientras exista un nodo por imprimir. */
    while (nodo) {
        /* Escribe dos espacios de sangría por cada nivel de profundidad del nodo. */
        for (size_t i = 0; i < profundidad; ++i) fputs("  ", salida);
        /* Escribe el nombre legible del tipo del nodo actual. */
        fputs(ast_nombre_tipo(nodo->tipo), salida);
        /* Si el nodo tiene texto, escribe un espacio y luego el texto entre comillas y con escapes. */
        if (nodo->texto) { fputc(' ', salida); imprimir_texto(salida, nodo->texto); }
        /* Escribe la línea y la columna iniciales del nodo, precedidas por @, y termina la línea de salida. */
        fprintf(salida, " @%zu:%zu\n", nodo->ubicacion.first_line, nodo->ubicacion.first_column);
        /* Comprueba si el nodo tiene hijos para descender por el primero. */
        if (nodo->primer_hijo) {
            /* Avanza al primer hijo del nodo actual. */
            nodo = nodo->primer_hijo;
            /* Incrementa la profundidad al bajar un nivel en el árbol. */
            ++profundidad;
        /* Si el nodo no tiene hijos, busca un hermano pendiente en este nivel o en un nivel superior. */
        } else {
            /* Sube por los padres mientras exista un nodo y este no tenga un siguiente hermano. */
            while (nodo && !nodo->siguiente) {
                /* Retrocede al padre del nodo actual. */
                nodo = nodo->padre;
                /* Reduce la profundidad al subir, evitando decrementar el valor sin signo cuando ya es cero. */
                if (profundidad) --profundidad;
            /* Finaliza el ascenso al encontrar un hermano pendiente o llegar más allá de la raíz. */
            }
            /* Si aún hay un nodo, continúa por su siguiente hermano. */
            if (nodo) nodo = nodo->siguiente;
        /* Finaliza la elección del próximo nodo después de visitar un nodo sin hijos. */
        }
    /* Finaliza el recorrido cuando ya no quedan nodos por imprimir. */
    }
/* Finaliza la función de impresión del árbol. */
}


/* JSON necesita escapes Unicode para los controles, no escapes de C. */
static void json_texto(FILE *salida, const char *texto) {
    if (!texto) { fputs("null", salida); return; }
    fputc('"', salida);
    for (const unsigned char *p = (const unsigned char *)texto; *p; ++p) {
        if (*p == '"' || *p == '\\') { fputc('\\', salida); fputc(*p, salida); }
        else if (*p < 0x20) fprintf(salida, "\\u%04x", (unsigned)*p);
        else fputc(*p, salida);
    }
    fputc('"', salida);
}

static void json_abrir_nodo(FILE *salida, const AstNodo *nodo) {
    fputs("{\"tipo\":", salida);
    json_texto(salida, ast_nombre_tipo(nodo->tipo));
    fputs(",\"texto\":", salida);
    json_texto(salida, nodo->texto);
    fprintf(salida, ",\"ubicacion\":{\"linea_inicio\":%zu,\"columna_inicio\":%zu,"
            "\"linea_fin\":%zu,\"columna_fin\":%zu},\"hijos\":[",
            nodo->ubicacion.first_line, nodo->ubicacion.first_column,
            nodo->ubicacion.last_line, nodo->ubicacion.last_column);
}

int ast_exportar_json(const Ast *arbol, FILE *salida) {
    if (!arbol || !salida) return 1;
    const AstNodo *nodo = arbol->raiz;
    if (!nodo) { fputs("null\n", salida); return ferror(salida) != 0; }
    /* Recorremos padres y hermanos sin recursion ni modificar el AST. */
    json_abrir_nodo(salida, nodo);
    for (;;) {
        if (nodo->primer_hijo) {
            nodo = nodo->primer_hijo;
            json_abrir_nodo(salida, nodo);
            continue;
        }
        fputs("]}", salida);
        while (nodo != arbol->raiz && !nodo->siguiente) {
            nodo = nodo->padre;
            fputs("]}", salida);
        }
        if (nodo == arbol->raiz || ferror(salida)) break;
        nodo = nodo->siguiente;
        fputc(',', salida);
        json_abrir_nodo(salida, nodo);
    }
    fputc('\n', salida);
    return ferror(salida) != 0;
}
