#include "symbol_table.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//Copia una cadena a memoria dinamica
//Se evita strdup para mantener compatibilidad con C11 estandar
 
static char *copiar(const char *texto)
{
    size_t longitud = strlen(texto) + 1;

    char *copia = malloc(longitud);

    if (!copia) {
        return NULL;
    }

    memcpy(copia, texto, longitud);

    return copia;
}

//Crea un ambito nuevo
 
static SimAmbito *crear_ambito(
    const char *nombre,
    SimAmbito *padre
)
{
    SimAmbito *ambito = calloc(1, sizeof(*ambito));

    if (!ambito) {
        return NULL;
    }

    ambito->nombre = copiar(nombre);

    if (!ambito->nombre) {
        free(ambito);
        return NULL;
    }

    ambito->padre = padre;

    ambito->nivel = padre ? padre->nivel + 1 : 0;

    ambito->simbolos = NULL;

    return ambito;
}

//Inicializa la tabla y crea automaticamente el ambito global.
 
SimResultado sim_inicializar(SimTabla *tabla)
{
    if (!tabla || tabla->global || tabla->actual) {
        return SIM_ARGUMENTO_INVALIDO;
    }

    SimAmbito *global = crear_ambito(
        "global",
        NULL
    );

    if (!global) {
        return SIM_SIN_MEMORIA;
    }

    tabla->global = global;
    tabla->actual = global;
    tabla->ambitos_ultimo = global;

    return SIM_OK;
}

//Entra en un nuevo ambito
 
SimResultado sim_entrar_ambito(
    SimTabla *tabla,
    const char *nombre
)
{
    if (!tabla ||
        !tabla->actual ||
        !nombre ||
        !*nombre) {
        return SIM_ARGUMENTO_INVALIDO;
    }

    SimAmbito *nuevo = crear_ambito(
        nombre,
        tabla->actual
    );

    if (!nuevo) {
        return SIM_SIN_MEMORIA;
    }

    tabla->ambitos_ultimo->siguiente_creacion = nuevo;
    tabla->ambitos_ultimo = nuevo;

    tabla->actual = nuevo;

    return SIM_OK;
}

//Sale del ambito actual pero no permite salir del ambito global
 
int sim_salir_ambito(SimTabla *tabla)
{
    if (!tabla ||
        !tabla->actual ||
        !tabla->actual->padre) {
        return 0;
    }

    tabla->actual = tabla->actual->padre;

    return 1;
}

//Devuelve el ambito actual
 
SimAmbito *sim_ambito_actual(
    const SimTabla *tabla
)
{
    return tabla ? tabla->actual : NULL;
}

//Busca un simbolo únicamente en un ambito
//HASH_FIND_STR es proporcionada por uthash.
 
static SimSimbolo *buscar_en(
    const SimAmbito *ambito,
    const char *nombre
)
{
    if (!ambito || !nombre) {
        return NULL;
    }

    SimSimbolo *resultado = NULL;

    HASH_FIND_STR(
        ambito->simbolos,
        nombre,
        resultado
    );

    return resultado;
}

//Busca solamente en el ambito actual
//Se utiliza para detectar redeclaraciones
 
SimSimbolo *sim_buscar_actual(
    const SimTabla *tabla,
    const char *nombre
)
{
    if (!tabla) {
        return NULL;
    }

    return buscar_en(tabla->actual, nombre);
}

//Busca desde el ambito actual hacia los padres
//Esto permite acceder a variables globales desde funciones y bloques internos
 
SimSimbolo *sim_buscar(
    const SimTabla *tabla,
    const char *nombre
)
{
    if (!tabla || !nombre) {
        return NULL;
    }

    for (
        SimAmbito *ambito = tabla->actual;
        ambito != NULL;
        ambito = ambito->padre
    ) {
        SimSimbolo *resultado = buscar_en(
            ambito,
            nombre
        );

        if (resultado) {
            return resultado;
        }
    }

    return NULL;
}

//Inserta un simbolo en el ambito actual
//Además comprueba si el nombre ya fue declarado en este mismo ambito
 
