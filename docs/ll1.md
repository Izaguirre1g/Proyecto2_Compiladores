# Análisis predictivo LL(1)

El compilador tiene dos analizadores sintácticos para el mismo lenguaje:

| | Bison (`src/Analisis_Sintactico/`) | LL(1) (`src/LL1/`) |
|---|---|---|
| Tipo | Ascendente, LALR(1) | Descendente, predictivo LL(1) |
| Para qué se usa | Construir el AST que usan el semántico y la generación | Predecir qué tokens pueden venir en cada punto: sugerencias y correcciones del IDE |
| Gramática | `gramatica.y` (con recursividad izquierda y `%left`/`%right`) | `ll1_gramatica.c` (sin recursividad izquierda, factorizada) |

Los dos reconocen exactamente el mismo lenguaje. `make test-ll1` lo comprueba: compara los dos parsers sobre todos los `.bal` del repositorio y sobre unas 5000 variantes con errores (borrando o repitiendo un token). En cada variante revisa que los dos:

- digan lo mismo (válido o error);
- marquen el error en la misma línea y columna;
- esperen los mismos tokens cuando Bison los enumera (`expecting A or B`).

## 1. La gramática LL(1)

Se parte de la gramática de Bison y se aplican las transformaciones de las diapositivas de Análisis Sintáctico.

**Eliminación de la recursividad por la izquierda** (diapositiva 10). `A → A α | β` se cambia por `A → β A'` y `A' → α A' | ε`:

```
Bison:  programa → programa elemento_superior | elemento_superior
LL(1):  programa      → elemento_superior mas_elementos
        mas_elementos → elemento_superior mas_elementos | ε
```

Lo mismo se hizo con parámetros, sentencias, declaradores, listas, capturas, argumentos, dimensiones, referencias (`x[1].y`) y todas las expresiones binarias.

**Factorización por la izquierda** (diapositiva 11). Una declaración, una asignación y una llamada empiezan con un nombre, así que con un solo token no se sabe cuál es:

```
sentencia           → IDENTIFIER resto_identificador | ...
resto_identificador → STAR tipo_variable inicializador_opcional mas_declaradores SEMICOLON
                    | sufijos_referencia resto_referencia
resto_referencia    → ASSIGN expresion SEMICOLON
                    | LPAREN argumentos_opcionales RPAREN SEMICOLON
```

También se factorizaron `give expresion;` y `give;`, y las dos formas de dimensión de un parámetro (`[]` y `[n]`).

**Precedencia escrita en la gramática** (diapositivas 9 y 29). Bison resuelve la ambigüedad con `%left` y `%right`. En LL(1) se usa un no terminal por nivel, de menor a mayor precedencia:

```
expresion   (||)  → exclusiva resto_or
exclusiva   (^)   → conjuncion resto_xor
conjuncion  (&&)  → comparacion resto_and
comparacion       → suma resto_comparacion          (como mucho una: %nonassoc)
suma        (gauss, neumann)           → producto resto_suma
producto    (pitagoras, euclides, euler) → unario resto_producto
unario      (~, neumann)               → ~ unario | neumann unario | potencia
potencia    (descartes)                → primario resto_potencia
resto_potencia → DESCARTES unario | ε               (asocia a la derecha: %right)
```

No hay *else* colgante: `whether`, `alif` y `also` siempre llevan bloque con llaves, así que `alif` y `also` no están en SIGUIENTE(alternativa).

## 2. PRIMERO, SIGUIENTE y la tabla M

`ll1_tabla.c` implementa los algoritmos de punto fijo de las diapositivas 25, 26 y 27. Cada pasada recorre todas las producciones, y se repiten pasadas hasta que ningún conjunto cambie. Con PRIMERO y SIGUIENTE se llena `M[A, a]`. Si una celda recibe dos producciones, se cuenta un **conflicto**. La gramática de BAL tiene **0 conflictos**, así que es LL(1).

- 132 producciones, 62 no terminales y 64 terminales (los mismos `TokenType` del lexer; `$` es `TOKEN_EOF`).
- La tabla completa, con PRIMERO y SIGUIENTE de cada no terminal, está en [ll1_tabla.md](ll1_tabla.md). Se regenera con `make tabla-ll1`.

## 3. El analizador predictivo

