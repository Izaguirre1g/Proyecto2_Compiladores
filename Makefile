# Herramientas que usamos para compilar el proyecto.
CC ?= cc
BISON ?= bison
FLEX ?= flex

# Evita que Make aplique reglas implicitas que sobrescriban lexer.c.
.SUFFIXES:

# Opciones de compilación: C11, advertencias y optimización.
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2

# Carpetas del código fuente, una por cada fase del compilador.
# Si una carpeta cambia de nombre, solo se corrige aquí.
LEXICO     := src/Analisis_Lexico
SINTACTICO := src/Analisis_Sintactico
SEMANTICO  := src/Analisis_Semantico
IR         := src/Representacion_Intermedia
GENERACION := src/Generacion_Codigo
ENLACE     := src/Resolucion_y_Enlace
BINARIO    := src/Codificacion_Binaria
CLI        := src/Linea_Comandos
CLONES     := src/Deteccion_Clones
IDE        := src/IDE
LL1        := src/LL1
DCE        := src/DCE

# Analisis predictivo LL(1): gramatica de BAL sin recursividad izquierda y
# factorizada, PRIMERO, SIGUIENTE, tabla M[A, a] y analizador con pila.
# Lo usa el IDE para predecir que tokens pueden venir (sugerencias y
# correcciones). Bison sigue siendo el parser que construye el AST.
LL1_SRC := $(LL1)/ll1_gramatica.c $(LL1)/ll1_tabla.c $(LL1)/ll1_analizador.c $(LL1)/ll1_recuperacion.c

# Carpetas donde se buscan los archivos .h.
# include/ tiene las interfaces públicas; las otras tres tienen encabezados
# que viven junto a su código: lexer_flex.h (Flex), parser.h (Bison) y
# generacion_interna.h (contrato privado de los generadores).
CPPFLAGS ?= -Iinclude -I$(LEXICO) -I$(SINTACTICO) -I$(GENERACION)

# Archivos del parser y del AST.
PARSER_SRC := $(SINTACTICO)/parser.c $(SINTACTICO)/puente_lexer.c $(SINTACTICO)/ast.c

# Archivo C que genera Flex a partir de lexer.l.
LEXER_GEN := $(LEXICO)/lexer_flex.c

# Archivos que forman el compilador.
SRC := $(CLI)/main.c $(CLI)/cli.c $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c $(GENERACION)/generacion_basica.c $(PARSER_SRC) $(SEMANTICO)/symbol_table.c $(SEMANTICO)/semantic.c $(SEMANTICO)/semantic_modules.c $(SEMANTICO)/memory.c \
       $(GENERACION)/generacion_adaptador.c $(GENERACION)/generacion_funciones.c $(GENERACION)/generacion_control.c \
       $(GENERACION)/funciones_marco.c $(GENERACION)/funciones_llamada.c $(ENLACE)/referencias_integracion.c \
       $(ENLACE)/resolucion_referencias.c $(ENLACE)/resolucion_saltos.c $(ENLACE)/enlace_funciones.c \
       $(BINARIO)/codificacion_binaria.c $(BINARIO)/formato_binario.c $(CLI)/diagnosticos.c $(CLONES)/clones.c $(IR)/ir.c \
       $(DCE)/dce.c

# Estos nombres son comandos de Make, no archivos.
.PHONY: all test test-parser test-ast test-cli test-generacion test-semantic-modules test-semantic-entry test-funciones-backend test-referencias-integracion test-m-simbolos test-auditoria-semantica test-ejercicio2-matriz test-diagnosticos test-ide-nucleo test-ide-mensajes test-ide-analisis test-ide-arreglos test-clones test-dce test-ide-dce test-ll1 tabla-ll1 ide clean

# Al ejecutar make, se construye balc.
all: balc

# Flex convierte las reglas de lexer.l en código C.
# $@ es el archivo generado y $< es lexer.l.
$(LEXER_GEN): $(LEXICO)/lexer.l $(LEXICO)/lexer_flex.h include/token.h
	$(FLEX) -o $@ $<

# Bison genera parser.c y parser.h usando la gramática.
# &: evita generar ambos archivos dos veces con make -j.
$(SINTACTICO)/parser.c $(SINTACTICO)/parser.h &: $(SINTACTICO)/gramatica.y
	$(BISON) -Wall -Werror -d -o $(SINTACTICO)/parser.c $<

# Compila todos los módulos y crea el ejecutable balc.
balc: $(SRC) include/cli.h include/lexer.h include/token.h include/sintactico.h include/ast.h include/generacion_basica.h include/semantic_modules.h include/semantic.h include/memory.h $(SINTACTICO)/parser.h \
      include/enlace_funciones.h include/resolucion_saltos.h include/resolucion_referencias.h include/codificacion_binaria.h \
      include/referencias_integracion.h include/generacion_adaptador.h include/generacion_funciones.h \
      $(GENERACION)/generacion_interna.h include/formato_binario.h include/diagnosticos.h include/clones.h include/ir.h include/dce.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) -o $@

# Compila las pruebas del lexer, sin necesitar el parser.
tests/test_lexer: tests/test_lexer.c $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c include/lexer.h include/token.h $(LEXICO)/lexer_flex.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_lexer.c $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c -o $@

