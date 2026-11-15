# Tabla LL(1) de BAL (generada)

Archivo generado con `make tabla-ll1`; no se edita a mano.
La explicación está en [ll1.md](ll1.md).

- Producciones: 132
- No terminales: 62
- Conflictos (celdas con más de una producción): 0 → **la gramática es LL(1)**

## Producciones

  0. programa → elemento_superior mas_elementos
  1. mas_elementos → elemento_superior mas_elementos
  2. mas_elementos → ε
  3. elemento_superior → funcion
  4. elemento_superior → importacion
  5. elemento_superior → constante
  6. funcion → CREATE_FUNK HASH tipo_retorno HASH nombre_funcion LPAREN parametros_opcionales RPAREN bloque
  7. tipo_retorno → tipo
  8. tipo_retorno → DECLARE_INFINITE_VOID
  9. nombre_funcion → MAIN
 10. nombre_funcion → IDENTIFIER
 11. parametros_opcionales → parametro mas_parametros
 12. parametros_opcionales → ε
 13. mas_parametros → COMMA parametro mas_parametros
 14. mas_parametros → ε
 15. parametro → IDENTIFIER STAR tipo_parametro
 16. tipo_parametro → tipo dimensiones_parametro
 17. tipo_parametro → DECLARE_LIST tipo
 18. dimensiones_parametro → LBRACKET dimension_parametro_resto dimensiones_parametro
 19. dimensiones_parametro → ε
 20. dimension_parametro_resto → RBRACKET
 21. dimension_parametro_resto → expresion RBRACKET
 22. importacion → BRING IDENTIFIER alias_opcional SEMICOLON
 23. alias_opcional → AKA IDENTIFIER
 24. alias_opcional → ε
 25. constante → DECLARE_CONST IDENTIFIER STAR tipo_variable ASSIGN expresion SEMICOLON
 26. tipo → DECLARE_INT
 27. tipo → DECLARE_BOOLEAN
 28. tipo → DECLARE_TEXT
 29. tipo → DECLARE_CHAR
 30. tipo_variable → tipo dimensiones
 31. dimensiones → LBRACKET expresion RBRACKET dimensiones
 32. dimensiones → ε
 33. bloque → LBRACE sentencias RBRACE
 34. sentencias → sentencia sentencias
 35. sentencias → ε
 36. sentencia → IDENTIFIER resto_identificador
 37. sentencia → bloque
 38. sentencia → condicional
 39. sentencia → ciclo
 40. sentencia → retorno
 41. sentencia → constante
 42. sentencia → declaracion_lista
 43. sentencia → recorrido
 44. sentencia → intentar
 45. sentencia → llamada_especial SEMICOLON
 46. resto_identificador → STAR tipo_variable inicializador_opcional mas_declaradores SEMICOLON
 47. resto_identificador → sufijos_referencia resto_referencia
 48. resto_referencia → ASSIGN expresion SEMICOLON
 49. resto_referencia → LPAREN argumentos_opcionales RPAREN SEMICOLON
 50. mas_declaradores → COMMA IDENTIFIER STAR tipo_variable inicializador_opcional mas_declaradores
 51. mas_declaradores → ε
 52. inicializador_opcional → ASSIGN expresion
 53. inicializador_opcional → ε
 54. llamada_especial → MAIN LPAREN argumentos_opcionales RPAREN
 55. llamada_especial → ADD LPAREN expresion COMMA expresion RPAREN
 56. llamada_especial → REMOVE LPAREN expresion COMMA expresion RPAREN
 57. llamada_especial → SIZE LPAREN expresion RPAREN
 58. declaracion_lista → DECLARE_LIST declarador_lista mas_listas SEMICOLON
 59. declarador_lista → IDENTIFIER STAR tipo inicializador_opcional
 60. mas_listas → COMMA declarador_lista mas_listas
 61. mas_listas → ε
 62. condicional → WHETHER LPAREN expresion RPAREN bloque alternativa
 63. alternativa → ALIF LPAREN expresion RPAREN bloque alternativa
 64. alternativa → ALSO bloque
 65. alternativa → ε
 66. ciclo → WHALE LPAREN expresion RPAREN bloque STOP SEMICOLON
 67. retorno → GIVE resto_retorno
 68. resto_retorno → expresion SEMICOLON
 69. resto_retorno → SEMICOLON
 70. recorrido → CYCLE IDENTIFIER LET expresion UNTIL expresion paso_opcional bloque ENDGAME SEMICOLON
 71. paso_opcional → STEP expresion
 72. paso_opcional → ε
 73. intentar → SEEK bloque captura mas_capturas
 74. mas_capturas → captura mas_capturas
 75. mas_capturas → ε
 76. captura → SEIZE LPAREN IDENTIFIER IDENTIFIER RPAREN bloque
 77. argumentos_opcionales → expresion mas_argumentos
 78. argumentos_opcionales → ε
 79. mas_argumentos → COMMA expresion mas_argumentos
 80. mas_argumentos → ε
 81. expresion → exclusiva resto_or
 82. resto_or → OR exclusiva resto_or
 83. resto_or → ε
 84. exclusiva → conjuncion resto_xor
 85. resto_xor → XOR conjuncion resto_xor
 86. resto_xor → ε
 87. conjuncion → comparacion resto_and
 88. resto_and → AND comparacion resto_and
 89. resto_and → ε
 90. comparacion → suma resto_comparacion
 91. resto_comparacion → operador_relacional suma
 92. resto_comparacion → ε
 93. operador_relacional → EQUAL
 94. operador_relacional → NOT_EQUAL
 95. operador_relacional → LESS
 96. operador_relacional → GREATER
 97. operador_relacional → LESS_EQUAL
 98. operador_relacional → GREATER_EQUAL
 99. suma → producto resto_suma
