/* Driver independiente para las pruebas del productor O2 completo.
 * Se separa del driver de renombramiento para no mezclar sus modos CLI.
 * La compilación envuelve malloc/calloc y permite inyectar fallos durante
 * ir_vliw_programa(), verificando que no se publique estado parcial. */
#include "ir_vliw_programa.h"
#include "semantic_modules.h"
#include "memory.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long cuenta = -1;
void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t s);
void *__wrap_malloc(size_t n)
{
    if (cuenta == 0) return NULL;
    if (cuenta > 0) --cuenta;
    return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t s)
{
    if (cuenta == 0) return NULL;
    if (cuenta > 0) --cuenta;
    return __real_calloc(n, s);
}

/* La tabla se prepara una vez. Cada fallo afecta únicamente la construcción
 * privada: ninguna salida parcial ni mutación del AST/tabla queda publicada.
 * El caso exitoso usa ocho registros, independiente del CLI que usa tres. */
static int probar_productor(void *ctx, const Ast *ast, SimTabla *tabla,
                            FILE *diag)
{
    MemProyectoMapa mapa = {0};
    assert(mem_asignar_proyecto_referencias(tabla, 16384, 32768, 32768,
                                            &mapa) == MEM_PROYECTO_OK);
    SimTabla tabla_antes = *tabla;
    Ast ast_antes = *ast;
    IrVliwPrograma out = {0};

    assert(ir_vliw_programa(NULL, tabla, 8, &out, NULL) == 2);
    out.pc_fin = 1;
    assert(ir_vliw_programa(ast, tabla, 8, &out, NULL) == 2);
    out.pc_fin = 0;

    int exito = 0;
    long limite = ctx ? 5000 : 1;
    for (long i = 0; i < limite; ++i) {
        cuenta = ctx ? i : -1;
        int rc = ir_vliw_programa(ast, tabla, 8, &out, diag);
        cuenta = -1;
        assert(!memcmp(tabla, &tabla_antes, sizeof *tabla));
        assert(!memcmp(ast, &ast_antes, sizeof *ast));
        if (rc == 0) {
            if (ctx) printf("fallos_reserva_productor=%ld\n", i);
            assert(out.cadena && out.reporte_json);
            ir_vliw_programa_liberar(&out);
            exito = 1;
            break;
        }
        assert(!out.cadena && !out.reporte_json && !out.original.codigo &&
               !out.optimizada.codigo && !out.spilling.codigo);
    }
    assert(exito);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) return 64;
    int recursos;
    if (!strcmp(argv[1], "programa")) recursos = 0;
    else if (!strcmp(argv[1], "recursos_programa")) recursos = 1;
    else return 64;

    FILE *diag = tmpfile();
    assert(diag);
    int rc = sem_visitar_proyecto(argv[2], diag, probar_productor,
                                  recursos ? &recursos : NULL);
    fclose(diag);
    return rc;
}