# Ejecuta las pruebas del proyecto y analiza el factorial.
test: test-ir test-dce test-ide-dce test-ll1 test-diagnosticos test-ide-nucleo test-ide-mensajes test-ide-analisis test-ide-arreglos test-clones test-ast-json test-formato-binario test-ejercicio3 test-ambitos-generacion test-whale-pipeline test-cycle-pipeline test-resolucion-saltos test-codificacion-binaria test-condicional-pipeline tests/test_lexer balc test-parser test-ast test-cli test-generacion test-symbol-table test-semantic test-semantic-functions test-semantic-arrays test-semantic-matrix-cycle test-semantic-returns test-semantic-initializers test-semantic-dynamic-arrays test-semantic-lists test-semantic-list-indices test-semantic-composite-params test-semantic-pipeline test-semantic-modules test-semantic-entry test-funciones-backend test-referencias-integracion test-m-simbolos test-auditoria-semantica test-ejercicio2-matriz
	./tests/test_lexer
	./balc -v -o tests/factorial_make.bin examples/factorial.bal
	rm -f tests/factorial_make.bin tests/factorial_make.mem

# Compila las pruebas del parser.
tests/test_parser: tests/test_parser.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

# Ejecuta las pruebas sintácticas.
test-parser: balc tests/test_parser
	./tests/test_parser

# Compila las pruebas del AST.
tests/test_ast: tests/test_ast.c $(PARSER_SRC) $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c include/ast.h include/sintactico.h include/lexer.h include/token.h $(LEXICO)/lexer_flex.h $(SINTACTICO)/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ast.c $(PARSER_SRC) $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c -o $@

# Comprueba que el árbol se construya correctamente.
test-ast: tests/test_ast
	./tests/test_ast

# Compila las pruebas de los comandos de la terminal.
tests/test_cli: tests/test_cli.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

# Prueba las opciones y respuestas del ejecutable.
test-cli: balc tests/test_cli
	./tests/test_cli

# Compila las pruebas de generación básica de ensamblador.
tests/test_generacion: tests/test_generacion.c $(GENERACION)/generacion_basica.c include/generacion_basica.h $(PARSER_SRC) $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c include/ast.h include/sintactico.h include/lexer.h include/token.h $(LEXICO)/lexer_flex.h $(SINTACTICO)/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_generacion.c $(GENERACION)/generacion_basica.c $(PARSER_SRC) $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c -o $@

# Comprueba la traducción de operaciones básicas.
test-generacion: tests/test_generacion
	./tests/test_generacion

# Elimina los ejecutables y el archivo generado por Flex.
# Sirve para volver a compilar todo desde cero.
# Elimina los ejecutables y archivos generados.
clean:
	rm -f tests/test_ejercicio3 tests/test_resolucion_saltos tests/test_codificacion_binaria tests/test_ambitos_generacion tests/test_condicional_pipeline balc tests/test_lexer tests/test_parser tests/test_ast tests/test_cli tests/test_generacion tests/test_funciones_marco tests/test_funciones_llamada tests/test_funciones_integracion tests/test_generacion_funciones tests/test_semantic_visitante tests/test_generacion_adaptador tests/test_resolucion_referencias tests/test_referencias_integracion tests/test_symbol_table tests/test_semantic tests/test_semantic_functions tests/test_semantic_arrays tests/test_semantic_matrix_cycle tests/test_semantic_returns tests/test_semantic_dynamic_arrays test/test_semantic_lists test/test_semantic_list_indices test/test_semantic_composite_params test/test_semantic_pipeline tests/test_semantic_modules tests/test_diagnosticos tests/test_ide_nucleo tests/test_ide_mensajes tests/test_ide_analisis tests/test_ide_arreglos tests/test_clones tests/test_dce tests/test_ide_dce tests/test_ll1 balc-ll1 balc-ide $(LEXER_GEN)

# Pruebas de la tabla de símbolos
.PHONY: test-symbol-table

tests/test_symbol_table: tests/test_symbol_table.c \
                        $(SEMANTICO)/symbol_table.c \
                        include/symbol_table.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    tests/test_symbol_table.c \
	    $(SEMANTICO)/symbol_table.c \
	    -o $@

# Ejecutar pruebas de la tabla de símbolos
test-symbol-table: tests/test_symbol_table
	./tests/test_symbol_table

# Compilar pruebas del análisis semántico.
tests/test_semantic: tests/test_semantic.c \
                     $(SEMANTICO)/semantic.c \
                     $(SEMANTICO)/symbol_table.c \
                     $(SINTACTICO)/ast.c \
                     include/semantic.h \
                     include/symbol_table.h \
                     include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    tests/test_semantic.c \
	    $(SEMANTICO)/semantic.c \
	    $(SEMANTICO)/symbol_table.c \
	    $(SINTACTICO)/ast.c \
	    -o $@

# Ejecutar pruebas del análisis semántico.
.PHONY: test-semantic

test-semantic: tests/test_semantic
	./tests/test_semantic

# Compilar pruebas semánticas de funciones.
tests/test_semantic_functions: tests/test_semantic_functions.c \
                               $(SEMANTICO)/semantic.c \
                               $(SEMANTICO)/symbol_table.c \
                               $(SINTACTICO)/ast.c \
                               include/semantic.h \
                               include/symbol_table.h \
                               include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    tests/test_semantic_functions.c \
	    $(SEMANTICO)/semantic.c \
	    $(SEMANTICO)/symbol_table.c \
	    $(SINTACTICO)/ast.c \
	    -o $@

