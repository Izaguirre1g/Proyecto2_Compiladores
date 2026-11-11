# Eliminación de código muerto (DCE)

Código: [`src/DCE/dce.c`](../src/DCE/dce.c) · Interfaz: [`include/dce.h`](../include/dce.h) ·
Pruebas: [`tests/test_dce.c`](../tests/test_dce.c) y [`examples/dce/`](../examples/dce/)

```
./balc --dce archivo.bal     # IR antes, variables vivas, lo eliminado e IR después
make test-dce                # pruebas
```

Para verlo funcionar con un ejemplo:

```
./balc --dce examples/dce/dce_ciclo.bal
```

La salida tiene cuatro partes: la IR original, las variables vivas de cada
bloque (`use`, `def`, `IN`, `OUT`), lo que se eliminó (`extra` y `base`, en
3 pasadas) y la IR después del DCE. La llamada a `acumular` se conserva.

### En el IDE

`./balc-ide examples/dce/dce_llamadas.bal` y el botón **Código muerto**
([`src/IDE/ide_dce.c`](../src/IDE/ide_dce.c)):

- Corre lo mismo que `balc --dce` sobre el texto del editor, aunque no esté guardado.
- **Tacha** las líneas eliminadas completas (`b * declare_int : a pitagoras 2;`).
- Pone **fondo gris** a las que solo perdieron una parte. Por ejemplo, en
  `c * declare_int : registrar(v, a);` se elimina la copia a `c`, pero
  la llamada se queda.
- La pestaña **Código muerto** lista cada línea con lo eliminado; un clic lleva a la línea.
- **Ver IR y variables vivas** abre la IR antes, la tabla `use`/`def`/`IN`/`OUT`
  de cada bloque y la IR después ([`ide_dce_ventana.c`](../src/IDE/ide_dce_ventana.c)).
- Si se edita el código, las marcas se borran, porque las líneas ya no coinciden.
- Si el archivo tiene errores, o la IR todavía no soporta algo (por ejemplo,
  `cycle` con `step` negativo), lo dice en la pestaña en vez de marcar.

Para mostrar en la defensa que el DCE decide por las variables vivas:

1. Pulsa **Código muerto**: la línea 12 (`b`, dentro de `procesar`) sale tachada.
2. Agrega `registrar(v, b);` antes de `give a;` y pulsa **Código muerto** otra vez.
3. Ahora la línea 12 ya no se tacha, porque `b` está viva.

La prueba sin ventanas es [`tests/test_ide_dce.c`](../tests/test_ide_dce.c) (`make test-ide-dce`).

### El ensamblador y el binario sin el código muerto

El DCE trabaja sobre la IR, pero el backend genera el ensamblador desde el
**código fuente** (su AST). Para que el ensamblador y el binario salgan sin el
código muerto, el IDE arma el **código BAL nuevo**
([`src/IDE/ide_dce_codigo.c`](../src/IDE/ide_dce_codigo.c)) y lo compila con el
compilador normal.

Cada instrucción de la IR guarda la línea BAL de donde salió. Por eso, para
una declaración o asignación que ocupa sus propias líneas, se sabe qué parte
de ella eliminó el DCE:

| Lo que eliminó el DCE | Código nuevo |
|---|---|
| Todas sus instrucciones (`b * declare_int : a pitagoras 2;`) | se borra la línea |
| Solo la copia del resultado de una llamada (`aux * declare_int : cuadrado(n);`) | queda la llamada: `cuadrado(n);` |
| Una declaración cuyo valor se reemplaza antes de leerse (`r * declare_int : 0;` y luego `r : cuadrado(4);`) | se unen: `r * declare_int : cuadrado(4);`, porque el backend necesita declaraciones con valor |
| La copia, pero queda una división que puede fallar | se deja igual: BAL no tiene sentencias que solo evalúen una expresión |

El código nuevo se revisa con el análisis semántico antes de ofrecerlo. En la
pestaña **Código muerto**:

- el resumen dice cuánto cambia el ensamblador, por ejemplo "pasa de 528 a 435 bundles";
- **Aplicar al código** cambia el código del editor por el nuevo. Desde ahí, el
  botón **Ensamblador** (y su pestaña **Binario**) muestra el código nuevo, y si
  esa ventana estaba abierta se actualiza sola. Al guardar, `balc` genera el `.bin` nuevo;