100. resto_suma → GAUSS producto resto_suma
101. resto_suma → NEUMANN producto resto_suma
102. resto_suma → ε
103. producto → unario resto_producto
104. resto_producto → PITAGORAS unario resto_producto
105. resto_producto → EUCLIDES unario resto_producto
106. resto_producto → EULER unario resto_producto
107. resto_producto → ε
108. unario → NOT unario
109. unario → NEUMANN unario
110. unario → potencia
111. potencia → primario resto_potencia
112. resto_potencia → DESCARTES unario
113. resto_potencia → ε
114. primario → INTEGER
115. primario → STRING
116. primario → CHARACTER
117. primario → DECLARE_TRUE
118. primario → DECLARE_FALSE
119. primario → LPAREN expresion RPAREN
120. primario → LBRACKET elementos_opcionales RBRACKET
121. primario → IDENTIFIER sufijos_referencia llamada_opcional
122. primario → llamada_especial
123. sufijos_referencia → DOT IDENTIFIER sufijos_referencia
124. sufijos_referencia → LBRACKET expresion RBRACKET sufijos_referencia
125. sufijos_referencia → ε
126. llamada_opcional → LPAREN argumentos_opcionales RPAREN
127. llamada_opcional → ε
128. elementos_opcionales → expresion mas_elementos_lista
129. elementos_opcionales → ε
130. mas_elementos_lista → COMMA expresion mas_elementos_lista
131. mas_elementos_lista → ε

## PRIMERO y SIGUIENTE