# Ejecutar pruebas de funciones.
.PHONY: test-semantic-functions

test-semantic-functions: tests/test_semantic_functions
	./tests/test_semantic_functions

# Compilar pruebas de arreglos.
tests/test_semantic_arrays: tests/test_semantic_arrays.c \
                            $(SEMANTICO)/semantic.c \
                            $(SEMANTICO)/symbol_table.c \
                            $(SINTACTICO)/ast.c \
                            include/semantic.h \
                            include/symbol_table.h \
                            include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    tests/test_semantic_arrays.c \
	    $(SEMANTICO)/semantic.c \
	    $(SEMANTICO)/symbol_table.c \
	    $(SINTACTICO)/ast.c \
	    -o $@

.PHONY: test-semantic-arrays

test-semantic-arrays: tests/test_semantic_arrays
	./tests/test_semantic_arrays

# =============================================
# Pruebas de matrices bidimensionales y cycle
# =============================================

tests/test_semantic_matrix_cycle: tests/test_semantic_matrix_cycle.c \
                                  $(SEMANTICO)/semantic.c \
                                  $(SEMANTICO)/symbol_table.c \
                                  $(SINTACTICO)/ast.c \
                                  include/semantic.h \
                                  include/symbol_table.h \
                                  include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    tests/test_semantic_matrix_cycle.c \
	    $(SEMANTICO)/semantic.c \
	    $(SEMANTICO)/symbol_table.c \
	    $(SINTACTICO)/ast.c \
	    -o $@

.PHONY: test-semantic-matrix-cycle

test-semantic-matrix-cycle: tests/test_semantic_matrix_cycle
	./tests/test_semantic_matrix_cycle

# ==========================================
# Pruebas de retornos obligatorios
# ==========================================

tests/test_semantic_returns: tests/test_semantic_returns.c \
                             $(SEMANTICO)/semantic.c \
                             $(SEMANTICO)/symbol_table.c \
                             $(SINTACTICO)/ast.c \
                             include/semantic.h \
                             include/symbol_table.h \
                             include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    tests/test_semantic_returns.c \
	    $(SEMANTICO)/semantic.c \
	    $(SEMANTICO)/symbol_table.c \
	    $(SINTACTICO)/ast.c \
	    -o $@

.PHONY: test-semantic-returns

test-semantic-returns: tests/test_semantic_returns
	./tests/test_semantic_returns

# ===========================================
# Pruebas de inicializadores de matrices
# ===========================================

tests/test_semantic_initializers: tests/test_semantic_initializers.c \
                                  $(SEMANTICO)/semantic.c \
                                  $(SEMANTICO)/symbol_table.c \
                                  $(SINTACTICO)/ast.c \
                                  include/semantic.h \
                                  include/symbol_table.h \
                                  include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    tests/test_semantic_initializers.c \
	    $(SEMANTICO)/semantic.c \
	    $(SEMANTICO)/symbol_table.c \
	    $(SINTACTICO)/ast.c \
	    -o $@

.PHONY: test-semantic-initializers

test-semantic-initializers: tests/test_semantic_initializers
	./tests/test_semantic_initializers

# ===========================================
# Pruebas de dimensiones dinamicas
# ===========================================

tests/test_semantic_dynamic_arrays: tests/test_semantic_dynamic_arrays.c \
                                    $(SEMANTICO)/semantic.c \
                                    $(SEMANTICO)/symbol_table.c \
                                    $(SINTACTICO)/ast.c \
                                    include/semantic.h \
                                    include/symbol_table.h \
                                    include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    tests/test_semantic_dynamic_arrays.c \
	    $(SEMANTICO)/semantic.c \
	    $(SEMANTICO)/symbol_table.c \
	    $(SINTACTICO)/ast.c \
	    -o $@

.PHONY: test-semantic-dynamic-arrays

test-semantic-dynamic-arrays: tests/test_semantic_dynamic_arrays
	./tests/test_semantic_dynamic_arrays

# ===========================================
# Pruebas semanticas de listas
# ===========================================

tests/test_semantic_lists: tests/test_semantic_lists.c \
                           $(SEMANTICO)/semantic.c \
                           $(SEMANTICO)/symbol_table.c \
                           $(SINTACTICO)/ast.c \
                           include/semantic.h \
                           include/symbol_table.h \
                           include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    tests/test_semantic_lists.c \
	    $(SEMANTICO)/semantic.c \
	    $(SEMANTICO)/symbol_table.c \
	    $(SINTACTICO)/ast.c \
	    -o $@

.PHONY: test-semantic-lists

test-semantic-lists: tests/test_semantic_lists
	./tests/test_semantic_lists

# ===========================================
# Pruebas de indices de listas
# ===========================================

tests/test_semantic_list_indices: tests/test_semantic_list_indices.c \
                                  $(SEMANTICO)/semantic.c \
                                  $(SEMANTICO)/symbol_table.c \
                                  $(SINTACTICO)/ast.c \
                                  include/semantic.h \
                                  include/symbol_table.h \
                                  include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    tests/test_semantic_list_indices.c \
	    $(SEMANTICO)/semantic.c \
	    $(SEMANTICO)/symbol_table.c \
	    $(SINTACTICO)/ast.c \
	    -o $@

