#include "ir_spilling_marco.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Comprueba que las ranuras nuevas empiecen despues del marco existente. */
static void prueba_reserva_sin_colisiones(void) {
    IrSpillingMarco marco = {0};
    size_t offset = 0;
    assert(ir_spilling_calcular_marco(17, 3, &marco) == 0);
    assert(marco.inicio_spilling == 20);
    assert(marco.bytes_marco == 32);
    assert(ir_spilling_offset(&marco, "$spill0", &offset) == 0 && offset == 20);
    assert(ir_spilling_offset(&marco, "$spill2", &offset) == 0 && offset == 28);
    assert(ir_spilling_offset(&marco, "$spill3", &offset) == 2);
    assert(ir_spilling_offset(&marco, "$spill-1", &offset) == 2);
    assert(ir_spilling_offset(&marco, "$spill999999999999999999999", &offset) == 2);
}

/* El ISA usa cargai y guardap con la base sp almacenada en x2. */
static void prueba_emision_de_memoria(void) {
    IrSpillingMarco marco = {0};
    assert(ir_spilling_calcular_marco(16, 2, &marco) == 0);
    IrInstr carga = {0};
    carga.op = IR_SPILL_LOAD;
    carga.a = "$spill1";
    IrInstr escritura = {0};
    escritura.op = IR_SPILL_STORE;
    escritura.dst = "$spill0";
    FILE *f = tmpfile();
    assert(f);
    assert(ir_spilling_emitir_acceso(f, &marco, &carga, 5) == 0);
    assert(ir_spilling_emitir_acceso(f, &marco, &escritura, 6) == 0);
    rewind(f);
    char buffer[128] = {0};
    assert(fread(buffer, 1, sizeof(buffer) - 1, f) > 0);
    assert(strcmp(buffer, "cargai x5, x2, 20\nguardap x2, x6, 16\n") == 0);
    fclose(f);
}

/* Los marcos demasiado grandes deben rechazarse, no truncarse. */
static void prueba_limites(void) {
    IrSpillingMarco marco = {0};
    assert(ir_spilling_calcular_marco(4, 255, &marco) == 0);
    assert(ir_spilling_calcular_marco(4, 256, &marco) == 2);
    assert(ir_spilling_calcular_marco(1024, 1, &marco) == 2);
    assert(ir_spilling_calcular_marco(SIZE_MAX, 1, &marco) == 2);
    assert(ir_spilling_calcular_marco(0, 1, &marco) == 2);
    assert(ir_spilling_calcular_marco(16, 2, &marco) == 0);
    IrInstr carga = {0};
    carga.op = IR_SPILL_LOAD;
    carga.a = "$spill0";
    FILE *f = tmpfile();
    assert(f);
    assert(ir_spilling_emitir_acceso(f, &marco, &carga, 2) == 2);
    assert(ir_spilling_emitir_acceso(f, &marco, &carga, 32) == 2);
    assert(ftell(f) == 0);
    fclose(f);
}

/* Ejecuta las pruebas del calculo de marco y de acceso LSU. */
int main(void) {
    prueba_reserva_sin_colisiones();
    prueba_emision_de_memoria();
    prueba_limites();
    puts("OK: spilling - marcos, offsets y memoria VLIW");
    return 0;
}