| No terminal | PRIMERO | SIGUIENTE |
|---|---|---|
| programa | { `DECLARE_CONST`, `CREATE_FUNK`, `BRING` } | { `$` } |
| mas_elementos | { `DECLARE_CONST`, `CREATE_FUNK`, `BRING`, ε } | { `$` } |
| elemento_superior | { `DECLARE_CONST`, `CREATE_FUNK`, `BRING` } | { `$`, `DECLARE_CONST`, `CREATE_FUNK`, `BRING` } |
| funcion | { `CREATE_FUNK` } | { `$`, `DECLARE_CONST`, `CREATE_FUNK`, `BRING` } |
| tipo_retorno | { `DECLARE_INT`, `DECLARE_BOOLEAN`, `DECLARE_TEXT`, `DECLARE_CHAR`, `DECLARE_INFINITE_VOID` } | { `HASH` } |
| nombre_funcion | { `IDENTIFIER`, `MAIN` } | { `LPAREN` } |
| parametros_opcionales | { `IDENTIFIER`, ε } | { `RPAREN` } |
| mas_parametros | { `COMMA`, ε } | { `RPAREN` } |
| parametro | { `IDENTIFIER` } | { `RPAREN`, `COMMA` } |
| tipo_parametro | { `DECLARE_LIST`, `DECLARE_INT`, `DECLARE_BOOLEAN`, `DECLARE_TEXT`, `DECLARE_CHAR` } | { `RPAREN`, `COMMA` } |
| dimensiones_parametro | { `LBRACKET`, ε } | { `RPAREN`, `COMMA` } |
| dimension_parametro_resto | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `NEUMANN`, `NOT`, `LPAREN`, `LBRACKET`, `RBRACKET` } | { `RPAREN`, `LBRACKET`, `COMMA` } |
| importacion | { `BRING` } | { `$`, `DECLARE_CONST`, `CREATE_FUNK`, `BRING` } |
| alias_opcional | { `AKA`, ε } | { `SEMICOLON` } |
| constante | { `DECLARE_CONST` } | { `$`, `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `CREATE_FUNK`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `BRING`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| tipo | { `DECLARE_INT`, `DECLARE_BOOLEAN`, `DECLARE_TEXT`, `DECLARE_CHAR` } | { `ASSIGN`, `RPAREN`, `LBRACKET`, `COMMA`, `SEMICOLON`, `HASH` } |
| tipo_variable | { `DECLARE_INT`, `DECLARE_BOOLEAN`, `DECLARE_TEXT`, `DECLARE_CHAR` } | { `ASSIGN`, `COMMA`, `SEMICOLON` } |
| dimensiones | { `LBRACKET`, ε } | { `ASSIGN`, `COMMA`, `SEMICOLON` } |
| bloque | { `LBRACE` } | { `$`, `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `CREATE_FUNK`, `GIVE`, `WHETHER`, `ALIF`, `ALSO`, `WHALE`, `STOP`, `CYCLE`, `ENDGAME`, `BRING`, `SEEK`, `SEIZE`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| sentencias | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, ε } | { `RBRACE` } |
| sentencia | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE` } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| resto_identificador | { `ASSIGN`, `DOT`, `LPAREN`, `LBRACKET`, `STAR` } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| resto_referencia | { `ASSIGN`, `LPAREN` } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| mas_declaradores | { `COMMA`, ε } | { `SEMICOLON` } |
| inicializador_opcional | { `ASSIGN`, ε } | { `COMMA`, `SEMICOLON` } |
| llamada_especial | { `MAIN`, `ADD`, `REMOVE`, `SIZE` } | { `UNTIL`, `STEP`, `GAUSS`, `NEUMANN`, `PITAGORAS`, `EUCLIDES`, `EULER`, `DESCARTES`, `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| declaracion_lista | { `DECLARE_LIST` } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| declarador_lista | { `IDENTIFIER` } | { `COMMA`, `SEMICOLON` } |
| mas_listas | { `COMMA`, ε } | { `SEMICOLON` } |
| condicional | { `WHETHER` } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| alternativa | { `ALIF`, `ALSO`, ε } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| ciclo | { `WHALE` } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| retorno | { `GIVE` } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| resto_retorno | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `NEUMANN`, `NOT`, `LPAREN`, `LBRACKET`, `SEMICOLON` } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| recorrido | { `CYCLE` } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| paso_opcional | { `STEP`, ε } | { `LBRACE` } |
| intentar | { `SEEK` } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| mas_capturas | { `SEIZE`, ε } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| captura | { `SEIZE` } | { `IDENTIFIER`, `MAIN`, `DECLARE_CONST`, `GIVE`, `WHETHER`, `WHALE`, `CYCLE`, `SEEK`, `SEIZE`, `DECLARE_LIST`, `ADD`, `REMOVE`, `SIZE`, `LBRACE`, `RBRACE` } |
| argumentos_opcionales | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `NEUMANN`, `NOT`, `LPAREN`, `LBRACKET`, ε } | { `RPAREN` } |
| mas_argumentos | { `COMMA`, ε } | { `RPAREN` } |
| expresion | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `NEUMANN`, `NOT`, `LPAREN`, `LBRACKET` } | { `UNTIL`, `STEP`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| resto_or | { `OR`, ε } | { `UNTIL`, `STEP`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| exclusiva | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `NEUMANN`, `NOT`, `LPAREN`, `LBRACKET` } | { `UNTIL`, `STEP`, `OR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| resto_xor | { `XOR`, ε } | { `UNTIL`, `STEP`, `OR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| conjuncion | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `NEUMANN`, `NOT`, `LPAREN`, `LBRACKET` } | { `UNTIL`, `STEP`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| resto_and | { `AND`, ε } | { `UNTIL`, `STEP`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| comparacion | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `NEUMANN`, `NOT`, `LPAREN`, `LBRACKET` } | { `UNTIL`, `STEP`, `AND`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| resto_comparacion | { `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, ε } | { `UNTIL`, `STEP`, `AND`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| operador_relacional | { `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL` } | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `NEUMANN`, `NOT`, `LPAREN`, `LBRACKET` } |
| suma | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `NEUMANN`, `NOT`, `LPAREN`, `LBRACKET` } | { `UNTIL`, `STEP`, `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| resto_suma | { `GAUSS`, `NEUMANN`, ε } | { `UNTIL`, `STEP`, `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| producto | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `NEUMANN`, `NOT`, `LPAREN`, `LBRACKET` } | { `UNTIL`, `STEP`, `GAUSS`, `NEUMANN`, `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| resto_producto | { `PITAGORAS`, `EUCLIDES`, `EULER`, ε } | { `UNTIL`, `STEP`, `GAUSS`, `NEUMANN`, `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| unario | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `NEUMANN`, `NOT`, `LPAREN`, `LBRACKET` } | { `UNTIL`, `STEP`, `GAUSS`, `NEUMANN`, `PITAGORAS`, `EUCLIDES`, `EULER`, `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| potencia | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `LPAREN`, `LBRACKET` } | { `UNTIL`, `STEP`, `GAUSS`, `NEUMANN`, `PITAGORAS`, `EUCLIDES`, `EULER`, `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| resto_potencia | { `DESCARTES`, ε } | { `UNTIL`, `STEP`, `GAUSS`, `NEUMANN`, `PITAGORAS`, `EUCLIDES`, `EULER`, `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| primario | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `LPAREN`, `LBRACKET` } | { `UNTIL`, `STEP`, `GAUSS`, `NEUMANN`, `PITAGORAS`, `EUCLIDES`, `EULER`, `DESCARTES`, `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| sufijos_referencia | { `DOT`, `LBRACKET`, ε } | { `UNTIL`, `STEP`, `GAUSS`, `NEUMANN`, `PITAGORAS`, `EUCLIDES`, `EULER`, `DESCARTES`, `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `XOR`, `ASSIGN`, `LPAREN`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| llamada_opcional | { `LPAREN`, ε } | { `UNTIL`, `STEP`, `GAUSS`, `NEUMANN`, `PITAGORAS`, `EUCLIDES`, `EULER`, `DESCARTES`, `EQUAL`, `NOT_EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `AND`, `OR`, `XOR`, `RPAREN`, `RBRACKET`, `LBRACE`, `COMMA`, `SEMICOLON` } |
| elementos_opcionales | { `IDENTIFIER`, `INTEGER`, `STRING`, `CHARACTER`, `MAIN`, `ADD`, `REMOVE`, `SIZE`, `DECLARE_FALSE`, `DECLARE_TRUE`, `NEUMANN`, `NOT`, `LPAREN`, `LBRACKET`, ε } | { `RBRACKET` } |
| mas_elementos_lista | { `COMMA`, ε } | { `RBRACKET` } |

## Tabla M[A, a]

Solo se listan las celdas con producción; las demás son error.

| A | a | M[A, a] |
|---|---|---|
| programa | `DECLARE_CONST` | programa → elemento_superior mas_elementos |
| programa | `CREATE_FUNK` | programa → elemento_superior mas_elementos |
| programa | `BRING` | programa → elemento_superior mas_elementos |
| mas_elementos | `$` | mas_elementos → ε |
| mas_elementos | `DECLARE_CONST` | mas_elementos → elemento_superior mas_elementos |
| mas_elementos | `CREATE_FUNK` | mas_elementos → elemento_superior mas_elementos |
| mas_elementos | `BRING` | mas_elementos → elemento_superior mas_elementos |
| elemento_superior | `DECLARE_CONST` | elemento_superior → constante |
| elemento_superior | `CREATE_FUNK` | elemento_superior → funcion |
| elemento_superior | `BRING` | elemento_superior → importacion |
| funcion | `CREATE_FUNK` | funcion → CREATE_FUNK HASH tipo_retorno HASH nombre_funcion LPAREN parametros_opcionales RPAREN bloque |
| tipo_retorno | `DECLARE_INT` | tipo_retorno → tipo |
| tipo_retorno | `DECLARE_BOOLEAN` | tipo_retorno → tipo |
| tipo_retorno | `DECLARE_TEXT` | tipo_retorno → tipo |
| tipo_retorno | `DECLARE_CHAR` | tipo_retorno → tipo |
| tipo_retorno | `DECLARE_INFINITE_VOID` | tipo_retorno → DECLARE_INFINITE_VOID |
| nombre_funcion | `IDENTIFIER` | nombre_funcion → IDENTIFIER |
| nombre_funcion | `MAIN` | nombre_funcion → MAIN |
| parametros_opcionales | `IDENTIFIER` | parametros_opcionales → parametro mas_parametros |
| parametros_opcionales | `RPAREN` | parametros_opcionales → ε |
| mas_parametros | `RPAREN` | mas_parametros → ε |
| mas_parametros | `COMMA` | mas_parametros → COMMA parametro mas_parametros |
| parametro | `IDENTIFIER` | parametro → IDENTIFIER STAR tipo_parametro |
| tipo_parametro | `DECLARE_LIST` | tipo_parametro → DECLARE_LIST tipo |
| tipo_parametro | `DECLARE_INT` | tipo_parametro → tipo dimensiones_parametro |
| tipo_parametro | `DECLARE_BOOLEAN` | tipo_parametro → tipo dimensiones_parametro |
| tipo_parametro | `DECLARE_TEXT` | tipo_parametro → tipo dimensiones_parametro |
| tipo_parametro | `DECLARE_CHAR` | tipo_parametro → tipo dimensiones_parametro |
| dimensiones_parametro | `RPAREN` | dimensiones_parametro → ε |
| dimensiones_parametro | `LBRACKET` | dimensiones_parametro → LBRACKET dimension_parametro_resto dimensiones_parametro |
| dimensiones_parametro | `COMMA` | dimensiones_parametro → ε |
| dimension_parametro_resto | `IDENTIFIER` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `INTEGER` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `STRING` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `CHARACTER` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `MAIN` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `ADD` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `REMOVE` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `SIZE` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `DECLARE_FALSE` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `DECLARE_TRUE` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `NEUMANN` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `NOT` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `LPAREN` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `LBRACKET` | dimension_parametro_resto → expresion RBRACKET |
| dimension_parametro_resto | `RBRACKET` | dimension_parametro_resto → RBRACKET |
| importacion | `BRING` | importacion → BRING IDENTIFIER alias_opcional SEMICOLON |
| alias_opcional | `AKA` | alias_opcional → AKA IDENTIFIER |
| alias_opcional | `SEMICOLON` | alias_opcional → ε |
| constante | `DECLARE_CONST` | constante → DECLARE_CONST IDENTIFIER STAR tipo_variable ASSIGN expresion SEMICOLON |
| tipo | `DECLARE_INT` | tipo → DECLARE_INT |
| tipo | `DECLARE_BOOLEAN` | tipo → DECLARE_BOOLEAN |
| tipo | `DECLARE_TEXT` | tipo → DECLARE_TEXT |
| tipo | `DECLARE_CHAR` | tipo → DECLARE_CHAR |
| tipo_variable | `DECLARE_INT` | tipo_variable → tipo dimensiones |
| tipo_variable | `DECLARE_BOOLEAN` | tipo_variable → tipo dimensiones |
| tipo_variable | `DECLARE_TEXT` | tipo_variable → tipo dimensiones |
| tipo_variable | `DECLARE_CHAR` | tipo_variable → tipo dimensiones |
| dimensiones | `ASSIGN` | dimensiones → ε |
| dimensiones | `LBRACKET` | dimensiones → LBRACKET expresion RBRACKET dimensiones |
| dimensiones | `COMMA` | dimensiones → ε |
| dimensiones | `SEMICOLON` | dimensiones → ε |
| bloque | `LBRACE` | bloque → LBRACE sentencias RBRACE |
| sentencias | `IDENTIFIER` | sentencias → sentencia sentencias |
| sentencias | `MAIN` | sentencias → sentencia sentencias |
| sentencias | `DECLARE_CONST` | sentencias → sentencia sentencias |
| sentencias | `GIVE` | sentencias → sentencia sentencias |
| sentencias | `WHETHER` | sentencias → sentencia sentencias |
| sentencias | `WHALE` | sentencias → sentencia sentencias |
| sentencias | `CYCLE` | sentencias → sentencia sentencias |
| sentencias | `SEEK` | sentencias → sentencia sentencias |
| sentencias | `DECLARE_LIST` | sentencias → sentencia sentencias |
| sentencias | `ADD` | sentencias → sentencia sentencias |
| sentencias | `REMOVE` | sentencias → sentencia sentencias |
| sentencias | `SIZE` | sentencias → sentencia sentencias |
| sentencias | `LBRACE` | sentencias → sentencia sentencias |
| sentencias | `RBRACE` | sentencias → ε |
| sentencia | `IDENTIFIER` | sentencia → IDENTIFIER resto_identificador |
| sentencia | `MAIN` | sentencia → llamada_especial SEMICOLON |
| sentencia | `DECLARE_CONST` | sentencia → constante |
| sentencia | `GIVE` | sentencia → retorno |
| sentencia | `WHETHER` | sentencia → condicional |
| sentencia | `WHALE` | sentencia → ciclo |
| sentencia | `CYCLE` | sentencia → recorrido |
| sentencia | `SEEK` | sentencia → intentar |
| sentencia | `DECLARE_LIST` | sentencia → declaracion_lista |
| sentencia | `ADD` | sentencia → llamada_especial SEMICOLON |
| sentencia | `REMOVE` | sentencia → llamada_especial SEMICOLON |
| sentencia | `SIZE` | sentencia → llamada_especial SEMICOLON |
| sentencia | `LBRACE` | sentencia → bloque |
| resto_identificador | `ASSIGN` | resto_identificador → sufijos_referencia resto_referencia |
| resto_identificador | `DOT` | resto_identificador → sufijos_referencia resto_referencia |
| resto_identificador | `LPAREN` | resto_identificador → sufijos_referencia resto_referencia |
| resto_identificador | `LBRACKET` | resto_identificador → sufijos_referencia resto_referencia |
| resto_identificador | `STAR` | resto_identificador → STAR tipo_variable inicializador_opcional mas_declaradores SEMICOLON |
| resto_referencia | `ASSIGN` | resto_referencia → ASSIGN expresion SEMICOLON |
| resto_referencia | `LPAREN` | resto_referencia → LPAREN argumentos_opcionales RPAREN SEMICOLON |
| mas_declaradores | `COMMA` | mas_declaradores → COMMA IDENTIFIER STAR tipo_variable inicializador_opcional mas_declaradores |
| mas_declaradores | `SEMICOLON` | mas_declaradores → ε |
| inicializador_opcional | `ASSIGN` | inicializador_opcional → ASSIGN expresion |
| inicializador_opcional | `COMMA` | inicializador_opcional → ε |
| inicializador_opcional | `SEMICOLON` | inicializador_opcional → ε |
| llamada_especial | `MAIN` | llamada_especial → MAIN LPAREN argumentos_opcionales RPAREN |
| llamada_especial | `ADD` | llamada_especial → ADD LPAREN expresion COMMA expresion RPAREN |
| llamada_especial | `REMOVE` | llamada_especial → REMOVE LPAREN expresion COMMA expresion RPAREN |
| llamada_especial | `SIZE` | llamada_especial → SIZE LPAREN expresion RPAREN |
| declaracion_lista | `DECLARE_LIST` | declaracion_lista → DECLARE_LIST declarador_lista mas_listas SEMICOLON |
| declarador_lista | `IDENTIFIER` | declarador_lista → IDENTIFIER STAR tipo inicializador_opcional |
| mas_listas | `COMMA` | mas_listas → COMMA declarador_lista mas_listas |
| mas_listas | `SEMICOLON` | mas_listas → ε |
| condicional | `WHETHER` | condicional → WHETHER LPAREN expresion RPAREN bloque alternativa |
| alternativa | `IDENTIFIER` | alternativa → ε |
| alternativa | `MAIN` | alternativa → ε |
| alternativa | `DECLARE_CONST` | alternativa → ε |
| alternativa | `GIVE` | alternativa → ε |
| alternativa | `WHETHER` | alternativa → ε |
| alternativa | `ALIF` | alternativa → ALIF LPAREN expresion RPAREN bloque alternativa |
| alternativa | `ALSO` | alternativa → ALSO bloque |
| alternativa | `WHALE` | alternativa → ε |
| alternativa | `CYCLE` | alternativa → ε |
| alternativa | `SEEK` | alternativa → ε |
| alternativa | `DECLARE_LIST` | alternativa → ε |
| alternativa | `ADD` | alternativa → ε |
| alternativa | `REMOVE` | alternativa → ε |
| alternativa | `SIZE` | alternativa → ε |
| alternativa | `LBRACE` | alternativa → ε |
| alternativa | `RBRACE` | alternativa → ε |
| ciclo | `WHALE` | ciclo → WHALE LPAREN expresion RPAREN bloque STOP SEMICOLON |
| retorno | `GIVE` | retorno → GIVE resto_retorno |
| resto_retorno | `IDENTIFIER` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `INTEGER` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `STRING` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `CHARACTER` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `MAIN` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `ADD` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `REMOVE` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `SIZE` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `DECLARE_FALSE` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `DECLARE_TRUE` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `NEUMANN` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `NOT` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `LPAREN` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `LBRACKET` | resto_retorno → expresion SEMICOLON |
| resto_retorno | `SEMICOLON` | resto_retorno → SEMICOLON |
| recorrido | `CYCLE` | recorrido → CYCLE IDENTIFIER LET expresion UNTIL expresion paso_opcional bloque ENDGAME SEMICOLON |
| paso_opcional | `STEP` | paso_opcional → STEP expresion |
| paso_opcional | `LBRACE` | paso_opcional → ε |
| intentar | `SEEK` | intentar → SEEK bloque captura mas_capturas |
| mas_capturas | `IDENTIFIER` | mas_capturas → ε |
| mas_capturas | `MAIN` | mas_capturas → ε |
| mas_capturas | `DECLARE_CONST` | mas_capturas → ε |
| mas_capturas | `GIVE` | mas_capturas → ε |
| mas_capturas | `WHETHER` | mas_capturas → ε |
| mas_capturas | `WHALE` | mas_capturas → ε |
| mas_capturas | `CYCLE` | mas_capturas → ε |
| mas_capturas | `SEEK` | mas_capturas → ε |
| mas_capturas | `SEIZE` | mas_capturas → captura mas_capturas |
| mas_capturas | `DECLARE_LIST` | mas_capturas → ε |
| mas_capturas | `ADD` | mas_capturas → ε |
| mas_capturas | `REMOVE` | mas_capturas → ε |
| mas_capturas | `SIZE` | mas_capturas → ε |
| mas_capturas | `LBRACE` | mas_capturas → ε |
| mas_capturas | `RBRACE` | mas_capturas → ε |
| captura | `SEIZE` | captura → SEIZE LPAREN IDENTIFIER IDENTIFIER RPAREN bloque |
| argumentos_opcionales | `IDENTIFIER` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `INTEGER` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `STRING` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `CHARACTER` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `MAIN` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `ADD` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `REMOVE` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `SIZE` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `DECLARE_FALSE` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `DECLARE_TRUE` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `NEUMANN` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `NOT` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `LPAREN` | argumentos_opcionales → expresion mas_argumentos |
| argumentos_opcionales | `RPAREN` | argumentos_opcionales → ε |
| argumentos_opcionales | `LBRACKET` | argumentos_opcionales → expresion mas_argumentos |
| mas_argumentos | `RPAREN` | mas_argumentos → ε |
| mas_argumentos | `COMMA` | mas_argumentos → COMMA expresion mas_argumentos |
| expresion | `IDENTIFIER` | expresion → exclusiva resto_or |
| expresion | `INTEGER` | expresion → exclusiva resto_or |
| expresion | `STRING` | expresion → exclusiva resto_or |
| expresion | `CHARACTER` | expresion → exclusiva resto_or |
| expresion | `MAIN` | expresion → exclusiva resto_or |
| expresion | `ADD` | expresion → exclusiva resto_or |
| expresion | `REMOVE` | expresion → exclusiva resto_or |
| expresion | `SIZE` | expresion → exclusiva resto_or |
| expresion | `DECLARE_FALSE` | expresion → exclusiva resto_or |
| expresion | `DECLARE_TRUE` | expresion → exclusiva resto_or |
| expresion | `NEUMANN` | expresion → exclusiva resto_or |
| expresion | `NOT` | expresion → exclusiva resto_or |
| expresion | `LPAREN` | expresion → exclusiva resto_or |
| expresion | `LBRACKET` | expresion → exclusiva resto_or |
| resto_or | `UNTIL` | resto_or → ε |
| resto_or | `STEP` | resto_or → ε |
| resto_or | `OR` | resto_or → OR exclusiva resto_or |
| resto_or | `RPAREN` | resto_or → ε |
| resto_or | `RBRACKET` | resto_or → ε |
| resto_or | `LBRACE` | resto_or → ε |
| resto_or | `COMMA` | resto_or → ε |
| resto_or | `SEMICOLON` | resto_or → ε |
| exclusiva | `IDENTIFIER` | exclusiva → conjuncion resto_xor |
| exclusiva | `INTEGER` | exclusiva → conjuncion resto_xor |
| exclusiva | `STRING` | exclusiva → conjuncion resto_xor |
| exclusiva | `CHARACTER` | exclusiva → conjuncion resto_xor |
| exclusiva | `MAIN` | exclusiva → conjuncion resto_xor |
| exclusiva | `ADD` | exclusiva → conjuncion resto_xor |
| exclusiva | `REMOVE` | exclusiva → conjuncion resto_xor |
| exclusiva | `SIZE` | exclusiva → conjuncion resto_xor |
| exclusiva | `DECLARE_FALSE` | exclusiva → conjuncion resto_xor |
| exclusiva | `DECLARE_TRUE` | exclusiva → conjuncion resto_xor |
| exclusiva | `NEUMANN` | exclusiva → conjuncion resto_xor |
| exclusiva | `NOT` | exclusiva → conjuncion resto_xor |
| exclusiva | `LPAREN` | exclusiva → conjuncion resto_xor |
| exclusiva | `LBRACKET` | exclusiva → conjuncion resto_xor |
| resto_xor | `UNTIL` | resto_xor → ε |
| resto_xor | `STEP` | resto_xor → ε |
| resto_xor | `OR` | resto_xor → ε |
| resto_xor | `XOR` | resto_xor → XOR conjuncion resto_xor |
| resto_xor | `RPAREN` | resto_xor → ε |
| resto_xor | `RBRACKET` | resto_xor → ε |
| resto_xor | `LBRACE` | resto_xor → ε |
| resto_xor | `COMMA` | resto_xor → ε |
| resto_xor | `SEMICOLON` | resto_xor → ε |
| conjuncion | `IDENTIFIER` | conjuncion → comparacion resto_and |
| conjuncion | `INTEGER` | conjuncion → comparacion resto_and |
| conjuncion | `STRING` | conjuncion → comparacion resto_and |
| conjuncion | `CHARACTER` | conjuncion → comparacion resto_and |
| conjuncion | `MAIN` | conjuncion → comparacion resto_and |
| conjuncion | `ADD` | conjuncion → comparacion resto_and |
| conjuncion | `REMOVE` | conjuncion → comparacion resto_and |
| conjuncion | `SIZE` | conjuncion → comparacion resto_and |
| conjuncion | `DECLARE_FALSE` | conjuncion → comparacion resto_and |
| conjuncion | `DECLARE_TRUE` | conjuncion → comparacion resto_and |
| conjuncion | `NEUMANN` | conjuncion → comparacion resto_and |
| conjuncion | `NOT` | conjuncion → comparacion resto_and |
| conjuncion | `LPAREN` | conjuncion → comparacion resto_and |
| conjuncion | `LBRACKET` | conjuncion → comparacion resto_and |
| resto_and | `UNTIL` | resto_and → ε |
| resto_and | `STEP` | resto_and → ε |
| resto_and | `AND` | resto_and → AND comparacion resto_and |
| resto_and | `OR` | resto_and → ε |
| resto_and | `XOR` | resto_and → ε |
| resto_and | `RPAREN` | resto_and → ε |
| resto_and | `RBRACKET` | resto_and → ε |
| resto_and | `LBRACE` | resto_and → ε |
| resto_and | `COMMA` | resto_and → ε |
| resto_and | `SEMICOLON` | resto_and → ε |
| comparacion | `IDENTIFIER` | comparacion → suma resto_comparacion |
| comparacion | `INTEGER` | comparacion → suma resto_comparacion |
| comparacion | `STRING` | comparacion → suma resto_comparacion |
| comparacion | `CHARACTER` | comparacion → suma resto_comparacion |
| comparacion | `MAIN` | comparacion → suma resto_comparacion |
| comparacion | `ADD` | comparacion → suma resto_comparacion |
| comparacion | `REMOVE` | comparacion → suma resto_comparacion |
| comparacion | `SIZE` | comparacion → suma resto_comparacion |
| comparacion | `DECLARE_FALSE` | comparacion → suma resto_comparacion |
| comparacion | `DECLARE_TRUE` | comparacion → suma resto_comparacion |
| comparacion | `NEUMANN` | comparacion → suma resto_comparacion |
| comparacion | `NOT` | comparacion → suma resto_comparacion |
| comparacion | `LPAREN` | comparacion → suma resto_comparacion |
| comparacion | `LBRACKET` | comparacion → suma resto_comparacion |
| resto_comparacion | `UNTIL` | resto_comparacion → ε |
| resto_comparacion | `STEP` | resto_comparacion → ε |
| resto_comparacion | `EQUAL` | resto_comparacion → operador_relacional suma |
| resto_comparacion | `NOT_EQUAL` | resto_comparacion → operador_relacional suma |
| resto_comparacion | `LESS` | resto_comparacion → operador_relacional suma |
| resto_comparacion | `GREATER` | resto_comparacion → operador_relacional suma |
| resto_comparacion | `LESS_EQUAL` | resto_comparacion → operador_relacional suma |
| resto_comparacion | `GREATER_EQUAL` | resto_comparacion → operador_relacional suma |
| resto_comparacion | `AND` | resto_comparacion → ε |
| resto_comparacion | `OR` | resto_comparacion → ε |
| resto_comparacion | `XOR` | resto_comparacion → ε |
| resto_comparacion | `RPAREN` | resto_comparacion → ε |
| resto_comparacion | `RBRACKET` | resto_comparacion → ε |
| resto_comparacion | `LBRACE` | resto_comparacion → ε |
| resto_comparacion | `COMMA` | resto_comparacion → ε |
| resto_comparacion | `SEMICOLON` | resto_comparacion → ε |
| operador_relacional | `EQUAL` | operador_relacional → EQUAL |
| operador_relacional | `NOT_EQUAL` | operador_relacional → NOT_EQUAL |
| operador_relacional | `LESS` | operador_relacional → LESS |
| operador_relacional | `GREATER` | operador_relacional → GREATER |
| operador_relacional | `LESS_EQUAL` | operador_relacional → LESS_EQUAL |
| operador_relacional | `GREATER_EQUAL` | operador_relacional → GREATER_EQUAL |
| suma | `IDENTIFIER` | suma → producto resto_suma |
| suma | `INTEGER` | suma → producto resto_suma |
| suma | `STRING` | suma → producto resto_suma |
| suma | `CHARACTER` | suma → producto resto_suma |
| suma | `MAIN` | suma → producto resto_suma |
| suma | `ADD` | suma → producto resto_suma |
| suma | `REMOVE` | suma → producto resto_suma |
| suma | `SIZE` | suma → producto resto_suma |
| suma | `DECLARE_FALSE` | suma → producto resto_suma |
| suma | `DECLARE_TRUE` | suma → producto resto_suma |
| suma | `NEUMANN` | suma → producto resto_suma |
| suma | `NOT` | suma → producto resto_suma |
| suma | `LPAREN` | suma → producto resto_suma |
| suma | `LBRACKET` | suma → producto resto_suma |
| resto_suma | `UNTIL` | resto_suma → ε |
| resto_suma | `STEP` | resto_suma → ε |
| resto_suma | `GAUSS` | resto_suma → GAUSS producto resto_suma |
| resto_suma | `NEUMANN` | resto_suma → NEUMANN producto resto_suma |
| resto_suma | `EQUAL` | resto_suma → ε |
| resto_suma | `NOT_EQUAL` | resto_suma → ε |
| resto_suma | `LESS` | resto_suma → ε |
| resto_suma | `GREATER` | resto_suma → ε |
| resto_suma | `LESS_EQUAL` | resto_suma → ε |
| resto_suma | `GREATER_EQUAL` | resto_suma → ε |
| resto_suma | `AND` | resto_suma → ε |
| resto_suma | `OR` | resto_suma → ε |
| resto_suma | `XOR` | resto_suma → ε |
| resto_suma | `RPAREN` | resto_suma → ε |
| resto_suma | `RBRACKET` | resto_suma → ε |
| resto_suma | `LBRACE` | resto_suma → ε |
| resto_suma | `COMMA` | resto_suma → ε |
| resto_suma | `SEMICOLON` | resto_suma → ε |
| producto | `IDENTIFIER` | producto → unario resto_producto |
| producto | `INTEGER` | producto → unario resto_producto |
| producto | `STRING` | producto → unario resto_producto |
| producto | `CHARACTER` | producto → unario resto_producto |
| producto | `MAIN` | producto → unario resto_producto |
| producto | `ADD` | producto → unario resto_producto |
| producto | `REMOVE` | producto → unario resto_producto |
| producto | `SIZE` | producto → unario resto_producto |
| producto | `DECLARE_FALSE` | producto → unario resto_producto |
| producto | `DECLARE_TRUE` | producto → unario resto_producto |
| producto | `NEUMANN` | producto → unario resto_producto |
| producto | `NOT` | producto → unario resto_producto |
| producto | `LPAREN` | producto → unario resto_producto |
| producto | `LBRACKET` | producto → unario resto_producto |
| resto_producto | `UNTIL` | resto_producto → ε |
| resto_producto | `STEP` | resto_producto → ε |
| resto_producto | `GAUSS` | resto_producto → ε |
| resto_producto | `NEUMANN` | resto_producto → ε |
| resto_producto | `PITAGORAS` | resto_producto → PITAGORAS unario resto_producto |
| resto_producto | `EUCLIDES` | resto_producto → EUCLIDES unario resto_producto |
| resto_producto | `EULER` | resto_producto → EULER unario resto_producto |
| resto_producto | `EQUAL` | resto_producto → ε |
| resto_producto | `NOT_EQUAL` | resto_producto → ε |
| resto_producto | `LESS` | resto_producto → ε |
| resto_producto | `GREATER` | resto_producto → ε |
| resto_producto | `LESS_EQUAL` | resto_producto → ε |
| resto_producto | `GREATER_EQUAL` | resto_producto → ε |
| resto_producto | `AND` | resto_producto → ε |
| resto_producto | `OR` | resto_producto → ε |
| resto_producto | `XOR` | resto_producto → ε |
| resto_producto | `RPAREN` | resto_producto → ε |
| resto_producto | `RBRACKET` | resto_producto → ε |
| resto_producto | `LBRACE` | resto_producto → ε |
| resto_producto | `COMMA` | resto_producto → ε |
| resto_producto | `SEMICOLON` | resto_producto → ε |
| unario | `IDENTIFIER` | unario → potencia |
| unario | `INTEGER` | unario → potencia |
| unario | `STRING` | unario → potencia |
| unario | `CHARACTER` | unario → potencia |
| unario | `MAIN` | unario → potencia |
| unario | `ADD` | unario → potencia |
| unario | `REMOVE` | unario → potencia |
| unario | `SIZE` | unario → potencia |
| unario | `DECLARE_FALSE` | unario → potencia |
| unario | `DECLARE_TRUE` | unario → potencia |
| unario | `NEUMANN` | unario → NEUMANN unario |
| unario | `NOT` | unario → NOT unario |
| unario | `LPAREN` | unario → potencia |
| unario | `LBRACKET` | unario → potencia |
| potencia | `IDENTIFIER` | potencia → primario resto_potencia |
| potencia | `INTEGER` | potencia → primario resto_potencia |
| potencia | `STRING` | potencia → primario resto_potencia |
| potencia | `CHARACTER` | potencia → primario resto_potencia |
| potencia | `MAIN` | potencia → primario resto_potencia |
| potencia | `ADD` | potencia → primario resto_potencia |
| potencia | `REMOVE` | potencia → primario resto_potencia |
| potencia | `SIZE` | potencia → primario resto_potencia |
| potencia | `DECLARE_FALSE` | potencia → primario resto_potencia |
| potencia | `DECLARE_TRUE` | potencia → primario resto_potencia |
| potencia | `LPAREN` | potencia → primario resto_potencia |
| potencia | `LBRACKET` | potencia → primario resto_potencia |
| resto_potencia | `UNTIL` | resto_potencia → ε |
| resto_potencia | `STEP` | resto_potencia → ε |
| resto_potencia | `GAUSS` | resto_potencia → ε |
| resto_potencia | `NEUMANN` | resto_potencia → ε |
| resto_potencia | `PITAGORAS` | resto_potencia → ε |
| resto_potencia | `EUCLIDES` | resto_potencia → ε |
| resto_potencia | `EULER` | resto_potencia → ε |
| resto_potencia | `DESCARTES` | resto_potencia → DESCARTES unario |
| resto_potencia | `EQUAL` | resto_potencia → ε |
| resto_potencia | `NOT_EQUAL` | resto_potencia → ε |
| resto_potencia | `LESS` | resto_potencia → ε |
| resto_potencia | `GREATER` | resto_potencia → ε |
| resto_potencia | `LESS_EQUAL` | resto_potencia → ε |
| resto_potencia | `GREATER_EQUAL` | resto_potencia → ε |
| resto_potencia | `AND` | resto_potencia → ε |
| resto_potencia | `OR` | resto_potencia → ε |
| resto_potencia | `XOR` | resto_potencia → ε |
| resto_potencia | `RPAREN` | resto_potencia → ε |
| resto_potencia | `RBRACKET` | resto_potencia → ε |
| resto_potencia | `LBRACE` | resto_potencia → ε |
| resto_potencia | `COMMA` | resto_potencia → ε |
| resto_potencia | `SEMICOLON` | resto_potencia → ε |
| primario | `IDENTIFIER` | primario → IDENTIFIER sufijos_referencia llamada_opcional |
| primario | `INTEGER` | primario → INTEGER |
| primario | `STRING` | primario → STRING |
| primario | `CHARACTER` | primario → CHARACTER |
| primario | `MAIN` | primario → llamada_especial |
| primario | `ADD` | primario → llamada_especial |
| primario | `REMOVE` | primario → llamada_especial |
| primario | `SIZE` | primario → llamada_especial |
| primario | `DECLARE_FALSE` | primario → DECLARE_FALSE |
| primario | `DECLARE_TRUE` | primario → DECLARE_TRUE |
| primario | `LPAREN` | primario → LPAREN expresion RPAREN |
| primario | `LBRACKET` | primario → LBRACKET elementos_opcionales RBRACKET |
| sufijos_referencia | `UNTIL` | sufijos_referencia → ε |
| sufijos_referencia | `STEP` | sufijos_referencia → ε |
| sufijos_referencia | `GAUSS` | sufijos_referencia → ε |
| sufijos_referencia | `NEUMANN` | sufijos_referencia → ε |
| sufijos_referencia | `PITAGORAS` | sufijos_referencia → ε |
| sufijos_referencia | `EUCLIDES` | sufijos_referencia → ε |
| sufijos_referencia | `EULER` | sufijos_referencia → ε |
| sufijos_referencia | `DESCARTES` | sufijos_referencia → ε |
| sufijos_referencia | `EQUAL` | sufijos_referencia → ε |
| sufijos_referencia | `NOT_EQUAL` | sufijos_referencia → ε |
| sufijos_referencia | `LESS` | sufijos_referencia → ε |
| sufijos_referencia | `GREATER` | sufijos_referencia → ε |
| sufijos_referencia | `LESS_EQUAL` | sufijos_referencia → ε |
| sufijos_referencia | `GREATER_EQUAL` | sufijos_referencia → ε |
| sufijos_referencia | `AND` | sufijos_referencia → ε |
| sufijos_referencia | `OR` | sufijos_referencia → ε |
| sufijos_referencia | `XOR` | sufijos_referencia → ε |
| sufijos_referencia | `ASSIGN` | sufijos_referencia → ε |
| sufijos_referencia | `DOT` | sufijos_referencia → DOT IDENTIFIER sufijos_referencia |
| sufijos_referencia | `LPAREN` | sufijos_referencia → ε |
| sufijos_referencia | `RPAREN` | sufijos_referencia → ε |
| sufijos_referencia | `LBRACKET` | sufijos_referencia → LBRACKET expresion RBRACKET sufijos_referencia |
| sufijos_referencia | `RBRACKET` | sufijos_referencia → ε |
| sufijos_referencia | `LBRACE` | sufijos_referencia → ε |
| sufijos_referencia | `COMMA` | sufijos_referencia → ε |
| sufijos_referencia | `SEMICOLON` | sufijos_referencia → ε |
| llamada_opcional | `UNTIL` | llamada_opcional → ε |
| llamada_opcional | `STEP` | llamada_opcional → ε |
| llamada_opcional | `GAUSS` | llamada_opcional → ε |
| llamada_opcional | `NEUMANN` | llamada_opcional → ε |
| llamada_opcional | `PITAGORAS` | llamada_opcional → ε |
| llamada_opcional | `EUCLIDES` | llamada_opcional → ε |
| llamada_opcional | `EULER` | llamada_opcional → ε |
| llamada_opcional | `DESCARTES` | llamada_opcional → ε |
| llamada_opcional | `EQUAL` | llamada_opcional → ε |
| llamada_opcional | `NOT_EQUAL` | llamada_opcional → ε |
| llamada_opcional | `LESS` | llamada_opcional → ε |
| llamada_opcional | `GREATER` | llamada_opcional → ε |
| llamada_opcional | `LESS_EQUAL` | llamada_opcional → ε |
| llamada_opcional | `GREATER_EQUAL` | llamada_opcional → ε |
| llamada_opcional | `AND` | llamada_opcional → ε |
| llamada_opcional | `OR` | llamada_opcional → ε |
| llamada_opcional | `XOR` | llamada_opcional → ε |
| llamada_opcional | `LPAREN` | llamada_opcional → LPAREN argumentos_opcionales RPAREN |
| llamada_opcional | `RPAREN` | llamada_opcional → ε |
| llamada_opcional | `RBRACKET` | llamada_opcional → ε |
| llamada_opcional | `LBRACE` | llamada_opcional → ε |
| llamada_opcional | `COMMA` | llamada_opcional → ε |
| llamada_opcional | `SEMICOLON` | llamada_opcional → ε |
| elementos_opcionales | `IDENTIFIER` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `INTEGER` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `STRING` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `CHARACTER` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `MAIN` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `ADD` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `REMOVE` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `SIZE` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `DECLARE_FALSE` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `DECLARE_TRUE` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `NEUMANN` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `NOT` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `LPAREN` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `LBRACKET` | elementos_opcionales → expresion mas_elementos_lista |
| elementos_opcionales | `RBRACKET` | elementos_opcionales → ε |
| mas_elementos_lista | `RBRACKET` | mas_elementos_lista → ε |
| mas_elementos_lista | `COMMA` | mas_elementos_lista → COMMA expresion mas_elementos_lista |