.PHONY: test-semantic-list-indices

test-semantic-list-indices: tests/test_semantic_list_indices
	./tests/test_semantic_list_indices

# ===========================================
# Pruebas de parametros compuestos
# ===========================================

tests/test_semantic_composite_params: tests/test_semantic_composite_params.c \
                                      $(SEMANTICO)/semantic.c \
                                      $(SEMANTICO)/symbol_table.c \
                                      $(SINTACTICO)/ast.c \
                                      include/semantic.h \
                                      include/symbol_table.h \
                                      include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    tests/test_semantic_composite_params.c \
	    $(SEMANTICO)/semantic.c \
	    $(SEMANTICO)/symbol_table.c \
	    $(SINTACTICO)/ast.c \
	    -o $@

.PHONY: test-semantic-composite-params

test-semantic-composite-params: tests/test_semantic_composite_params
	./tests/test_semantic_composite_params

# ===========================================
# ETAPA 2K
# Integracion parser + analisis semantico
# ===========================================

tests/test_semantic_pipeline: \
    tests/test_semantic_pipeline.c \
    $(SINTACTICO)/parser.c \
    $(SINTACTICO)/puente_lexer.c \
    $(SINTACTICO)/ast.c \
    $(SEMANTICO)/semantic.c \
    $(SEMANTICO)/symbol_table.c \
    $(LEXICO)/lexer.c \
    $(LEXICO)/lexer_flex.c \
    $(LEXICO)/token.c \
    include/semantic.h \
    include/symbol_table.h \
    include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    tests/test_semantic_pipeline.c \
	    $(SINTACTICO)/parser.c \
	    $(SINTACTICO)/puente_lexer.c \
	    $(SINTACTICO)/ast.c \
	    $(SEMANTICO)/semantic.c \
	    $(SEMANTICO)/symbol_table.c \
	    $(LEXICO)/lexer.c \
	    $(LEXICO)/lexer_flex.c \
	    $(LEXICO)/token.c \
	    -o $@

.PHONY: test-semantic-pipeline

test-semantic-pipeline: tests/test_semantic_pipeline
	@echo "Pipeline compilado correctamente."
	@echo "Ejecuta el programa con un archivo BAL y su resultado esperado."

# ===========================================
# ETAPA 2N-F: integracion de pruebas de modulos
# ===========================================

MODULOS_SRC := $(SEMANTICO)/semantic_modules.c \
               $(SEMANTICO)/semantic.c \
               $(SEMANTICO)/symbol_table.c \
               $(PARSER_SRC) $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c

# Compilacion independiente: el CLI y el backend no participan.
tests/test_semantic_modules: tests/test_semantic_modules.c $(MODULOS_SRC) \
                             include/semantic_modules.h include/semantic.h \
                             include/symbol_table.h include/ast.h \
                             include/sintactico.h $(SINTACTICO)/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_semantic_modules.c $(MODULOS_SRC) -o $@

# Casos positivos, negativos, transitivos, compartidos y rutas equivalentes.
# El ejecutable compara el codigo real (0, 1 o 2) con el esperado.
test-semantic-modules: tests/test_semantic_modules
	./tests/test_semantic_modules tests/modulos/principal_valido.bal 0
	./tests/test_semantic_modules tests/modulos/principal_inexistente.bal 1
	./tests/test_semantic_modules tests/modulos/principal_ciclo.bal 1
	./tests/test_semantic_modules tests/modulos/principal_modulo_invalido.bal 1
	./tests/test_semantic_modules tests/modulos/principal_llamada.bal 0
	./tests/test_semantic_modules tests/modulos/principal_funcion_inexistente.bal 1
	./tests/test_semantic_modules tests/modulos/principal_argumentos_error.bal 1
	./tests/test_semantic_modules tests/modulos/principal_tipo_error.bal 1
	./tests/test_semantic_modules tests/modulos/principal_doble_importacion.bal 0
	./tests/test_semantic_modules tests/modulos/principal_alias_duplicado.bal 1
	./tests/test_semantic_modules tests/modulos/principal_constante.bal 0
	./tests/test_semantic_modules tests/modulos/principal_alias_inexistente.bal 1
	./tests/test_semantic_modules tests/modulos/principal_anidado.bal 0
	./tests/test_semantic_modules tests/modulos/principal_compartido.bal 0
	./tests/test_semantic_modules tests/modulos/principal_dependencia_error.bal 1
	./tests/test_semantic_modules tests/modulos/principal_privacidad.bal 1
	./tests/test_semantic_modules tests/modulos/principal_arreglo_importado.bal 0
	./tests/test_semantic_modules tests/modulos/principal_arreglo_error.bal 1
	./tests/test_semantic_modules tests/modulos/./principal_llamada.bal 0
	./tests/test_semantic_modules tests/modulos/./principal_ciclo.bal 1
	@echo "OK: 20 escenarios de modulos BAL (sin offsets)"

