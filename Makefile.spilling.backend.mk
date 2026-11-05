# Agrega pruebas de integracion con el emisor de marcos real.
# Requiere que Makefile.spilling.mk ya este incluido en el Makefile principal.

.PHONY: test-spilling-backend clean-spilling-backend

tests/test_ir_spilling_backend: tests/test_ir_spilling_backend.c src/ir_spilling_backend.c src/ir_spilling_marco.c src/funciones_marco.c include/ir_spilling_backend.h include/ir_spilling_marco.h include/funciones_marco.h include/ir.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ir_spilling_backend.c src/ir_spilling_backend.c src/ir_spilling_marco.c src/funciones_marco.c -o $@

test-spilling-backend: tests/test_ir_spilling_backend
	./tests/test_ir_spilling_backend

test-spilling: test-spilling-backend

clean-spilling-backend:
	rm -f tests/test_ir_spilling_backend
