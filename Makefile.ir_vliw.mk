# Integracion incremental IR -> spilling -> operaciones fisicas -> cadena VLIW.
# Python solo orquesta el compilador C y ejecuta los tests; no requiere Flex/Bison.
.PHONY: test-ir-vliw

test-ir-vliw:
	python3 tests/test_ir_vliw.py

test: test-ir-vliw

# El CLI invoca el mismo productor probado; no cambia el backend por defecto.
VLIW_BRIDGE_SRC := src/ir_vliw.c src/ir_spilling_marco.c src/ir_spilling_backend.c \
       src/Calendarizacion_VLIW/vliw_proyeccion_fisica.c \
       src/Calendarizacion_VLIW/vliw_dag_fisico.c \
       src/Calendarizacion_VLIW/vliw_cfg_fisico.c \
       src/Calendarizacion_VLIW/vliw_metadatos.c \
       src/Simulador_VLIW/sim_decode.c \
       src/Calendarizacion_VLIW/vliw_integracion.c \
       src/Calendarizacion_VLIW/vliw_cadena_fisica.c \
       src/Calendarizacion_VLIW/vliw_scheduler.c \
       src/Calendarizacion_VLIW/vliw_emisor.c \
       src/Calendarizacion_VLIW/vliw_visualizador.c
SRC += $(VLIW_BRIDGE_SRC)
# El SRC de la receta de balc es recursivo, pero sus prerequisitos fueron
# expandidos antes del include: agregarlos tambien para recompilar en cambios.
balc: $(VLIW_BRIDGE_SRC) include/ir_vliw.h

.PHONY: test-ir-vliw-cli
# Requiere Flex/Bison y ejecutable BAL: se usa en los puestos del equipo.
test-ir-vliw-cli: balc test-ir-vliw-flags
	sh tests/test_ir_vliw_cli.sh

test: test-ir-vliw-cli

# Prueba de opciones del CLI sin Flex/Bison; uthash-dev sigue requerido.
.PHONY: test-ir-vliw-flags
tests/test_ir_vliw_cli_flags: tests/test_ir_vliw_cli_flags.c src/Linea_Comandos/cli.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
	    tests/test_ir_vliw_cli_flags.c -Wl,--gc-sections -o $@
test-ir-vliw-flags: tests/test_ir_vliw_cli_flags
	./tests/test_ir_vliw_cli_flags
