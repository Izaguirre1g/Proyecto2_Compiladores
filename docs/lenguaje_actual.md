# Sintaxis acordada e implementada en el parser y AST

Este documento registra las decisiones confirmadas para el proyecto y las
convenciones necesarias para las extensiones. Complementa el PDF del avance:
los ejemplos contradictorios del PDF no cambian estas reglas.

El ejecutable se llama `balc`; sus opciones están en el README. Para ver solo
tokens o el AST sin compilar: `balc --frontend [-v] [-t]`.
`tree`, `trace` y demás opciones antiguas del avance no se implementan.
No se admiten `declare_float`, literales decimales ni `show` por ahora.
`alif` siempre requiere una condición entre paréntesis.

## Alcance

Todo lo siguiente está implementado como **reconocimiento sintáctico y nodos
AST**. El compilador todavía no ejecuta los programas ni genera ensamblador.
Existe además un módulo parcial de generación de expresiones y asignaciones,
todavía sin integrar a la CLI; véase `docs/generacion_basica.md`.
No carga archivos importados, asigna memoria a colecciones, implementa captura
de excepciones en ejecución ni verifica tipos o símbolos. Estas tareas requieren
las fases semánticas y de generación de código del equipo.

## Variables y constantes

```text
n * declare_int;
n : 5;
a * declare_int : 1, b * declare_int;
declare_const LIMITE * declare_int : 5;
```

El valor inicial es opcional para variables. Su ausencia no inserta un cero en
el AST ni garantiza un valor de ejecución. Cada declaración termina con `;`.
Las constantes usan el prefijo `declare_const` y requieren un inicializador.
Esta es la convención adoptada para concretar el token del avance. Se admiten
constantes globales y locales; las variables y listas se declaran en bloques.
Prohibir reasignaciones a constantes corresponde al análisis semántico.

### Plegado de constantes

Después del análisis semántico (y del enlace de `bring`), `sem_visitar_proyecto`
en `src/Analisis_Semantico/semantic_modules.c` resuelve las constantes antes
de entregar el AST al backend, porque cada backend las soportaba a medias:

| Constante | Antes | Ahora (normal, `--ir`, `-O1`, `-O2`) |
|---|---|---|
| `declare_int` global, literal o expresión (`A pitagoras 3`) | solo normal y `-O2` | todos |
| `declare_boolean` usada en condiciones (`whether (DEBUG)`) | solo `-O2` | todos* |
| Arreglo constante global (`TABLA[i][j]`, índice variable o constante) | solo `-O2` | todos |
| Constante local (`declare_const` dentro de una función) | `--ir` y `-O2` | todos |
| Constante como tamaño de arreglo (`v * declare_int[N]`) | ninguno | todos |
| Constantes importadas (`d.DOBLE`, `d.MATRIZ[1][0]`) | ninguno | todos |

\* `-O1` no puede reescribir el texto cuando el DCE quita el salto de un
`whether` cuya condición es constante y la variable se usa después; pasa igual
con `whether (declare_true)` escrito a mano. Las variables `declare_boolean`
siguen sin soporte en el backend normal (ocupan 1 byte en la tabla y el
backend lee palabras de 4).

Reglas:

1. Una constante `declare_int` o `declare_boolean` cuyo valor se calcula en
   compilación (literales, otras constantes, operadores, elementos de un
   arreglo constante con índice constante) se reemplaza por su valor en cada
   uso. Los parámetros y variables locales con el mismo nombre la tapan y no
   se reemplazan.
2. La aritmética es la de la sección de datos del binario (`fb_preparar_datos`):
   32 bits con desborde circular, `euclides`/`euler` con signo truncando hacia
   cero, `descartes` con exponente no negativo. Una división por cero no se
   pliega (el error se informa como antes).
3. Las constantes `declare_int` globales siguen declaradas, con su valor ya
   calculado: el binario las sigue guardando en su sección de datos. La IR ignora una
   constante global que ninguna función usa (`src/Representacion_Intermedia/ir.c`);
   si alguna la usa todavía (no se pudo plegar), la rechaza como antes.
4. Un arreglo constante global se declara como arreglo normal al inicio de
   cada función que lo usa (la semántica ya impide modificarlo). Su literal se
   lee en el ámbito global: un parámetro con el nombre de una constante del
   literal no la tapa.
5. Una constante local que no se puede calcular (depende de variables o
   llamadas), o un arreglo constante local, pasa a ser una declaración normal.
6. El AST plegado vuelve a pasar por la semántica; si fallara, se usa el AST
   sin plegar, igual que antes.

`semantic.c` también acepta indexar un arreglo constante importado
(`d.MATRIZ[1][0]`), con las mismas reglas de índices y límites que un arreglo
local. Pruebas: `make test-constantes` (`tests/fixtures/constantes.bal`, `constantes_ambito.bal`,
`tests/modulos/constantes_principal.bal`), con resultados revisados en el
simulador.