SimResultado sim_insertar(
    SimTabla *tabla,
    const char *nombre,
    SimClase clase,
    SimTipo tipo,
    size_t linea,
    size_t columna,
    SimSimbolo **creado
)
{
    if (creado) {
        *creado = NULL;
    }

    if (!tabla ||
        !tabla->actual ||
        !nombre ||
        !*nombre) {
        return SIM_ARGUMENTO_INVALIDO;
    }

    //Verificar duplicados
     
    if (sim_buscar_actual(tabla, nombre)) {
        return SIM_DUPLICADO;
    }

    //Crear simbolo
     
    SimSimbolo *simbolo = calloc(
        1,
        sizeof(*simbolo)
    );

    if (!simbolo) {
        return SIM_SIN_MEMORIA;
    }

    simbolo->nombre = copiar(nombre);

    if (!simbolo->nombre) {
        free(simbolo);
        return SIM_SIN_MEMORIA;
    }

    simbolo->clase = clase;
    simbolo->tipo = tipo;

    simbolo->linea = linea;
    simbolo->columna = columna;

    simbolo->ambito = tabla->actual;

    simbolo->almacen = SIM_ALMACEN_SIN_ASIGNAR;

    //Inserta mediante uthash.
    //La clave es el nombre del simbolo
     
    HASH_ADD_KEYPTR(
        hh,
        tabla->actual->simbolos,
        simbolo->nombre,
        strlen(simbolo->nombre),
        simbolo
    );

    //Mantiene también el orden global para poder imprimir todos los simbolos
    //aunque sus ambitos estén cerrados.
     
    if (tabla->ultimo) {
        tabla->ultimo->siguiente_orden = simbolo;
    } else {
        tabla->primero = simbolo;
    }

    tabla->ultimo = simbolo;

    tabla->cantidad++;

    if (creado) {
        *creado = simbolo;
    }

    return SIM_OK;
}

//Guarda dimensiones de arreglos y matrices.
//Un cero representa una dimensión cuyo tamaño todavia no se conoce
 
/*
 * Guarda las dimensiones de arreglos y matrices.
 *
 * Un cero representa una dimensión cuyo tamaño
 * todavía no se conoce.
 */
SimResultado sim_definir_dimensiones(
    SimSimbolo *simbolo,
    const size_t *longitudes,
    unsigned cantidad
)
{
    if (!simbolo || (cantidad > 0 && !longitudes)) {
        return SIM_ARGUMENTO_INVALIDO;
    }

    size_t *nuevas = NULL;

    if (cantidad > 0) {

        /*
         * Comprobar que el cálculo del tamaño
         * no provoque desbordamiento.
         */
        size_t n = (size_t)cantidad;

        /*
         * Reservar memoria para las dimensiones.
         */
        nuevas = malloc(n * sizeof(*nuevas));

        if (!nuevas) {
            return SIM_SIN_MEMORIA;
        }

        memcpy(
            nuevas,
            longitudes,
            n * sizeof(*nuevas)
        );
    }

    /*
     * Liberar las dimensiones anteriores.
     */
    free(simbolo->longitudes);

    simbolo->longitudes = nuevas;
    simbolo->dimensiones = cantidad;

    return SIM_OK;
}

//Guarda la ubicación de memoria.
//El calculo definitivo de direcciones corresponde al módulo memory.c
 
void sim_definir_ubicacion(
    SimSimbolo *simbolo,
    SimAlmacen almacen,
    long desplazamiento,
    size_t bytes
)
{
    if (!simbolo) {
        return;
    }

    simbolo->almacen = almacen;
    simbolo->desplazamiento = desplazamiento;
    simbolo->bytes = bytes;
}

//Obtiene el nombre legible del tipo
 
const char *sim_nombre_tipo(SimTipo tipo)
{
    switch (tipo) {
        case SIM_TIPO_INT:
            return "declare_int";

        case SIM_TIPO_BOOLEAN:
            return "declare_boolean";

        case SIM_TIPO_TEXT:
            return "declare_text";

        case SIM_TIPO_CHAR:
            return "declare_char";

        case SIM_TIPO_VOID:
            return "declare_infinite_void";

        case SIM_TIPO_ERROR:
            return "<error>";
    }

    return "<desconocido>";
}

