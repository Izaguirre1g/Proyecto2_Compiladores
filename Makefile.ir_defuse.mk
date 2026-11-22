# Issue #7: API simbolica USE/DEF; pruebas aisladas, sin parser/Flex/Bison.
.PHONY: test-ir-defuse clean-ir-defuse

IR_DEFUSE_SRC := src/Representacion_Intermedia/ir_defuse.c
SRC += $(IR_DEFUSE_SRC)
balc: $(IR_DEFUSE_SRC) include/ir_defuse.h

tests/test_ir_defuse: tests/test_ir_defuse.c $(IR_DEFUSE_SRC) include/ir_defuse.h include/ir.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ir_defuse.c $(IR_DEFUSE_SRC) -o $@

test-ir-defuse: tests/test_ir_defuse
	./tests/test_ir_defuse

test: test-ir-defuse
clean: clean-ir-defuse
clean-ir-defuse:
	rm -f tests/test_ir_defuse tests/test_ir_defuse_consumidores

# Consumidores del contrato: DCE, IDE (incluidas metricas) y reordenamiento.
# Las recetas heredadas usan DCE_SRC/IDE_DCE_SRC al ejecutar, no al parsear.
DCE_SRC += $(IR_DEFUSE_SRC)
IDE_DCE_SRC += $(IR_DEFUSE_SRC)
tests/test_dce tests/test_ide_dce tests/test_metricas balc-ide: $(IR_DEFUSE_SRC) include/ir_defuse.h
reordenamiento/pruebas/test_reordenamiento: $(IR_DEFUSE_SRC) include/ir_defuse.h

# Prueba sin parser/Flex/Bison de ambos consumidores sobre IR real.
.PHONY: test-ir-defuse-consumidores
tests/test_ir_defuse_consumidores: tests/test_ir_defuse_consumidores.c \
        $(IR_DEFUSE_SRC) src/DCE/dce.c reordenamiento/reordenamiento.c \
        src/Representacion_Intermedia/ir.c src/Analisis_Sintactico/ast.c \
        include/ir_defuse.h include/ir.h include/dce.h reordenamiento/reordenamiento.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ir_defuse_consumidores.c \
	    $(IR_DEFUSE_SRC) src/DCE/dce.c reordenamiento/reordenamiento.c \
	    src/Representacion_Intermedia/ir.c src/Analisis_Sintactico/ast.c -o $@
test-ir-defuse-consumidores: tests/test_ir_defuse_consumidores
	./tests/test_ir_defuse_consumidores
test-ir-defuse: test-ir-defuse-consumidores