## Operadores

Precedencia adoptada, de menor a mayor:

| Grupo | Asociatividad |
|---|---|
| `\|\|` (OR) | Izquierda |
| `^` (XOR lógico) | Izquierda |
| `&&` (AND) | Izquierda |
| `==`, `=/=`, `<`, `>`, `<=`, `>=` | Sin encadenar |
| `gauss`, `neumann` binario | Izquierda |
| `pitagoras`, `euclides`, `euler` (residuo) | Izquierda |
| `neumann` unario, `~` (NOT) | Prefijos |
| `descartes` (potencia) | Derecha |

Paréntesis, llamadas, acceso a miembros e índices permiten agrupar operandos.
`2 descartes 3 descartes 2` significa `2 descartes (3 descartes 2)`.
`neumann 2 descartes 2` se agrupa como `neumann (2 descartes 2)`.
Para negar una comparación completa, escribir `~(n == 0)`.
Las prioridades se eligieron explícitamente porque el PDF no las formaliza.
Todavía no hay evaluación ni implementación de cortocircuito; los nodos
preservan los operadores para la siguiente fase.

## Cycle

```text
cycle i let 0 until 5 step 2 {
    ultimo : i;
} endgame;
```

No se ponen paréntesis alrededor de la cabecera. El cierre es `endgame;`.
Inicio, límite y paso admiten expresiones. Si `step` se omite, el AST incluye
un entero `1` como paso. El límite final es **exclusivo**: el ejemplo anterior
describe los valores 0, 2 y 4. Para un recorrido descendente puede escribirse
`step neumann 1`; la condición prevista es mayor que el límite final.
La validación de paso cero, tipos y ámbito del índice queda para semántica.

Para visitar todos los elementos de una colección se escribe:

```text
cycle i let 0 until size(numeros) {
    numeros[i] : numeros[i] gauss 1;
} endgame;
```

No se resta 1 al límite exclusivo. `whale (...) { ... } stop;` sigue disponible.

## Arreglos, matrices y parámetros

```text
a * declare_int[5];
m * declare_int[2][3] : [[1, 2, 3], [4, 5, 6]];
dinamica * declare_int[filas][columnas gauss 1];
m[1][2] : a[0] gauss 1;
```

Las dimensiones de variables requieren expresiones; no se permite `a * declare_int[];`.
En parámetros sí se admiten dimensiones sin tamaño:

```text
create_funk #declare_int# leer(a * declare_int[], m * declare_int[filas][cols]) {
    give a[0] gauss m[0][1];
}
```

También se admiten parámetros como `m * declare_int[][]`. Las dimensiones y
los índices pueden repetirse: el parser no limita el número de dimensiones a dos.
Los inicializadores usan corchetes anidados y comas; cada elemento puede ser una
expresión. Varias declaraciones se separan por comas antes del `;` final.
Comprobar tamaños positivos, forma de inicializadores, límites de índices y tipos
corresponde al análisis semántico y, cuando proceda, a la ejecución.

## Listas

```text
declare_list numeros * declare_int : [10, 20], vacia * declare_int : [];
add(numeros, 30);
remove(numeros, 0);
cantidad * declare_int : size(numeros);
```

El inicializador de una lista es opcional; su ausencia se conserva en el AST.
`add` y `remove` requieren exactamente dos argumentos; `size`, exactamente uno.
`remove` recibe la colección y el índice, siguiendo el ejemplo del avance.
El parser permite usar estas operaciones como sentencias o expresiones; la fase
semántica deberá restringir el uso del resultado de operaciones sin retorno.

Para pasar una lista tipada se adopta `nombre * declare_list tipo`, por ejemplo:

```text
create_funk #declare_int# contar(xs * declare_list declare_int) {
    give size(xs);
}
```

Esta extensión diferencia listas de tamaño variable y parámetros de arreglo.

## Importaciones y acceso a módulos

```text
bring matematicas aka math;
bring utilidades;

create_funk #declare_int# sumar() {
    give math.suma(2, 3);
}
```

Las importaciones se escriben al nivel del programa, con alias opcional; los
nombres son identificadores, sin comillas ni extensión `.bal`. Las funciones,
importaciones y constantes globales se conservan en orden en `Programa`.
No se permite `bring` dentro de una función. Los módulos sin `main` pueden
analizarse: la verificación del punto de entrada corresponde a la integración.

`math.suma` genera un acceso a miembro; la llamada contiene ese acceso como
destino. También se representan accesos indexados como `datos.matriz[i][j]`.
El parser solo registra la importación; el archivo se abre y se enlaza en la
semántica (ver abajo).

### Enlace entre archivos

