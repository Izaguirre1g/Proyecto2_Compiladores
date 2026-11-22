#ifndef GENERACION_ADAPTADOR_H
#define GENERACION_ADAPTADOR_H

#include "generacion_funciones.h"
#include "symbol_table.h"

/* 3B-D3C: integra simbolos ya ubicados con el backend 3B-D2.
 * La tabla debe haber pasado por mem_asignar_proyecto().
 * bytes_marco corresponde a la funcion seleccionada, NO al maximo global.
 * El mapa de codigo pertenece al llamador: destinos no se inventan.
 * Variables/parametros enteros de la funcion y sus bloques, y constantes
 * enteras globales. Conserva el alcance y orden de declaracion del AST.
 * AST y tabla deben ser los mismos que valido semantica y permanecer vivos;
 * el adaptador no posee sus datos. Tablas manuales sin enlaces solo admiten
 * el ambito de funcion original. No produce bundles ni binarios.
 * 0 exito, 1 generacion o referencias no soportadas.
 */
int gd_generar_funcion(const AstNodo *funcion, const SimTabla *tabla,
                       size_t bytes_marco, GfResolverDestino destino,
                       void *contexto_destinos, FILE *salida,
                       FILE *diagnosticos);

/* Identidad semántica del uso, con visibilidad y enlaces AST/ámbito reales.
 * Préstamo de tabla; no requiere ni asigna memoria. NULL si no se acredita. */
const SimSimbolo *gd_resolver_simbolo(const SimTabla *tabla,
    const char *funcion, const AstNodo *identificador);

/* Opt-in para el layout mem_asignar_proyecto_referencias: arreglos int 1D
 * prestados por dirección; sin descriptor de longitud ni copia de elementos. */
int gd_generar_funcion_referencias(const AstNodo *funcion,const SimTabla *tabla,
    size_t bytes_marco,GfResolverDestino destino,void *contexto,FILE *salida,FILE *diagnosticos);
#endif
