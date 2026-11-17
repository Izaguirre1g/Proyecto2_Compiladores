#ifndef GENERACION_BASICA_H
#define GENERACION_BASICA_H

#include "ast.h"

#include <stdint.h>

typedef enum { GB_ENTERO, GB_BOOLEANO } GbTipo;
typedef struct {
    GbTipo tipo;
    unsigned base; /* x0, x2 (sp) o x3 (gp); la integracion los inicializa. */
    int desplazamiento; /* Bytes, multiplo de 4, entre -1024 y 1023. */
    int escribible;
} GbReferencia;

/* El nodo permite resolver el nombre en su ambito. Devuelve 1 si existe.
 * La tabla de simbolos conserva la propiedad de sus datos.
 */
typedef int (*GbResolver)(void *datos, const AstNodo *identificador,
                          GbReferencia *referencia);

/* Recibe una expresion pura, asignacion o declaracion escalar inicializada.
 * Emite instrucciones y etiquetas internas; NO bundles ni un programa ejecutable.
 * Usa x4-x7; multiplicacion tambien modifica x12-x15 y emite un bucle.
 * Division/residuo/potencia tambien modifican x8-x15 y emiten saltos.
 * Una expresion deja el resultado en x4.
 * El backend reserva los prefijos __gb_mul_, __gb_div_ y __gb_pow_ y coloca NOPs en los destinos.
 * Retorna 0 en exito y 1 en error, con diagnostico opcional.
 * No escribe instrucciones si falla la validacion/generacion. Un error de E/S
 * al copiar al destino puede dejar una salida parcial, que debe descartarse.
 */
int gb_generar(const AstNodo *nodo, GbResolver resolver, void *datos,
               FILE *salida, FILE *diagnosticos);

/* Arreglo estatico de enteros de 4 bytes, en orden por filas: el elemento
 * [i][j] de una matriz NxM esta en desplazamiento + 4*(i*M + j). */
#define GB_MAX_DIMENSIONES 4
/* Descriptor optativo: el home slot sp+desplazamiento contiene la dirección. */
#define GB_ARREGLO_REFERENCIA_SP 34u
typedef struct {
    unsigned base;          /* x2 (sp) o x3 (gp). */
    int desplazamiento;     /* Byte del primer elemento. */
    unsigned dimensiones;   /* 1..GB_MAX_DIMENSIONES */
    uint32_t longitudes[GB_MAX_DIMENSIONES];
} GbArreglo;

/* Devuelve 1 si el identificador es un arreglo estatico de enteros. */
typedef int (*GbResolverArreglo)(void *datos, const AstNodo *identificador,
                                 GbArreglo *arreglo);

/* Igual que gb_generar y ademas admite, con un resolvedor de arreglos:
 * lectura de elementos (a[i], m[i][j]) en expresiones, asignacion a un
 * elemento y declaracion con literal ([[1, 2], [3, 4]]) o sin inicializar.
 * Los indices se evaluan en tiempo de ejecucion y NO se comprueban sus
 * limites (la semantica rechaza los indices constantes fuera de rango).
 * La direccion usa los temporales del nivel: a[i] en xR necesita xR+1. */
int gb_generar_arreglos(const AstNodo *nodo, GbResolver resolver,
                        GbResolverArreglo arreglos, void *datos,
                        FILE *salida, FILE *diagnosticos);

/* Adaptador IR: expande la aritmética existente, con operandos ya evaluados
 * en x4/x5 y resultado x4. Solo pitagoras/euclides/euler/descartes.
 * Conserva exactamente errores/overflow del backend; clobbers x4..x15.
 * El flujo contiene etiquetas, no bundles; el productor debe estructurarlas. */
int gb_expandir_aritmetica(const char *operador, FILE *salida, FILE *diag);

#endif