# Etapa 2O-D: validar salidas y diagnosticos del modo semantico de balc.
# Las salidas negativas son esperadas: no deben hacer fallar la receta.
test-semantic-entry: balc
	@set -eu; \
	 salida=$$(mktemp); \
	 trap 'rm -f "$$salida"' EXIT HUP INT TERM; \
	 verificar() { \
	     etiqueta=$$1; esperado=$$2; patron=$$3; shift 3; \
	     estado=0; ./balc --semantico "$$@" >"$$salida" 2>&1 || estado=$$?; \
	     if [ "$$estado" -ne "$$esperado" ]; then \
	         printf 'FALLO: %s: codigo %s (esperado %s)\n' "$$etiqueta" "$$estado" "$$esperado"; \
	         cat "$$salida"; exit 1; \
	     fi; \
	     if [ -n "$$patron" ] && ! grep -F -q -- "$$patron" "$$salida"; then \
	         printf 'FALLO: %s: falta diagnostico: %s\n' "$$etiqueta" "$$patron"; \
	         cat "$$salida"; exit 1; \
	     fi; \
	     printf '[OK] %s (codigo %s)\n' "$$etiqueta" "$$esperado"; \
	 }; \
	 verificar 'proyecto valido' 0 'Integridad de tabla:' tests/modulos/principal_llamada.bal; \
	 verificar 'error de tipos' 1 'argumento 2 de' tests/modulos/principal_tipo_error.bal; \
	 verificar 'modulo inexistente' 1 "no se pudo abrir modulo 'tests/modulos/no_existe.bal'" tests/modulos/principal_inexistente.bal; \
	 verificar 'principal inexistente' 1 'archivo_que_no_existe.bal: error: no se pudo abrir el archivo principal' tests/modulos/archivo_que_no_existe.bal; \
	 verificar 'sin archivo' 2 'Uso:'; \
	 verificar 'argumentos sobrantes' 2 'Uso:' tests/modulos/principal_llamada.bal otro.bal; \
	 echo 'OK: 6 escenarios del modo --semantico (codigos y diagnosticos)'

# Etapa 3B: pruebas aisladas del ABI y generacion desde AST.
# No forman parte del ejecutable balc hasta integrar el resolvedor semantico.
FUNCIONES_SRC := $(GENERACION)/funciones_marco.c $(GENERACION)/funciones_llamada.c

tests/test_funciones_marco: tests/test_funciones_marco.c $(GENERACION)/funciones_marco.c include/funciones_marco.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_funciones_marco.c $(GENERACION)/funciones_marco.c -o $@

tests/test_funciones_llamada: tests/test_funciones_llamada.c $(GENERACION)/funciones_llamada.c include/funciones_llamada.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_funciones_llamada.c $(GENERACION)/funciones_llamada.c -o $@

tests/test_funciones_integracion: tests/test_funciones_integracion.c $(FUNCIONES_SRC) include/funciones_marco.h include/funciones_llamada.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_funciones_integracion.c $(FUNCIONES_SRC) -o $@

tests/test_generacion_funciones: tests/test_generacion_funciones.c $(GENERACION)/generacion_funciones.c $(GENERACION)/generacion_control.c $(GENERACION)/generacion_basica.c $(FUNCIONES_SRC) include/generacion_funciones.h include/generacion_basica.h include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_generacion_funciones.c $(GENERACION)/generacion_funciones.c $(GENERACION)/generacion_control.c $(GENERACION)/generacion_basica.c $(FUNCIONES_SRC) -o $@

.PHONY: test-funciones-backend
test-funciones-backend: tests/test_funciones_marco tests/test_funciones_llamada tests/test_funciones_integracion tests/test_generacion_funciones
	./tests/test_funciones_marco
	./tests/test_funciones_llamada
	./tests/test_funciones_integracion
	./tests/test_generacion_funciones

# 3B-D3B/D3C/D3D/D3E: fuentes aisladas del modulo y pruebas automatizadas.
BACKEND_ESCALAR_SRC := $(GENERACION)/generacion_adaptador.c $(GENERACION)/generacion_funciones.c $(GENERACION)/generacion_control.c \
                       $(GENERACION)/generacion_basica.c $(FUNCIONES_SRC)

# El visitante necesita el cargador, parser, lexer y asignador semantico.
tests/test_semantic_visitante: tests/test_semantic_visitante.c $(MODULOS_SRC) \
                              $(SEMANTICO)/memory.c include/memory.h \
                              include/semantic_modules.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_semantic_visitante.c \
	      $(MODULOS_SRC) $(SEMANTICO)/memory.c -o $@

tests/test_generacion_adaptador: tests/test_generacion_adaptador.c \
                                 $(BACKEND_ESCALAR_SRC) $(SEMANTICO)/symbol_table.c \
                                 include/generacion_adaptador.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_generacion_adaptador.c \
	      $(BACKEND_ESCALAR_SRC) $(SEMANTICO)/symbol_table.c -o $@

tests/test_resolucion_referencias: tests/test_resolucion_referencias.c \
                                   $(ENLACE)/resolucion_referencias.c include/resolucion_referencias.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_resolucion_referencias.c \
	      $(ENLACE)/resolucion_referencias.c -o $@

tests/test_referencias_integracion: tests/test_referencias_integracion.c \
                                    $(ENLACE)/referencias_integracion.c include/referencias_integracion.h \
                                    $(ENLACE)/resolucion_referencias.c $(BACKEND_ESCALAR_SRC) \
                                    $(SEMANTICO)/symbol_table.c
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_referencias_integracion.c \
	      $(ENLACE)/referencias_integracion.c $(ENLACE)/resolucion_referencias.c \
	      $(BACKEND_ESCALAR_SRC) $(SEMANTICO)/symbol_table.c -o $@

