# Habilita la visualizacion del spilling desde la CLI.
# La compilacion normal sigue utilizando el backend actual sin spilling.
SRC += src/ir_spilling.c
balc: include/ir_spilling.h src/ir_spilling.c

.PHONY: test-ir-spilling-cli
test-ir-spilling-cli: balc
	./balc --ir-spilling examples/factorial.bal
	! ./balc --ir-spilling --ir examples/factorial.bal

# La prueba de CLI se ejecuta tambien con make test.
test: test-ir-spilling-cli
