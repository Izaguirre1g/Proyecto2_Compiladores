# Eliminación de código muerto (DCE)

Código: [`src/DCE/dce.c`](../src/DCE/dce.c) · Interfaz: [`include/dce.h`](../include/dce.h) ·
Pruebas: [`tests/test_dce.c`](../tests/test_dce.c) y [`tests/dce/`](../tests/dce/)

```
./balc --dce archivo.bal     # IR antes, variables vivas, lo eliminado e IR después
make test-dce                # pruebas
```

Para verlo funcionar con un ejemplo:

```
./balc --dce tests/dce/dce_ciclo.bal
```

La salida tiene cuatro partes: la IR original, las variables vivas de cada
bloque (`use`, `def`, `IN`, `OUT`), lo que se eliminó (`extra` y `base`, en
3 pasadas) y la IR después del DCE. La llamada a `acumular` se conserva.

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
OUT[SALIDA] = ∅     (un bloque sin sucesores: el final de una función)
```

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
otra pasada. Ejemplo en [`tests/dce/dce_ciclo.bal`](../tests/dce/dce_ciclo.bal):

```
base * declare_int : 10;      <- 2.ª pasada: ya nadie lee base
...
whale (i < 4) {
    extra : base gauss i;     <- 1.ª pasada: nadie lee extra
    acumular(conteo, i);      <- se queda: escribe en conteo
    i : i gauss 1;            <- se queda: la lee la condición
} stop;
```

`balc --dce` dice: `Codigo muerto eliminado: 4 de 39 instrucciones (quedan 35), en 3 pasadas`.
Son dos pasadas que eliminan y una tercera que confirma que ya no queda nada.

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
| [`dce_llamadas.bal`](../tests/dce/dce_llamadas.bal) | `b = a pitagoras 2`, `d = a gauss 1` y la copia a `c` | las dos llamadas a `registrar`, que escribe en `datos` |
| [`dce_ciclo.bal`](../tests/dce/dce_ciclo.bal) | `extra` (1.ª pasada) y `base` (2.ª pasada) | la llamada a `acumular` dentro del ciclo, e `i` |
| [`dce_division.bal`](../tests/dce/dce_division.bal) | `doble`, `seguro` (`100 euclides 4`) y las copias a `riesgo` y `p` | `100 euclides divisor`, que puede dividir entre 0, y la llamada a `promedio` |

Además, cada prueba cuenta, antes y después del DCE, las llamadas, los
argumentos, las escrituras en memoria, los retornos y los saltos, y exige que
sean iguales. Así se comprueba que no se tocó nada con efectos.

La prueba se verificó a sí misma: se introdujeron a propósito seis errores en
el DCE (eliminar llamadas, ignorar las divisiones, una sola pasada, `use_B`
mal calculado, intersección en vez de unión y `store` sin usos), y la prueba
detectó cada uno.

## 5. Límite conocido

El DCE con variables vivas es **conservador**. En
[`examples/factorial.bal`](../examples/factorial.bal) el resultado nunca se
lee al final, pero dentro del ciclo `resultado : resultado pitagoras i` **lee**
`resultado`, así que el análisis la considera viva y no la elimina. Para
detectar que todo ese cálculo es inútil hace falta un análisis más fuerte, que
la presentación no cubre. Lo que sí se elimina es `resultado : 1` de la rama
`whether`, que va directo al final sin que nadie lo lea.