.PHONY: test-referencias-integracion
test-referencias-integracion: tests/test_semantic_visitante \
                              tests/test_generacion_adaptador \
                              tests/test_resolucion_referencias \
                              tests/test_referencias_integracion
	./tests/test_semantic_visitante
	./tests/test_generacion_adaptador
	./tests/test_resolucion_referencias
	./tests/test_referencias_integracion

# Fase 5: layout de bundles y resolucion de saltos de control anidado.
tests/test_resolucion_saltos: tests/test_resolucion_saltos.c tests/modelo_control.h \
                              $(ENLACE)/resolucion_saltos.c include/resolucion_saltos.h \
                              $(ENLACE)/enlace_funciones.c include/enlace_funciones.h \
                              $(ENLACE)/referencias_integracion.c $(ENLACE)/resolucion_referencias.c \
                              $(MODULOS_SRC) $(BACKEND_ESCALAR_SRC) $(SEMANTICO)/memory.c \
                              include/symbol_table.h $(GENERACION)/generacion_interna.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_resolucion_saltos.c $(ENLACE)/resolucion_saltos.c \
	      $(ENLACE)/enlace_funciones.c $(ENLACE)/referencias_integracion.c $(ENLACE)/resolucion_referencias.c \
	      $(MODULOS_SRC) $(BACKEND_ESCALAR_SRC) $(SEMANTICO)/memory.c -o $@

.PHONY: test-resolucion-saltos
test-resolucion-saltos: tests/test_resolucion_saltos
	./tests/test_resolucion_saltos

# Fase 6: codificacion binaria contra la referencia de tools/assembler.py.
tests/test_codificacion_binaria: tests/test_codificacion_binaria.c $(BINARIO)/codificacion_binaria.c \
                                 include/codificacion_binaria.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_codificacion_binaria.c $(BINARIO)/codificacion_binaria.c -o $@

.PHONY: test-codificacion-binaria
test-codificacion-binaria: tests/test_codificacion_binaria
	./tests/test_codificacion_binaria

# 3B-D3F: comprueba el reporte de simbolos despues del analisis semantico.
.PHONY: test-m-simbolos
test-m-simbolos: balc
	sh tests/test_m_simbolos.sh

# Auditoria adicional: reglas seek/seize, ambitos y diagnosticos.
test-auditoria-semantica: balc
	sh tests/test_auditoria_semantica.sh

# Ejercicio 2: validacion del programa BAL sobre matriz 2D.
.PHONY: test-ejercicio2-matriz
test-ejercicio2-matriz: balc
	sh tests/test_ejercicio2_matriz.sh

# Meibel: integracion del control de flujo con parser, semantica y memoria.
tests/test_condicional_pipeline: tests/test_condicional_pipeline.c $(MODULOS_SRC) $(BACKEND_ESCALAR_SRC) $(SEMANTICO)/memory.c include/generacion_funciones.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_condicional_pipeline.c $(MODULOS_SRC) $(BACKEND_ESCALAR_SRC) $(SEMANTICO)/memory.c -o $@

.PHONY: test-condicional-pipeline
test-condicional-pipeline: tests/test_condicional_pipeline
	./tests/test_condicional_pipeline

# Contrato privado compartido por los dos generadores.
tests/test_generacion_funciones tests/test_generacion_adaptador tests/test_referencias_integracion tests/test_condicional_pipeline: $(GENERACION)/generacion_interna.h

.PHONY: test-whale-pipeline
test-whale-pipeline: tests/test_condicional_pipeline
	./tests/test_condicional_pipeline --whale

.PHONY: test-cycle-pipeline
test-cycle-pipeline: tests/test_condicional_pipeline
	./tests/test_condicional_pipeline --cycle

# El contrato de simbolos se comparte entre semantica, memoria y generacion.
tests/test_generacion_funciones: tests/modelo_control.h
tests/test_generacion_adaptador tests/test_referencias_integracion tests/test_condicional_pipeline: include/symbol_table.h

tests/test_ambitos_generacion: tests/test_ambitos_generacion.c tests/modelo_control.h $(MODULOS_SRC) $(BACKEND_ESCALAR_SRC) $(SEMANTICO)/memory.c include/symbol_table.h $(GENERACION)/generacion_interna.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ambitos_generacion.c $(MODULOS_SRC) $(BACKEND_ESCALAR_SRC) $(SEMANTICO)/memory.c -o $@

.PHONY: test-ambitos-generacion
test-ambitos-generacion: tests/test_ambitos_generacion
	./tests/test_ambitos_generacion

# Ejercicio 3: funciones completas, llamadas y control en modelo secuencial.
tests/test_ejercicio3: tests/test_ejercicio3.c $(MODULOS_SRC) $(BACKEND_ESCALAR_SRC) $(SEMANTICO)/memory.c $(ENLACE)/referencias_integracion.c $(ENLACE)/resolucion_referencias.c include/symbol_table.h $(GENERACION)/generacion_interna.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ejercicio3.c $(MODULOS_SRC) $(BACKEND_ESCALAR_SRC) $(SEMANTICO)/memory.c $(ENLACE)/referencias_integracion.c $(ENLACE)/resolucion_referencias.c -o $@