- **Ver IR y variables vivas** tiene una cuarta pestaña, **Código nuevo**.

Con [`examples/dce/dce_ensamblador.bal`](../examples/dce/dce_ensamblador.bal), que
solo usa parámetros `int` (el backend todavía no acepta arreglos como
parámetros), el ensamblador pasa de **528 a 435 bundles** y el binario de 8496
a 7008 bytes.

**Comprobación con el simulador VLIW.** `make test-ide-dce` compila el original y
el código nuevo con `balc`, ejecuta los dos binarios en el simulador
([`tests/test_dce_simulador.c`](../tests/test_dce_simulador.c)) y exige que las
`main` guarde lo mismo en su arreglo `resultados`: `r = 16` y `s = 15` en los dos. Es la regla
de oro (diapositiva 12): el programa optimizado hace lo mismo, con menos código.

## Sobre qué trabaja

El DCE trabaja sobre la **IR de tres direcciones** ([ir.md](ir.md)): recibe el
programa ya dividido en bloques básicos con su grafo de flujo, quita las
instrucciones muertas y vuelve a calcular los bloques con `ir_bloques_basicos()`.
Se basa en la presentación *Optimización de Código* (diapositivas 12, 22, 25, 27 y 28).

## 1. Variables vivas (diapositiva 25)

Una variable está **viva** en un punto si su valor se puede **leer** más
adelante en **algún** camino. Es un análisis:

- **hacia atrás**: lo que pasa después decide qué está vivo antes;
- **may** (diapositiva 22): basta con un camino, así que los sucesores se combinan con **unión**.

Para cada bloque `B`:

| Conjunto | Significado |
|---|---|
| `use_B` | variables que `B` lee **antes** de asignarlas (su valor viene de antes del bloque) |
| `def_B` | variables que `B` asigna |
| `IN[B]` | vivas al entrar a `B` |
| `OUT[B]` | vivas al salir de `B` |

```
IN[B]  = use_B ∪ (OUT[B] − def_B)
OUT[B] = ∪ IN[S]    para cada sucesor S de B
OUT[SALIDA] = ∅                          (final de una función común)
OUT[SALIDA de main] = variables de main que se leen en main  (ver abajo)
```

**¿Por qué `main` es distinto?** La regla de los libros, `OUT[SALIDA] = ∅`,
supone que al terminar nadie mira las variables. Para las funciones comunes es
cierto: sus variables locales desaparecen al hacer `give`, y lo que devuelven
ya cuenta como uso del `return`.

Pero en este proyecto **el resultado de un programa es la memoria final de las
variables de `main`**. BAL no tiene `print`: el simulador y las pruebas del
equipo leen ahí el valor final. Por ejemplo,
[`tests/test_resolucion_saltos.c`](../tests/test_resolucion_saltos.c) ejecuta
`factorial.bal` y revisa que `resultado` valga 120 en memoria.

¿Cuáles variables de `main` son el resultado? **Las que el programa usa**: las
que se leen en algún lugar de `main`. Esas siguen vivas al terminar `main`. Una
variable que solo se escribe y nunca se lee no aporta nada: es código muerto
(el tipo 1 de código muerto: "la variable no será leída en ningún camino futuro").

| En `factorial.bal` con una línea agregada `t * declare_int : 4;` | ¿Código muerto? | Por qué |
|---|---|---|
| `t * declare_int : 4;` | **sí** | `t` nunca se usa después de declararse |
| `resultado : 1;` (rama `n <= 1`) | no | `resultado` se usa (`resultado pitagoras i`): es el resultado del programa |

Más consecuencias:

- Un valor que se **reemplaza antes de leerse** muere, aunque la variable sí se
  use después: en `x : 1; x : 2; v[0] : x;` se elimina `x : 1`.
- Si la única lectura de una variable está en código muerto, tampoco cuenta
  (diapositiva 28: eliminar código muerto puede generar más). En
  `a : 1; b : 2; suma : a gauss b;` con `suma` sin leer, mueren `suma` y después
  `a` y `b`. Por eso los ejemplos guardan su resultado en un arreglo
  (`resultados[0] : r;`): escribir en memoria es un efecto, así que `r` sí se usa.
- Los temporales (`$t1`, `$t2`…) no son variables del programa: nunca cuentan como resultado.