`ll1_analizador.c` implementa el algoritmo no recursivo de la diapositiva 28: una pila (`$ programa` al inicio), la entrada y la tabla. Para verlo paso a paso:

```bash
make balc-ll1
./balc-ll1 --traza examples/factorial.bal     # columnas Pila | Entrada | Acción
./balc-ll1 examples/factorial.bal             # solo el resultado
./balc-ll1 --tabla                            # PRIMERO, SIGUIENTE y M en Markdown
```

Además de aceptar o rechazar, el analizador calcula los **tokens esperados**: los terminales que podían ir en la posición del error, o justo al final si solo se le dio el comienzo de un programa. Para cada terminal `t` se simula el algoritmo sobre una copia de la pila, consultando `M`, hasta ver si `t` se puede leer.

Hay un detalle importante. Antes de detectar el error, el analizador puede aplicar producciones ε (por ejemplo `resto_suma → ε`), y la pila pierde opciones. Por eso los esperados se calculan con la pila guardada **justo antes de leer el token**, no con la del momento del fallo. Si se usara la otra pila, la prueba contra Bison falla: después de un nombre (donde ya se aplicó `sufijos_referencia → ε`), el `.` de acceso a un campo no saldría en los esperados.

## 4. Uso en el IDE

**Correcciones** (`src/IDE/ide_arreglos.c`). Cada error tiene **una sola** corrección. En LL(1) cada celda `M[A, a]` tiene a lo sumo una producción, así que en el punto del error el parser no tiene que elegir nada: la tabla predice qué esperaba. `ll1_insercion_predicha` (en `ll1_analizador.c`) parte de la pila que había antes de leer el token del error y la recorre de forma obligada:

- un no terminal que puede ser ε se quita (se usa su producción ε, igual que hace la tabla con SIGUIENTE);
- un no terminal con una sola producción se expande con ella;
- el terminal que queda en el tope es **el** token que el parser predice: ese es la corrección, y se "lee" como si estuviera.

Se repite hasta que el token del error se pueda leer, con un máximo de 2 tokens. Por ejemplo:

| Código | La tabla predice | Corrección |
|---|---|---|
| `x * declare_int : 5` y en la línea de abajo otra sentencia | `;` | agregar `;` al final de la línea |
| `whether (x == 1 {` | `)` | agregar `)` antes de `{` |
| `whale (...) { ... }` y después `}` | `stop` y `;` | agregar `stop;` |
| `create_funk declare_int# ...` | `#` | agregar `#` |
| falta el `}` de la función | `}` | agregar `}` en una línea nueva, con la sangría de la `{` |

Si la tabla no obliga a un token único (por ejemplo, en `x : 5 gauss ;` después de `gauss` puede ir cualquier valor, o en `give 1 1;` sobra algo), se usa la recuperación "a nivel de frase" de la diapositiva 4. Se prueban correcciones locales: insertar o cambiar por un token esperado, o borrar el token del error o el anterior. Cada una se valida volviendo a analizar con el LL(1), y se elige **la** mejor: la que más avanza y, si empatan, la más sencilla. En esos dos ejemplos gana "borrar `gauss`" y "borrar el `1` de la columna 12".

Nunca se propone un nombre, un número ni `declare_true`/`declare_false`, porque habría que inventar el valor. Los errores de delimitadores (un cierre que sobra o uno equivocado), que no vienen del parser, conservan sus reglas propias.

**Todos los errores, no solo el primero** (`src/LL1/ll1_recuperacion.c`, `ll1_analizar_con_recuperacion`). Bison se detiene en el primer error de sintaxis. Para que el IDE muestre todos, el LL(1) analiza el archivo con recuperación: en cada error aplica su corrección a la lista de tokens y sigue analizando. Así cada error del archivo aparece con su propia corrección. Los errores de sintaxis que reporta Bison se **reemplazan** por los del LL(1): el primero es el mismo, y así dos errores en el mismo token se muestran los dos. Por ejemplo, si falta el `;` de la línea anterior y además la palabra está mal escrita (`wheter`), los dos se detectan en `wheter`. Cuando la corrección la predice la tabla, el mensaje dice qué faltaba ("se esperaba `;`").