.PHONY: test-ejercicio3
test-ejercicio3: tests/test_ejercicio3
	PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p test_ejercicio3.py -v

.PHONY: test-formato-binario
test-formato-binario: balc
	python3 -m unittest discover -s tests -p 'test_formato_binario.py' -v

# Exportacion JSON del AST existente.
.PHONY: test-ast-json
test-ast-json: balc
	PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p test_ast_json.py -v

# Pruebas de --diagnosticos (errores en JSON para el IDE).
# 1. Prueba unitaria del modulo src/Linea_Comandos/diagnosticos.c.
# 2. Prueba con balc: un archivo sin errores debe dar "errores": 0, y uno
#    con un error semantico debe terminar con codigo 1 y dar la posicion exacta.
tests/test_diagnosticos: tests/test_diagnosticos.c $(CLI)/diagnosticos.c include/diagnosticos.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_diagnosticos.c $(CLI)/diagnosticos.c -o $@

test-diagnosticos: tests/test_diagnosticos balc
	./tests/test_diagnosticos
	./balc --diagnosticos tests/pipeline_valido.bal | grep -q '"errores": 0'
	./balc --diagnosticos tests/pipeline_error.bal > tests/diagnosticos_salida.json; test $$? -eq 1
	grep -q '"fase": "semantico", "archivo": "tests/pipeline_error.bal", "linea": 3, "columna": 5' tests/diagnosticos_salida.json
	rm -f tests/diagnosticos_salida.json
	@echo 'OK: --diagnosticos (unitarias y con balc)'

# Pruebas del nucleo del IDE: sugerencias, autocierre y delimitadores.
# Usa el lexer real del compilador, por eso compila tambien lexer.c y token.c.
IDE_NUCLEO_SRC := $(IDE)/ide_nucleo.c $(CLI)/diagnosticos.c $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c $(LL1_SRC)

tests/test_ide_nucleo: tests/test_ide_nucleo.c $(IDE_NUCLEO_SRC) include/ide_nucleo.h include/diagnosticos.h include/lexer.h include/token.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ide_nucleo.c $(IDE_NUCLEO_SRC) -o $@

test-ide-nucleo: tests/test_ide_nucleo
	./tests/test_ide_nucleo

# Traduccion de los mensajes del parser a espanol. No depende de nada mas.
tests/test_ide_mensajes: tests/test_ide_mensajes.c $(IDE)/ide_mensajes.c include/ide_mensajes.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ide_mensajes.c $(IDE)/ide_mensajes.c -o $@

test-ide-mensajes: tests/test_ide_mensajes
	./tests/test_ide_mensajes

# Analisis del texto del editor: necesita el frontend completo del compilador
# (lexer, parser, AST y semantico), pero no la generacion de codigo.
IDE_FRONTEND_SRC := $(SINTACTICO)/parser.c $(SINTACTICO)/puente_lexer.c $(SINTACTICO)/ast.c \
                    $(SEMANTICO)/symbol_table.c $(SEMANTICO)/semantic.c $(SEMANTICO)/semantic_modules.c
IDE_ANALISIS_SRC := $(IDE)/ide_analisis.c $(IDE)/ide_mensajes.c $(IDE)/ide_arreglos.c $(IDE_NUCLEO_SRC) $(IDE_FRONTEND_SRC)

# Boton "Codigo muerto" del IDE: semantico -> IR -> variables vivas -> DCE
# sobre el texto del editor (sin GTK, para poder probarlo).
IDE_DCE_SRC := $(IDE)/ide_dce.c $(DCE)/dce.c $(IR)/ir.c

tests/test_ide_dce: tests/test_ide_dce.c $(IDE_DCE_SRC) $(IDE_ANALISIS_SRC) include/ide_dce.h include/dce.h include/ir.h include/ide_analisis.h $(SINTACTICO)/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ide_dce.c $(IDE_DCE_SRC) $(IDE_ANALISIS_SRC) -o $@

test-ide-dce: tests/test_ide_dce
	./tests/test_ide_dce

tests/test_ide_analisis: tests/test_ide_analisis.c $(IDE_ANALISIS_SRC) include/ide_analisis.h include/ide_mensajes.h include/ide_arreglos.h include/ide_nucleo.h include/diagnosticos.h $(SINTACTICO)/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ide_analisis.c $(IDE_ANALISIS_SRC) -o $@

test-ide-analisis: tests/test_ide_analisis
	./tests/test_ide_analisis

# Correccion automatica: propone arreglos, los aplica y revisa que el
# codigo corregido ya no tenga errores (por eso usa todo el analisis).
tests/test_ide_arreglos: tests/test_ide_arreglos.c $(IDE_ANALISIS_SRC) include/ide_analisis.h include/ide_arreglos.h include/diagnosticos.h $(SINTACTICO)/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ide_arreglos.c $(IDE_ANALISIS_SRC) -o $@

test-ide-arreglos: tests/test_ide_arreglos
	./tests/test_ide_arreglos

# Deteccion de clones Tipo I y II. Compara los AST de los archivos, asi que
# necesita el lexer y el parser, pero no el semantico ni la generacion.
CLONES_SRC := $(CLONES)/clones.c $(SINTACTICO)/parser.c $(SINTACTICO)/puente_lexer.c \
              $(SINTACTICO)/ast.c $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c

