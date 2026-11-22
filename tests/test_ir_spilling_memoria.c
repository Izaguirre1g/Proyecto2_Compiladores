#include "ir_spilling_memoria.h"
#include "memory.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static SimSimbolo *agregar(SimTabla *t, const char *nombre,
                           SimClase clase, SimTipo tipo)
{
    SimSimbolo *s = NULL;
    assert(sim_insertar(t, nombre, clase, tipo, 1, 1, &s) == SIM_OK);
    assert(s);
    return s;
}

static void comparar(FILE *f, const char *esperado)
{
    char buffer[4096] = {0};
    rewind(f);
    size_t n = fread(buffer, 1, sizeof(buffer)-1, f);
    assert(n < sizeof(buffer)-1);
    assert(!strcmp(buffer, esperado));
}

int main(void)
{
    SimTabla tabla = {0};
    assert(sim_inicializar(&tabla) == SIM_OK);
    SimSimbolo *global = agregar(&tabla, "global_int", SIM_VARIABLE, SIM_TIPO_INT);
    (void)agregar(&tabla, "calcular", SIM_FUNCION, SIM_TIPO_INT);
    (void)agregar(&tabla, "main", SIM_FUNCION, SIM_TIPO_VOID);

    assert(sim_entrar_ambito(&tabla, "calcular") == SIM_OK);
    SimSimbolo *params[5];
    for (unsigned i=0; i<5; ++i) {
        char nombres[][4] = {"p0", "p1", "p2", "p3", "p4"};
        params[i] = agregar(&tabla, nombres[i], SIM_PARAMETRO,
                            i == 1 ? SIM_TIPO_BOOLEAN :
                            i == 2 ? SIM_TIPO_CHAR : SIM_TIPO_INT);
        params[i]->es_parametro = 1;
    }
    SimSimbolo *caracter = agregar(&tabla, "caracter", SIM_VARIABLE, SIM_TIPO_CHAR);
    SimSimbolo *entero = agregar(&tabla, "entero", SIM_VARIABLE, SIM_TIPO_INT);
    SimSimbolo *array = agregar(&tabla, "array", SIM_VARIABLE, SIM_TIPO_INT);
    size_t dimensiones[] = {3};
    assert(sim_definir_dimensiones(array, dimensiones, 1) == SIM_OK);
    assert(sim_entrar_ambito(&tabla, "if_anidado") == SIM_OK);
    SimSimbolo *otro = agregar(&tabla, "otro", SIM_VARIABLE, SIM_TIPO_INT);
    assert(sim_salir_ambito(&tabla));
    assert(sim_salir_ambito(&tabla));

    assert(sim_entrar_ambito(&tabla, "main") == SIM_OK);
    SimSimbolo *m = agregar(&tabla, "resultado", SIM_VARIABLE, SIM_TIPO_INT);
    assert(sim_salir_ambito(&tabla));

    MemProyectoMapa mapa = {0};
    assert(mem_asignar_proyecto(&tabla, 0x4000, 0x10000, 512, &mapa)
           == MEM_PROYECTO_OK);
    assert(mapa.funciones == 2 && mapa.marco_maximo_bytes == 48);
    assert(global->almacen == SIM_ALMACEN_GLOBAL && global->desplazamiento == 0);
    for (size_t i=0; i<5; ++i) {
        assert(params[i]->almacen == SIM_ALMACEN_PARAMETRO);
        assert(params[i]->desplazamiento == (long)(4 + 4*i));
    }
    assert(caracter->desplazamiento == 24 && caracter->bytes == 1);
    assert(entero->desplazamiento == 28 && entero->bytes == 4);
    assert(array->desplazamiento == 32 && array->bytes == 12);
    assert(otro->desplazamiento == 44 && otro->bytes == 4);
    assert(m->desplazamiento == 4 && m->bytes == 4);

    IrSpillingBackend calcular = {0}, main_b = {0};
    assert(ir_spilling_backend_desde_tabla(&tabla, "calcular", 2, &calcular) == 0);
    assert(calcular.numero_parametros == 5);
    assert(calcular.marco.bytes_ocupados == 48);
    assert(calcular.marco.inicio_spilling == 48);
    assert(calcular.marco.bytes_marco == 56);
    assert(ir_spilling_backend_desde_tabla(&tabla, "main", 1, &main_b) == 0);
    assert(main_b.numero_parametros == 0);
    assert(main_b.marco.inicio_spilling == 8);
    assert(main_b.marco.bytes_marco == 12);

    size_t offset = 0;
    assert(ir_spilling_offset(&calcular.marco, "$spill0", &offset) == 0 && offset == 48);
    assert(ir_spilling_offset(&calcular.marco, "$spill1", &offset) == 0 && offset == 52);
    assert(ir_spilling_offset(&main_b.marco, "$spill0", &offset) == 0 && offset == 8);
    assert(ir_spilling_offset(&main_b.marco, "$spill1", &offset) == 2);

    /* El argumento p4 entra por pila a partir del MARCO AMPLIADO, no del
     * marco pre-spilling; de lo contrario el call ABI leeria otro dato. */
    MemAbiArgumento argumento = {0};
    assert(mem_abi_argumento(4, calcular.marco.bytes_marco, &argumento) == MEM_ABI_OK);
    assert(!argumento.en_registro && argumento.offset_entrada == 56);
    assert(mem_abi_argumento(5, calcular.marco.bytes_marco, &argumento) == MEM_ABI_OK);
    assert(argumento.offset_entrada == 60);

    FILE *f = tmpfile(); assert(f);
    assert(ir_spilling_backend_prologo(f, &main_b) == 0);
    comparar(f, "restai x2, x2, 12\n"
                "guardap x2, x1, 0\n");
    fclose(f);
    f = tmpfile(); assert(f);
    IrInstr escribir = {0}; escribir.op = IR_SPILL_STORE; escribir.dst = "$spill1";
    IrInstr leer = {0}; leer.op = IR_SPILL_LOAD; leer.a = "$spill0";
    assert(ir_spilling_backend_acceso(f, &calcular, &escribir, 17) == 0);
    assert(ir_spilling_backend_acceso(f, &calcular, &leer, 18) == 0);
    comparar(f, "guardap x2, x17, 52\n"
                "cargai x18, x2, 48\n");
    fclose(f);
    f = tmpfile(); assert(f);
    assert(ir_spilling_backend_epilogo(f, &calcular) == 0);
    comparar(f, "cargai x1, x2, 0\n"
                "sumai x2, x2, 56\n"
                "syer x0, x1, 0\n");
    fclose(f);

    /* Todos los rechazos dejan la salida intacta: primero ubicar symbols,
     * despues entregar el marco, sin modificaciones parciales. */
    IrSpillingBackend rechazo = main_b;
    assert(ir_spilling_backend_desde_tabla(&tabla, "desconocida", 1, &rechazo) == 2);
    assert(rechazo.marco.bytes_marco == 12);
    assert(ir_spilling_backend_desde_tabla(&tabla, "calcular", 250, &rechazo) == 2);
    assert(rechazo.marco.bytes_marco == 12);
    long old = entero->desplazamiento;
    entero->desplazamiento = 24; /* pisa char y rompe alineacion */
    assert(ir_spilling_backend_desde_tabla(&tabla, "calcular", 1, &rechazo) == 2);
    assert(rechazo.marco.bytes_marco == 12);
    entero->desplazamiento = old;
    size_t old_bytes = array->bytes;
    array->bytes = 4; /* se exige tamano real de arreglo asignado */
    assert(ir_spilling_backend_desde_tabla(&tabla, "calcular", 1, &rechazo) == 2);
    assert(rechazo.marco.bytes_marco == 12);
    array->bytes = old_bytes;
    SimAlmacen old_almacen = params[0]->almacen;
    params[0]->almacen = SIM_ALMACEN_SIN_ASIGNAR;
    assert(ir_spilling_backend_desde_tabla(&tabla, "calcular", 1, &rechazo) == 2);
    params[0]->almacen = old_almacen;
    assert(ir_spilling_backend_desde_tabla(&tabla, "calcular", 2, &rechazo) == 0);
    assert(rechazo.marco.bytes_marco == 56);

    sim_liberar(&tabla);
    puts("OK contrato MEM->spilling: globales, 2 funciones, ABI 5 params, "
         "locales anidados, offsets, accesos LSU y rechazos");
    return 0;
}
