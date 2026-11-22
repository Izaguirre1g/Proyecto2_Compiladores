# Contrato entre offsets definitivos de tabla de simbolos y ranuras de spilling.
# No interviene en el compilador productivo hasta que el lowering BAL lo consuma.

.PHONY: test-spilling-memoria clean-spilling-memoria

tests/test_ir_spilling_memoria: tests/test_ir_spilling_memoria.c \
		src/ir_spilling_memoria.c src/ir_spilling_backend.c \
		src/ir_spilling_marco.c src/Generacion_Codigo/funciones_marco.c \
		src/Analisis_Semantico/memory.c src/Analisis_Semantico/symbol_table.c \
		include/ir_spilling_memoria.h include/ir_spilling_backend.h \
		include/ir_spilling_marco.h include/memory.h include/symbol_table.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ir_spilling_memoria.c \
		src/ir_spilling_memoria.c src/ir_spilling_backend.c \
		src/ir_spilling_marco.c src/Generacion_Codigo/funciones_marco.c \
		src/Analisis_Semantico/memory.c src/Analisis_Semantico/symbol_table.c -o $@

test-spilling-memoria: tests/test_ir_spilling_memoria
	./tests/test_ir_spilling_memoria

test-spilling: test-spilling-memoria
test: test-spilling-memoria

# Exportar la API al ejecutable, aunque no se active el lowering de variables.
SRC += src/ir_spilling_memoria.c
balc: src/ir_spilling_memoria.c include/ir_spilling_memoria.h

clean: clean-spilling-memoria

clean-spilling-memoria:
	rm -f tests/test_ir_spilling_memoria
