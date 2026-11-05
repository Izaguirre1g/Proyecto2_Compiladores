#include "ir_spilling_backend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Compara el texto realmente escrito por el backend en un archivo temporal. */
static void verificar_texto(FILE *archivo, const char *esperado) {
    char contenido[1024] = {0};
    rewind(archivo);
    size_t leidos = fread(contenido, 1, sizeof(contenido) - 1, archivo);
    assert(leidos < sizeof(contenido) - 1);
    assert(strcmp(contenido, esperado) == 0);
}

/* La reserva original de 16 bytes crece 8, sin mover parametros ni ra.
 * El prologo y epilogo deben compensarse exactamente. */
static void prueba_marco_ampliado(void) {
    IrSpillingBackend backend = {0};
    assert(ir_spilling_backend_preparar(16, 2, 2, &backend) == 0);
    assert(backend.marco.inicio_spilling == 16);
    assert(backend.marco.bytes_marco == 24);
    FILE *f = tmpfile();
    assert(f);
    assert(ir_spilling_backend_prologo(f, &backend) == 0);
    verificar_texto(f, "restai x2, x2, 24\n"
                      "guardap x2, x1, 0\n"
                      "guardap x2, x8, 4\n"
                      "guardap x2, x9, 8\n");
    fclose(f);
    f = tmpfile();
    assert(f);
    assert(ir_spilling_backend_epilogo(f, &backend) == 0);
    verificar_texto(f, "cargai x1, x2, 0\n"
                      "sumai x2, x2, 24\n"
                      "syer x0, x1, 0\n");
    fclose(f);
}

/* Las ranuras se emiten usando el SP real del marco ampliado. */
static void prueba_acceso_a_ranuras(void) {
    IrSpillingBackend backend = {0};
    assert(ir_spilling_backend_preparar(16, 2, 1, &backend) == 0);
    IrInstr carga = {0};
    carga.op = IR_SPILL_LOAD;
    carga.a = "$spill1";
    IrInstr almacenamiento = {0};
    almacenamiento.op = IR_SPILL_STORE;
    almacenamiento.dst = "$spill0";
    FILE *f = tmpfile();
    assert(f);
    assert(ir_spilling_backend_acceso(f, &backend, &carga, 5) == 0);
    assert(ir_spilling_backend_acceso(f, &backend, &almacenamiento, 6) == 0);
    verificar_texto(f, "cargai x5, x2, 20\n"
                      "guardap x2, x6, 16\n");
    fclose(f);
}

/* Rechaza marcos y parametros inconsistentes antes de generar instrucciones. */
static void prueba_rechazos(void) {
    IrSpillingBackend backend = {0};
    assert(ir_spilling_backend_preparar(16, 1, 4, &backend) == 2);
    assert(ir_spilling_backend_preparar(15, 1, 1, &backend) == 2);
    assert(ir_spilling_backend_preparar(1024, 1, 0, &backend) == 2);
    assert(ir_spilling_backend_preparar(16, 0, 0, &backend) == 0);
    assert(backend.marco.bytes_marco == 16);
    assert(ir_spilling_backend_prologo(NULL, &backend) == 2);
    assert(ir_spilling_backend_epilogo(NULL, &backend) == 2);
}

int main(void) {
    prueba_marco_ampliado();
    prueba_acceso_a_ranuras();
    prueba_rechazos();
    puts("OK: spilling - prologo, epilogo y LSU reales");
    return 0;
}
