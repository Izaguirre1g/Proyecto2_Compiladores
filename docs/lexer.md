# Analizador léxico

## Alcance

El lexer implementa la primera fase del compilador y mantiene la misma interfaz pública
que ya utilizaban la CLI y el parser: `lexer_init` y `lexer_next`. La diferencia es que
el reconocimiento de patrones ya no se hace con una cadena de decisiones escrita a mano;
las reglas léxicas se declaran en `src/Analisis_Lexico/lexer.l` y Flex genera el scanner.

Esto no reemplaza el análisis sintáctico. Flex reconoce tokens; Bison usa esos tokens para
validar las derivaciones de la gramática y construir el AST. Por esa razón no se modifica
`src/Analisis_Sintactico/gramatica.y` ni `src/Analisis_Sintactico/puente_lexer.c`.

## Tokens reconocidos

| Categoría | Lexemas o regla |
|---|---|
| Entrada, declaración, funciones | `main`, `declare_const`, `create_funk`, `give`, `show` |
| Condicionales y ciclos | `whether`, `alif`, `also`, `whale`, `stop`, `cycle`, `let`, `until`, `step`, `endgame` |
| Módulos, errores, listas | `bring`, `aka`, `seek`, `seize`, `declare_list`, `add`, `remove`, `size` |
| Tipos | `declare_int`, `declare_boolean`, `declare_text`, `declare_char`, `declare_infinite_void` |
| Booleanos | `declare_true`, `declare_false` |
| Aritmética | `gauss`, `neumann`, `pitagoras`, `euclides`, `euler`, `descartes` |
| Relaciones | `==`, `=/=`, `<`, `>`, `<=`, `>=` |
| Lógica | `&&`, `\|\|`, `~`, `^` |
| Asignación y acceso | `:`, `.` |
| Delimitadores | `(`, `)`, `[`, `]`, `{`, `}`, `,`, `;`, `*`, `#` |
| Identificador | `[A-Za-z_][A-Za-z0-9_]*`; distingue mayúsculas |
| Entero | Uno o más dígitos ASCII; no se admiten reales |
| Cadena | Comillas rectas `"..."`, sin salto de línea ni interpretación de escapes |
| Carácter | `$` + un carácter Unicode UTF-8 + `$` |
| Comentarios | `%%` hasta fin de línea; `%%//` hasta el primer `//%%` |

La lista y los nombres de `TokenType` no cambian. Tampoco cambia la estructura `Token`,
los códigos de error ni la propiedad del campo `lexeme`. Esto permite que el puente hacia
Bison siga traduciendo exactamente los mismos `TOKEN_*`.

## Cómo funciona ahora

`src/Analisis_Lexico/lexer.l` contiene expresiones regulares y estados exclusivos para comentarios de línea
y de bloque. Flex decide qué regla coincide con la entrada. `src/Analisis_Lexico/lexer.c` queda como una capa
pequeña de compatibilidad: conserva el estado de línea, columna y offset, copia el lexema al
`Token` y mantiene el comportamiento de la API anterior.

Se usa un prefijo propio para los símbolos generados por Flex. Así el `yylex` generado por
Flex no entra en conflicto con el `yylex` que ya define `puente_lexer.c` para Bison.

Los comentarios y espacios se omiten antes de entregar el siguiente token. Un comentario de
bloque sin `//%%` produce `TOKEN_UNTERMINATED_COMMENT`. Los decimales y combinaciones como
`12abc` producen `TOKEN_INVALID_NUMBER`. Los identificadores con caracteres fuera de ASCII
mantienen el comportamiento anterior: el carácter UTF-8 no permitido se entrega como un error
recuperable y las posiciones continúan contando caracteres, no bytes.

## Compatibilidad con el parser

No se cambia `include/lexer.h`, `include/token.h`, `src/Analisis_Lexico/token.c`, la gramática de Bison ni el
puente léxico-sintáctico. El contrato que ve el parser sigue siendo:

```c
void lexer_init(Lexer *lexer, const char *source, size_t length,
                const char *filename);
Token lexer_next(Lexer *lexer);
```

Por eso el trabajo de análisis sintáctico y AST puede permanecer intacto. El cambio está
encapsulado dentro de la implementación del lexer y del proceso de construcción.

## Compilación y pruebas

En Ubuntu/WSL se requieren Flex y Bison:

```bash
sudo apt update
sudo apt install build-essential flex bison
make clean
make test
```

El Makefile genera `src/Analisis_Lexico/lexer_flex.c` desde `src/Analisis_Lexico/lexer.l`. La batería existente de
`tests/test_lexer.c` se conserva sin cambios; por lo tanto se siguen verificando las mismas
reservadas, operadores, delimitadores, comentarios, literales, posiciones, errores, UTF-8,
NUL y el ejemplo de factorial.

La validación de sintaxis continúa a cargo de Bison con `-t`. Flex no valida la gramática;
solamente transforma los bytes del fuente en la misma secuencia de tokens que recibía Bison.
