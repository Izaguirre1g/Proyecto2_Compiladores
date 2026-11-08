/* Este archivo contiene definiciones internas 
/para la generación de código de funciones. 
No debe ser incluido por código externo al módulo de generación de funciones. 
Conexion entre generacion_funciones.c y generacion_funciones.h.
*/
#ifndef GENERACION_INTERNA_H
#define GENERACION_INTERNA_H

#include "generacion_funciones.h"

/* Contrato privado de funciones/control. Una instancia por funcion:
 * comparte destinos, referencias y contador de etiquetas. No posee el AST. */
typedef struct {
    FILE *salida;
    FILE *diagnosticos;
    GbResolver resolver;
    GfResolverDestino destino;
    void *datos;
    size_t bytes_marco;
    unsigned nivel;
    const char *funcion;
    unsigned etiquetas;
    unsigned bloques;
    GbResolverArreglo arreglos; /* NULL: sin arreglos. */
} GfContexto;

int gf_fallo(GfContexto *c, const AstNodo *n, const char *mensaje);
int gf_condicion(GfContexto *c, const AstNodo *n);
/* Expresion entera (admite llamadas) en x4; reserva = bytes ya apilados. */
int gf_expresion(GfContexto *c, const AstNodo *n, size_t reserva);
int gf_sentencia(GfContexto *c, const AstNodo *n, int es_void, int *retorna);
int gc_bloque(GfContexto *c, const AstNodo *bloque, int es_void, int *retorna);

#endif
