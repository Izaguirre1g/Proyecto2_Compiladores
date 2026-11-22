# Issue #3: contrato de latencias N+3 y efectos conservadores de la IR.
# No modifica el calendario fisico de Javier ni el backend BAL tradicional.
.PHONY: test-ir-latencias clean-ir-latencias

IR_LATENCIAS_SRC := src/Representacion_Intermedia/ir_latencias_efectos.c
SRC += $(IR_LATENCIAS_SRC)
balc: $(IR_LATENCIAS_SRC) include/ir_latencias_efectos.h

tests/test_ir_latencias_efectos: tests/test_ir_latencias_efectos.c \
        $(IR_LATENCIAS_SRC) $(IR_DEFUSE_SRC) \
        src/Calendarizacion_VLIW/vliw_scheduler.c \
        include/ir_latencias_efectos.h include/ir_defuse.h include/vliw_scheduler.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ir_latencias_efectos.c \
	    $(IR_LATENCIAS_SRC) $(IR_DEFUSE_SRC) \
	    src/Calendarizacion_VLIW/vliw_scheduler.c -o $@

test-ir-latencias: tests/test_ir_latencias_efectos
	./tests/test_ir_latencias_efectos

test: test-ir-latencias
clean: clean-ir-latencias
clean-ir-latencias:
	rm -f tests/test_ir_latencias_efectos
