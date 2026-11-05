# Reglas de prueba del spilling, separadas para no reemplazar el Makefile del equipo.
# Incluya este archivo al final de Makefile con: -include Makefile.spilling.mk

.PHONY: test-spilling clean-spilling

# Compila la pasada de registros utilizando la misma IR del proyecto.
tests/test_ir_spilling: tests/test_ir_spilling.c src/ir_spilling.c src/ir.c Analisis_Sintactico/ast.c include/ir_spilling.h include/ir.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ir_spilling.c src/ir_spilling.c src/ir.c Analisis_Sintactico/ast.c -o $@

# Prueba la reserva de espacio de pila y la emision de acceso a memoria.
tests/test_ir_spilling_marco: tests/test_ir_spilling_marco.c src/ir_spilling_marco.c include/ir_spilling_marco.h include/ir.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ir_spilling_marco.c src/ir_spilling_marco.c -o $@

# Ejecuta ambas pruebas y las agrega al conjunto habitual de make test.
test-spilling: tests/test_ir_spilling tests/test_ir_spilling_marco
	./tests/test_ir_spilling
	./tests/test_ir_spilling_marco

test: test-spilling

# Limpieza especifica; no reemplaza la receta clean del equipo.
clean-spilling:
	rm -f tests/test_ir_spilling tests/test_ir_spilling_marco