Se resuelve con el **algoritmo iterativo** (diapositiva 27): todos los
conjuntos empiezan vacíos y se recalculan hasta que una vuelta completa no
cambie nada (**punto fijo**). Como el análisis va hacia atrás, los bloques se
recorren del último al primero, y así se llega al punto fijo en menos vueltas.

`use_B` cuenta solo lo que se lee **antes** de asignarse. En `x = x gauss 1`,
`x` está en `use` (se lee el valor de antes) y en `def`. En cambio, en
`a = b + c; d = a - b`, `a` **no** está en `use`: se lee el valor que el mismo
bloque acaba de calcular. Con eso, la ecuación da exactamente el resultado
del **ejercicio 4** (diapositiva 39): con `OUT[B] = {x}`, `IN[B] = {b, c}`.
La prueba `prueba_ejercicio_4` lo comprueba.

## 2. Eliminación (diapositiva 28)

Una asignación `x = ...` es **código muerto** si `x` no está viva justo
después **y** la instrucción no tiene efectos secundarios. Procedimiento:

1. Calcular las variables vivas de todo el grafo de flujo.
2. Recorrer cada bloque **hacia atrás**, empezando con `OUT[B]`:
   - si la instrucción asigna `x`, `x` no está viva y se puede eliminar, se
     elimina, y sus operandos **no** cuentan como usados;
   - si no, `vivas = usos ∪ (vivas − {x})`, como en el ejercicio 4.
3. **Repetir**: eliminar código muerto puede dejar más código muerto.

Así, una cadena `a = ...; b = a; c = b` con `c` muerta cae entera en una
sola pasada dentro de un bloque. Cuando la cadena cruza bloques, hace falta
otra pasada. Ejemplo en [`examples/dce/dce_ciclo.bal`](../examples/dce/dce_ciclo.bal):

```
base * declare_int : 10;      <- 2.ª pasada: ya nadie lee base
...
whale (i < n) {
    extra : base gauss i;     <- 1.ª pasada: nadie lee extra
    acumular(conteo, i);      <- se queda: escribe en conteo
    i : i gauss 1;            <- se queda: la lee la condición
} stop;
```

`balc --dce` dice: `Codigo muerto eliminado: 4 de 48 instrucciones (quedan 44), en 3 pasadas`.
Son dos pasadas que eliminan y una tercera que confirma que ya no queda nada.

## 2b. Tipo 2: código inalcanzable

Lo de arriba es el **tipo 1**: una instrucción que **sí se ejecuta**, pero cuyo
resultado nadie lee. El **tipo 2** son instrucciones que **nunca se ejecutan**,
en ningún camino del grafo de flujo (diapositiva 15):

| Caso | Ejemplo en BAL | Qué se elimina |
|---|---|---|
| Después de un salto incondicional | `give x gauss x;` seguido de `y * declare_int : 5;` | lo que va después del `give` |
| Condición siempre falsa | `debug * declare_boolean : declare_false;` y `whether (debug) { ... }` | el cuerpo del `whether` |
| Condición que se calcula al compilar | `whale (1 > 2) { ... } stop;` | el cuerpo del `whale` |
| Condición siempre verdadera | `whether (declare_true) { ... } also { ... }` | la rama `also` |

Cómo se hace ([`src/DCE/dce.c`](../src/DCE/dce.c), sección 5):

1. **Propagación y plegado de constantes** (diapositiva 20). Una variable con
   **una sola asignación** de un valor constante vale eso en todo el programa
   (`debug = declare_false`), y una operación con operandos constantes se
   calcula al compilar (`1 > 2` es falso). Una variable con dos o más
   asignaciones (como `i : i gauss 1` en un ciclo) no es constante.
2. Si la condición de un salto condicional es constante, el salto se vuelve
   **fijo** (si siempre salta) o se **elimina** (si nunca salta).
3. **Bloques inalcanzables:** se recorre el grafo de flujo desde el inicio de
   cada función siguiendo los sucesores. Los bloques que no se visitan nunca se
   ejecutan, y se eliminan (salvo el inicio y el fin de la función).

Los dos tipos se repiten en las mismas pasadas: al quitar una rama, una
variable puede quedar sin leer (tipo 1). En el ejemplo, `debug` ya no la lee
nadie y se elimina también.