//Obtiene el nombre legible de la clase
 
const char *sim_nombre_clase(SimClase clase)
{
    switch (clase) {
        case SIM_VARIABLE:
            return "variable";

        case SIM_CONSTANTE:
            return "constante";

        case SIM_FUNCION:
            return "funcion";

        case SIM_PARAMETRO:
            return "parametro";

        case SIM_LISTA:
            return "lista";

        case SIM_MODULO:
            return "modulo";

        case SIM_ETIQUETA:
            return "etiqueta";
    }

    return "<desconocida>";
}

//Imprime la tabla de simbolos.

//Se utiliza posteriormente con la opción -m
 
void sim_imprimir(
    const SimTabla *tabla,
    FILE *salida
)
{
    if (!tabla || !salida) {
        return;
    }

    fprintf(
        salida,
        "%-18s %-12s %-22s %-16s %-6s %-14s %-8s\n",
        "Nombre",
        "Clase",
        "Tipo",
        "Ambito",
        "Dims",
        "Ubicacion",
        "Bytes"
    );

    for (
        const SimSimbolo *s = tabla->primero;
        s != NULL;
        s = s->siguiente_orden
    ) {
        char ubicacion[64];

        if (s->almacen == SIM_ALMACEN_SIN_ASIGNAR) {
            snprintf(
                ubicacion,
                sizeof(ubicacion),
                "pendiente"
            );
        } else {
            snprintf(
                ubicacion,
                sizeof(ubicacion),
                "%ld (%d)",
                s->desplazamiento,
                (int)s->almacen
            );
        }

        fprintf(
            salida,
            "%-18s %-12s %-22s %-16s %-6u %-14s %-8zu\n",
            s->nombre,
            sim_nombre_clase(s->clase),
            sim_nombre_tipo(s->tipo),
            s->ambito->nombre,
            s->dimensiones,
            ubicacion,
            s->bytes
        );
    }
}

//INTEGRIDAD DE LA TABLA ANTES DE MEMORIA

