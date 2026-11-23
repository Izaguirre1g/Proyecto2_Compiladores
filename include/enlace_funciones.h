#ifndef ENLACE_FUNCIONES_H
#define ENLACE_FUNCIONES_H

#include "ast.h"
#include "resolucion_saltos.h"
#include "symbol_table.h"

/* Genera todas las funciones del programa y fija sus direcciones.
 *
 * Las llamadas cargan en x13 la direccion absoluta de la funcion, pero esa
 * direccion depende del layout de bundles, que a su vez depende del codigo
 * generado. Se itera: generar con direcciones supuestas, armar bundles con
 * rs_resolver_etiquetas, leer el PC real de cada funcion y repetir hasta que
 * coinciden. Se parte de "todas en inicio", el valor minimo: las direcciones
 * y el tamano del codigo solo crecen entre vueltas, asi que converge.
 *
 * Las funciones quedan en orden del fuente desde inicio; cada una empieza en
 * la etiqueta "__fn_<nombre>". El marco de cada funcion se calcula con los
 * desplazamientos ya asignados por mem_asignar_proyecto.
 *
 * Arranque (si arranque no es NULL), en inicio y antes de las funciones:
 *   gp = arranque->gp; sp = arranque->sp;
 *   inicializa en orden las constantes globales enteras (guardap sobre gp);
 *   x13 = direccion de main; syer x1, x13, 0;
 *   __arranque_fin: sye x0, __arranque_fin   (salto estable: fin del programa)
 * gp debe ser el gp_base usado en mem_asignar_proyecto. La pila crece hacia
 * abajo: sp es el primer byte libre por encima de ella (p. ej. 0x10000).
 * main debe existir. Las constantes globales solo se inicializan aqui.
 *
 * funciones: si no es NULL (tabla inicializada y vacia), recibe la direccion
 * final de cada funcion como RR_FUNCION.
 * Devuelve 0 en exito y 1 en error; en error no escribe nada en salida. */
typedef struct {
    uint32_t gp;
    uint32_t sp;
} EfArranque;

typedef struct {
    RsResumen saltos; /* De la ultima vuelta. */
    size_t funciones;
    size_t vueltas;
} EfResumen;

int ef_enlazar(const Ast *ast, const SimTabla *tabla, uint32_t inicio,
               const EfArranque *arranque, RrTabla *funciones,
               FILE *salida, FILE *diagnosticos, EfResumen *resumen);

/* Opt-in de referencias int 1D; conserva arranque y enlace iterativo. */
int ef_enlazar_referencias(const Ast *ast,const SimTabla *tabla,uint32_t inicio,
    const EfArranque *arranque,RrTabla *funciones,FILE *salida,FILE *diagnosticos,EfResumen *resumen);
#endif