tests/test_clones: tests/test_clones.c $(CLONES_SRC) include/clones.h include/ast.h include/sintactico.h $(SINTACTICO)/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_clones.c $(CLONES_SRC) -o $@

test-clones: tests/test_clones balc
	./tests/test_clones
	./balc --clones examples/llamadas.bal examples/ejercicio3_puntajes.bal | grep -q 'Clon 1: tipo I'
	./balc --clones --json examples/llamadas.bal examples/matematicas.bal > tests/clones_salida.json
	grep -q '"tipo": 2, "que": "funcion"' tests/clones_salida.json
	grep -q '"archivo": "examples/matematicas.bal", "linea": 2, "columna": 1, "linea_fin": 4' tests/clones_salida.json
	rm -f tests/clones_salida.json
	./balc --clones examples/factorial.bal | grep -q '^0 clones en 1 archivo$$'
	! ./balc --clones 2>/dev/null
	@echo 'OK: deteccion de clones (unitarias y con balc)'

# El IDE con ventana (GTK 3). Se compila con "make ide" y se abre con:
#   ./balc-ide examples/factorial.bal
# Requiere: sudo apt install libgtk-3-dev pkg-config
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS = $(shell pkg-config --libs gtk+-3.0)

ide: balc-ide

balc-ide: $(IDE)/ide_gtk.c $(IDE)/ide_tabla_ll1.c $(IDE)/ide_dce_ventana.c $(IDE_DCE_SRC) $(CLONES)/clones.c $(IDE_ANALISIS_SRC) include/ide_tabla_ll1.h include/ide_dce.h include/ide_dce_ventana.h include/dce.h include/clones.h include/ide_analisis.h include/ide_mensajes.h include/ide_arreglos.h include/ide_nucleo.h include/diagnosticos.h $(SINTACTICO)/parser.h
	$(CC) $(CPPFLAGS) $(GTK_CFLAGS) $(CFLAGS) $(IDE)/ide_gtk.c $(IDE)/ide_tabla_ll1.c $(IDE)/ide_dce_ventana.c $(IDE_DCE_SRC) $(CLONES)/clones.c $(IDE_ANALISIS_SRC) -o $@ $(GTK_LIBS)

# Eliminacion de codigo muerto (DCE) con analisis de variables vivas, sobre
# la IR. La prueba arma la IR desde programas BAL (tests/dce/) con el parser,
# y despues revisa la salida de balc --dce.
DCE_SRC := $(DCE)/dce.c $(IR)/ir.c $(PARSER_SRC) $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c

tests/test_dce: tests/test_dce.c $(DCE_SRC) include/dce.h include/ir.h include/ast.h include/sintactico.h $(SINTACTICO)/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_dce.c $(DCE_SRC) -o $@

test-dce: tests/test_dce balc
	./tests/test_dce
	./balc --dce tests/dce/dce_llamadas.bal | grep -q 'Codigo muerto eliminado: 5 de'
	./balc --dce tests/dce/dce_ciclo.bal | grep -q 'en 3 pasadas'
	./balc --dce tests/dce/dce_division.bal | grep -q 'pueden fallar (division o potencia): 1'
	! ./balc --dce -s tests/dce/dce_llamadas.bal 2>/dev/null
	@echo 'OK: eliminacion de codigo muerto (unitarias y con balc --dce)'

# Proyecto II: IR independiente del parser generado, prueba con AST construido en C.
.PHONY: test-ir
tests/test_ir: tests/test_ir.c $(IR)/ir.c $(SINTACTICO)/ast.c include/ir.h include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ir.c $(IR)/ir.c $(SINTACTICO)/ast.c -o $@
test-ir: tests/test_ir
	./tests/test_ir

# Analisis predictivo LL(1) (LL1_SRC se define arriba, junto a las carpetas).
# Programas con los que se compara el LL(1) contra Bison.
LL1_PROGRAMAS := $(wildcard examples/*.bal tests/*.bal tests/modulos/*.bal tests/fixtures/*.bal)

# La prueba compara el LL(1) con Bison, asi que tambien compila el parser.
tests/test_ll1: tests/test_ll1.c $(LL1_SRC) include/ll1.h $(PARSER_SRC) $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c include/sintactico.h $(SINTACTICO)/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ll1.c $(LL1_SRC) $(PARSER_SRC) $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c -o $@

test-ll1: tests/test_ll1
	@echo './tests/test_ll1 <$(words $(LL1_PROGRAMAS)) programas .bal>'
	@./tests/test_ll1 $(LL1_PROGRAMAS)

# Herramienta para la defensa:
#   ./balc-ll1 --tabla               PRIMERO, SIGUIENTE y tabla M en Markdown
#   ./balc-ll1 --traza archivo.bal   pila, entrada y accion paso a paso
balc-ll1: $(LL1)/ll1_herramienta.c $(LL1_SRC) include/ll1.h $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LL1)/ll1_herramienta.c $(LL1_SRC) $(LEXICO)/lexer.c $(LEXER_GEN) $(LEXICO)/token.c -o $@

# Regenera docs/ll1_tabla.md a partir de la gramatica.
tabla-ll1: balc-ll1
	./balc-ll1 --tabla > docs/ll1_tabla.md