int sim_verificar_pre_memoria(
    const SimTabla *tabla,
    FILE *diagnosticos
)
{
    if (!tabla ||
        !tabla->global ||
        !tabla->actual ||
        !tabla->ambitos_ultimo) {

        if (diagnosticos) {
            fputs(
                "tabla de simbolos no inicializada\n",
                diagnosticos
            );
        }

        return 0;
    }

    /*
     * El analisis semantico debe terminar
     * nuevamente en el ambito global.
     */
    if (tabla->actual != tabla->global) {

        if (diagnosticos) {
            fputs(
                "el analisis no termino "
                "en el ambito global\n",
                diagnosticos
            );
        }

        return 0;
    }

    /*
     * Comprobar la estructura basica
     * del ambito global.
     */
    if (tabla->global->padre != NULL ||
        tabla->global->nivel != 0) {

        if (diagnosticos) {
            fputs(
                "ambito global invalido\n",
                diagnosticos
            );
        }

        return 0;
    }

    /*
     * Verificar relaciones entre ambitos.
     */
    for (
        const SimAmbito *ambito = tabla->global;
        ambito;
        ambito = ambito->siguiente_creacion
    ) {

        if (!ambito->nombre ||
            !ambito->nombre[0]) {

            if (diagnosticos) {
                fputs(
                    "ambito sin nombre\n",
                    diagnosticos
                );
            }

            return 0;
        }

        if (ambito != tabla->global) {

            if (!ambito->padre ||
                ambito->nivel !=
                    ambito->padre->nivel + 1) {

                if (diagnosticos) {
                    fprintf(
                        diagnosticos,
                        "jerarquia invalida "
                        "en ambito '%s'\n",
                        ambito->nombre
                    );
                }

                return 0;
            }
        }
    }

    size_t cantidad = 0;

    const SimSimbolo *ultimo = NULL;

    /*
     * Recorrer todos los simbolos,
     * incluso los pertenecientes
     * a ambitos ya cerrados.
     */
    for (
        const SimSimbolo *simbolo =
            tabla->primero;
        simbolo;
        simbolo = simbolo->siguiente_orden
    ) {

        if (!simbolo->nombre ||
            !simbolo->nombre[0] ||
            !simbolo->ambito) {

            if (diagnosticos) {
                fputs(
                    "simbolo incompleto\n",
                    diagnosticos
                );
            }

            return 0;
        }

        /*
         * El simbolo debe encontrarse
         * en la tabla hash de su ambito.
         */
        SimSimbolo *encontrado = NULL;

        HASH_FIND_STR(
            simbolo->ambito->simbolos,
            simbolo->nombre,
            encontrado
        );

        if (encontrado != simbolo) {

            if (diagnosticos) {
                fprintf(
                    diagnosticos,
                    "simbolo '%s' no coincide "
                    "con su ambito\n",
                    simbolo->nombre
                );
            }

            return 0;
        }

        /*
         * Verificar coherencia entre
         * numero de dimensiones y
         * arreglo de longitudes.
         */
        if ((simbolo->dimensiones == 0 &&
             simbolo->longitudes != NULL) ||
            (simbolo->dimensiones > 0 &&
             simbolo->longitudes == NULL)) {

            if (diagnosticos) {
                fprintf(
                    diagnosticos,
                    "dimensiones inconsistentes "
                    "en '%s'\n",
                    simbolo->nombre
                );
            }

            return 0;
        }

        /*
         * Ninguna ubicacion fisica debe
         * haber sido asignada.
         */
        if (simbolo->almacen !=
                SIM_ALMACEN_SIN_ASIGNAR ||
            simbolo->desplazamiento != 0 ||
            simbolo->bytes != 0) {

            if (diagnosticos) {
                fprintf(
                    diagnosticos,
                    "el simbolo '%s' ya tiene "
                    "memoria asignada\n",
                    simbolo->nombre
                );
            }

            return 0;
        }

        /*
         * Un simbolo de clase SIM_PARAMETRO
         * siempre debe estar marcado
         * como parametro.
         */
        if (simbolo->clase == SIM_PARAMETRO &&
            !simbolo->es_parametro) {

            if (diagnosticos) {
                fprintf(
                    diagnosticos,
                    "parametro '%s' sin "
                    "clasificacion correcta\n",
                    simbolo->nombre
                );
            }

            return 0;
        }

        ultimo = simbolo;
        ++cantidad;
    }

    /*
     * Comprobar la cantidad de simbolos
     * almacenados.
     */
    if (cantidad != tabla->cantidad ||
        ultimo != tabla->ultimo) {

        if (diagnosticos) {
            fputs(
                "cantidad u orden de "
                "simbolos inconsistente\n",
                diagnosticos
            );
        }

        return 0;
    }

    if (diagnosticos) {
        fprintf(
            diagnosticos,
            "[OK] Integridad de tabla: "
            "%zu simbolos, sin offsets\n",
            cantidad
        );
    }

    return 1;
}

//Libera la tabla completa
//HASH_DEL elimina cada simbolo de uthash antes de liberar su memoria
 
void sim_liberar(SimTabla *tabla)
{
    if (!tabla) {
        return;
    }

    //Elimina todos los simbolos de sus respectivas tablas hash
     
    SimSimbolo *simbolo = tabla->primero;

    while (simbolo) {
        SimSimbolo *siguiente =
            simbolo->siguiente_orden;

        HASH_DEL(
            simbolo->ambito->simbolos,
            simbolo
        );

        free(simbolo->nombre);
        free(simbolo->longitudes);
        free(simbolo);

        simbolo = siguiente;
    }

    //Libera ambitos
     
    SimAmbito *ambito = tabla->global;

    while (ambito) {
        SimAmbito *siguiente =
            ambito->siguiente_creacion;

        free(ambito->nombre);
        free(ambito);

        ambito = siguiente;
    }

    //Restablece tabla
     
    *tabla = (SimTabla){0};
}