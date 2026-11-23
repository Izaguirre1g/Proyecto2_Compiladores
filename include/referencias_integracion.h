#ifndef REFERENCIAS_INTEGRACION_H
#define REFERENCIAS_INTEGRACION_H

#include "generacion_adaptador.h"
#include "resolucion_referencias.h"

/* 3B-D3E: conecta el adaptador semantico con la tabla de destinos.
 * Las direcciones de RR deben provenir del layout externo de bundles.
 * Ni esta funcion ni RR calculan posiciones de instrucciones.
 * Retorna 0 en exito; 1 si faltan datos o falla la generacion.
 * No emite etiquetas, bundles ni codigo binario.
 */
int ri_generar_funcion(const AstNodo *funcion, const SimTabla *simbolos,
                       size_t bytes_marco, const RrTabla *destinos,
                       FILE *salida, FILE *diagnosticos);

/* Variante optativa para el ABI de referencias int 1D; mismos recursos. */
int ri_generar_funcion_referencias(const AstNodo *funcion,const SimTabla *tabla,
    size_t marco,const RrTabla *destinos,FILE *salida,FILE *diagnosticos);
#endif