`src/Analisis_Semantico/semantic_modules.c` carga y valida cada módulo
(existencia, ciclos de importación, funciones, argumentos, tipos, privacidad).
Después, en `sem_visitar_proyecto`, **une el programa y sus módulos en un solo
AST** antes de entregarlo al backend, así que el ensamblador normal, `--ir`,
`--dce`, `--unroll`, `--reorder`, `-O2` y el IDE generan código para programas
con `bring` sin cambios propios:

1. Cada módulo importado, directa o indirectamente, se copia una sola vez,
   después de los módulos que él importa, aunque se importe con varios alias.
2. Sus funciones y constantes llevan el nombre del archivo como prefijo:
   `sumar` de `matematicas.bal` pasa a ser `matematicas__sumar`. Así no chocan
   con nombres del programa ni de otros módulos (un `main` de un módulo queda
   como `modulo__main`).
3. Las referencias se reescriben: `math.sumar(2, 3)` → `matematicas__sumar(2, 3)`,
   y dentro del módulo `sumar(...)` → `matematicas__sumar(...)`.
4. Un parámetro o variable local con el mismo nombre que una constante del
   módulo la tapa y no se renombra (se respeta el orden: en
   `r * declare_int : base;` antes de declarar un `base` local, se usa la constante).
5. Las constantes `declare_int` o `declare_boolean` con valor literal se
   reemplazan por ese valor (`config.limite` → `10`), porque la IR todavía no
   admite declaraciones globales.
6. El AST unido vuelve a pasar por la semántica para construir su tabla de
   símbolos y se entrega al backend. Un programa sin `bring` no cambia.
7. El código copiado de un módulo queda en la **línea 0** ("otro archivo").
   El DCE que reescribe el texto (`-O1` y "Aplicar al código" del IDE) edita
   por número de línea: así nunca confunde la línea 5 de un módulo con la
   línea 5 del principal. El código muerto de un módulo se compila tal cual.

```text
bring operaciones aka op;            %% operaciones.bal hace bring matematicas aka math
... op.duplicar(5) ...
```

queda, para el backend, como

```text
create_funk #declare_int# matematicas__sumar(a * declare_int, b * declare_int) { ... }
create_funk #declare_int# operaciones__duplicar(n * declare_int) {
    give matematicas__sumar(n, n);
}
create_funk #declare_infinite_void# main() { ... operaciones__duplicar(5) ... }
```

`-O1` copia el programa a un directorio privado para reescribirlo; con
`bring`, copia ahí también los módulos importados (y los que ellos importan)
desde la carpeta del fuente, y borra todo al terminar
(`tools/vliw/optimizacion_cli.c`).

Pruebas: `make test-enlace` compila los programas válidos de `tests/modulos/`
con el backend normal, `--ir`, `-O1` y `-O2`, y ejecuta en el simulador VLIW
(`tests/test_enlace_simulador.c`) `enlace_principal.bal` (normal y `-O1`) y
`enlace_dce.bal` (con `-O1` se borra una variable muerta del principal y el
resultado no cambia), revisando sus resultados. Además, los binarios de un
programa con `bring` son idénticos byte a byte a los del mismo programa escrito
en un solo archivo con los nombres ya unidos.

Limitaciones: un identificador escrito a mano como `modulo__nombre` choca con
el nombre enlazado (la semántica lo informa como duplicado); un módulo no puede
llamarse `entrada` con `-O1` (es el nombre de la copia de trabajo); los errores
del backend dentro de código de un módulo se informan en la línea 0.

## Seek y seize

Se adopta la forma de captura tipada del avance:

```text
seek {
    resultado : math.suma(a, b);
} seize (ErrorAritmetico e) {
    valido : declare_false;
}
```

`seek` requiere un bloque y una o más capturas `seize (TipoError nombre) { ... }`.
Cada captura conserva el nombre del tipo, la variable y el bloque. El tipo del
error es un identificador, no un tipo de variable como `declare_int`.
Se admiten capturas sucesivas y bloques anidados. No llevan `;` al final.
La existencia de los tipos de error, la selección de una captura, el ámbito de
su variable y la traducción a la arquitectura quedan pendientes de integración.

## Ejemplos y pruebas

```bash
make
./balc --frontend -t examples/lenguaje_ampliado.bal
./balc --frontend -t examples/matematicas.bal
make test-parser
make test-ast
make test
```

`lenguaje_ampliado.bal` combina las extensiones y `matematicas.bal` contiene la
función importada de ejemplo. Cada archivo se analiza por separado en esta fase.
EOF y errores siguen siendo estados de la API: `@`, `!!!`, `!?` y `%?` no se
adoptan como símbolos del código fuente. Los identificadores siguen las reglas
ASCII del lexer de Javier; los acentos se admiten en textos y caracteres.
