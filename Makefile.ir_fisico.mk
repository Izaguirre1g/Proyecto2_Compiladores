# Issue #23: contrato explicito de registros fisicos para IR -> VLIW.
.PHONY: test-ir-fisico clean-ir-fisico

IR_FISICO_SRC := src/Representacion_Intermedia/ir_registros_fisicos.c
SRC += $(IR_FISICO_SRC)
balc: $(IR_FISICO_SRC) include/ir_registros_fisicos.h

tests/test_ir_fisico: tests/test_ir_fisico.c $(IR_FISICO_SRC) \
        $(IR_DEFUSE_SRC) src/ir_spilling.c $(IR)/ir.c $(SINTACTICO)/ast.c \
        include/ir_registros_fisicos.h include/ir_spilling.h include/ir_defuse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ir_fisico.c $(IR_FISICO_SRC) \
	    $(IR_DEFUSE_SRC) src/ir_spilling.c $(IR)/ir.c $(SINTACTICO)/ast.c -o $@

test-ir-fisico: tests/test_ir_fisico
	./tests/test_ir_fisico

test: test-ir-fisico
clean: clean-ir-fisico
clean-ir-fisico:
	rm -f tests/test_ir_fisico

# Recompilar el productor si cambia el contrato (SRC es recursivo).
balc: include/ir_registros_fisicos.h