- **Palabras clave mal escritas.** En cada error se buscan, hacia atrás en la misma sentencia, nombres parecidos a una palabra clave: a 1 o 2 letras de diferencia según la distancia de edición (Levenshtein). Si cambiar ese nombre por la palabra deja el archivo con **menos errores**, esa es la corrección, y el error se marca en el nombre. Por ejemplo, `whethr (n <= 1)` se lee como una llamada a `whethr`, y el parser se quejaría recién en la línea siguiente; la recuperación detecta que es `whether` mal escrito. Un nombre que de verdad es un nombre (`whales`) no se toca, porque cambiarlo no reduce los errores.
- **Modo pánico** (diapositiva 4). Si ningún arreglo local sirve (por ejemplo, falta `cycle` al inicio de la sentencia), se descarta la sentencia completa hasta el siguiente `;`, `{` o `}`, y se reporta **un** error. Si justo donde se reanuda vuelve a fallar, es la misma falla en cascada y no se reporta otra vez (como hacen Yacc y Bison).
- **Dos errores seguidos.** Si ninguna corrección sirve sola, se acepta una inserción que avanza al menos un token cuando el error siguiente también tiene un buen arreglo. Así `resultado  declare_int 1;` (faltan `*` y `:`) da dos errores: agregar `*` y luego agregar `:`. En BAL el `*` es el separador de las declaraciones (`nombre * tipo`), no la multiplicación (`pitagoras`), así que olvidarlo cuenta igual que olvidar `;` o `:`.
- **Sin errores duplicados de delimitadores.** Si con las correcciones el programa completo queda válido, los paréntesis y llaves también quedan balanceados. En ese caso el IDE no agrega los errores del revisor de delimitadores, que solo cuenta símbolos: si falta un `{`, ese revisor diría que "sobra" el último `}`, y borrarlo dañaría el código.

| Código | Errores que muestra el IDE |
|---|---|
| `resultado * declare_int : 1` sin `;` | agregar `;` al final de la línea |
| `i * declar_int : 2;` | `declar_int` parece `declare_int` mal escrita |
| `whethr (n <= 1)` | `whethr` parece `whether` mal escrita |
| …y sin `{` después | se esperaba `{`: agregar `{` al final de la línea |
| `resultado  declare_int 1;` | agregar `*` antes de `declare_int`, y agregar `:` antes de `1` |

**Sugerencias** (`src/IDE/ide_nucleo.c`, `ide_sugerencias_predictivas`). Al escribir una palabra, se analiza el código que hay antes del cursor (sin `$`) y la tabla dice qué tokens pueden seguir. Solo se sugieren esas palabras clave, y los nombres del archivo solo si ahí puede ir un nombre. Por ejemplo:

- tras el bloque de un `whether`, `al` sugiere `alif` y `also`, pero no `aka`;
- tras `x : 5 `, `e` sugiere `euclides` y `euler`, pero no `endgame`.

**Ver la tabla** (`src/IDE/ide_tabla_ll1.c`). El botón **Tabla LL(1)** del IDE abre una ventana con tres pestañas, en el orden en que se construye la tabla:

1. **Tabla M[A, a]**, con la misma notación de las diapositivas: una fila por no terminal (en cursiva) y una columna por terminal (en negrita, escrito como en BAL: `create_funk`, `#`, `(`, `;`; los que no tienen texto fijo se abrevian `id`, `num`, `cadena`, `car`, y `$` va en la última columna). Cada celda llena tiene el **lado derecho** de la producción (o `ε`); una vacía es error. Al hacer clic en una celda se ve la producción completa y su número (por ejemplo, `M[sentencia, id] = producción 36: sentencia → id resto_identificador`). La columna de los no terminales queda fija al desplazarse a la derecha.
2. **Producciones**: las 132 producciones numeradas.
3. **PRIMERO y SIGUIENTE** de cada no terminal.

Arriba se indica la cantidad de conflictos: con 0, la gramática es LL(1). La ventana no calcula nada: muestra lo mismo que `ll1_tabla()` le da al analizador.

## 5. Pruebas

```bash
make test-ll1           # tabla sin conflictos + equivalencia con Bison
make test-ide-arreglos  # la corrección predicha por la tabla deja el código sin errores
make test-ide-nucleo    # sugerencias con predicción LL(1)
```