**En `factorial.bal`**: `n` se asigna una sola vez con `5`, así que `n <= 1` es
siempre falso y la rama `resultado : 1;` **nunca se ejecuta** (tipo 2). No es
tipo 1: `resultado` sí se usa. Si `n` fuera un parámetro, la rama se quedaría.

En el IDE, las líneas inalcanzables dicen **"inalcanzable: nunca se ejecuta"**,
y en el código nuevo se borra la sentencia completa (todo el `whether` o
`whale`) cuando todo lo de adentro es inalcanzable. El backend rechaza
sentencias después del `give` final; en ese caso el IDE avisa que el código
original no compila pero el nuevo sí, y con cuántos bundles.

## 3. Qué NO se elimina nunca (regla de oro, diapositiva 12)

Una transformación solo es válida si el programa hace **lo mismo**: mismos
valores y mismos **errores**. Por eso solo se eliminan instrucciones que
**calculan un valor y nada más**: copia, operación binaria o unaria, `ref`,
`load` y `size`. Siempre se conservan:

| Instrucción | Por qué |
|---|---|
| `call` (y sus `arg`) | La función puede **escribir** en un arreglo o lista que recibe, o llamar a otra que lo haga. Se conserva aunque nadie use lo que devuelve. Lo que sí se elimina es la copia `p = $t12` del resultado. |
| `store`, `add`, `remove` | Escriben en memoria. |
| `euclides` / `euler` con divisor variable | Puede ser **división entre cero**: quitarla haría que el programa deje de fallar. Con un divisor que es un número distinto de 0 (`100 euclides 4`) sí se elimina. |
| `descartes` con exponente variable | El backend no acepta exponentes negativos. Con un número `>= 0` sí se elimina. |
| `array`, `dim`, `list`, `param_decl` | Declaraciones: reservan memoria o reciben los parámetros. |
| saltos, etiquetas, `return`, `func`, `endfunc` | Forman el grafo de flujo. |

Las lecturas (`load`) sí se eliminan: el backend no revisa los límites de
los arreglos, así que leer no puede fallar.

## 4. Las tres pruebas con llamadas con efectos

| Programa | Se elimina | Se conserva |
|---|---|---|
| [`dce_llamadas.bal`](../examples/dce/dce_llamadas.bal) | en `procesar`: `b = a pitagoras 2`, `d = a gauss 1` y la copia a `c`; en `main`: `total : 0`, que se reemplaza antes de leerse | las dos llamadas a `registrar`, que escribe en el arreglo, y el valor final de `total` |
| [`dce_ciclo.bal`](../examples/dce/dce_ciclo.bal) | en `contar`: `extra` (1.ª pasada) y `base` (2.ª pasada) | la llamada a `acumular` dentro del ciclo, `i` y `vueltas` (resultado de `main`) |
| [`dce_division.bal`](../examples/dce/dce_division.bal) | `doble`, `seguro` (`100 euclides 4`) y las copias a `riesgo` y `p` | `100 euclides divisor`, que puede dividir entre 0, la llamada a `promedio` y `r` (resultado de `main`) |

El código muerto está dentro de funciones porque ahí las variables locales sí
mueren al hacer `give`. En `main` solo muere lo que se reemplaza sin leerse.

Además, cada prueba cuenta, antes y después del DCE, las llamadas, los
argumentos, las escrituras en memoria, los retornos y los saltos, y exige que
sean iguales. Así se comprueba que no se tocó nada con efectos.

La prueba se verificó a sí misma: se introdujeron a propósito siete errores en
el DCE, y la prueba detectó cada uno:

- eliminar llamadas;
- ignorar que las divisiones pueden fallar;
- hacer una sola pasada;
- calcular mal `use_B`;
- usar intersección en vez de unión;
- olvidar que `store` usa sus operandos;
- olvidar que las variables de `main` son el resultado.

## 5. Límite conocido

El DCE con variables vivas es **conservador**: una variable que solo se lee
para calcularse a sí misma cuenta como viva. Por ejemplo, en una función:

```
cuenta * declare_int : 0;
whale (i < n) {
    cuenta : cuenta gauss 1;   <- lee cuenta: está viva
    i : i gauss 1;
} stop;
give i;                        <- cuenta nunca sale de la función
```

`cuenta` nunca se devuelve, así que todo su cálculo es inútil. Pero
`cuenta gauss 1` la **lee**, y el análisis la considera viva. Detectarlo
requiere un análisis más fuerte, que la presentación no cubre.
