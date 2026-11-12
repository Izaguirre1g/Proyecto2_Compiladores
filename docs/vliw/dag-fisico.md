# DAG físico independiente — I1-A2b-2

## Objetivo y arquitectura

**IMPLEMENTADO:** constructor C11 de restricciones físicas de emisión para S7.
Consume flujo ISA ordenado, metadatos I1-A2a, efectos adicionales acreditados y
CFG I1-A2b-1 vigente. Devuelve `VliwDependencia` propios. No genera BAL, no
modifica palabras, no asigna registros y no produce planes o código ensamblador.

```text
operaciones físicas explícitas + efectos/ABI externos
  → I1-A2a/S2 + CFG vigente → DAG físico → proyección explícita completa
  → I1-A1 → S7 → verificador S7 → S8-A → CB/S2
```

Solo las primeras fases hasta DAG pertenecen al módulo nuevo; los consumidores
son implementaciones existentes. Las pruebas recorren toda la cadena con
fixtures completos, sin sustituir las dependencias generadas.

## Contrato de entrada y API

```c
VliwDagEstado vliw_dag_construir(const VliwDagEntrada *entrada,
    VliwDag **salida, VliwDagDiagnostico *diagnostico);
void vliw_dag_liberar(VliwDag **dag);
```

`VliwDagEntrada` toma prestados `flujo`, `cfg`, `metadatos` y
`efectos_adicionales`. `cantidad_metadatos` debe coincidir con operaciones y
aplica a ambos arreglos; incluso cuando no hay efectos adicionales se aporta
un arreglo de ceros. Los efectos efectivos son mínimos OR adicionales; los
mínimos se contrastan íntegramente con una extracción nueva, no pueden borrarse.

`generacion` coincide con flujo/CFG y las referencias CFG. `pendientes!=0`
rechaza obligaciones de esta entrada sin resolver. No son los bits parciales de
I1-A2a: estos se contrastan y conservan, nunca se borran para aparentar completitud.

Para un flujo no vacío se requiere `efectos_acreditados=true`. Ante llamadas o
retornos se requiere además `abi_acreditado=true`. Son declaraciones externas
con evidencia a cargo del cliente; el módulo no autentica autoría ni contenido
omitido. Se exige cobertura dinámica CFG acreditada, incluso si el constructor
CFG puede aceptar una estructura parcial con destinos incompletos.

El vacío acepta arreglos de cantidad cero, sin exigir crédito de efectos/ABI;
requiere igualmente CFG vacío válido, generaciones coherentes y sin pendientes.
Los punteros con cantidad cero no se recorren. Todo almacenamiento descrito por
cantidades debe ser accesible realmente; la API C no prueba punteros arbitrarios.
Diagnóstico y salida deben ocupar almacenamiento separado de las entradas.

## Identidad, generación y relación con CFG

ID externo, posición de operación, índice de bloque y PC son conceptos distintos.
Los extremos de aristas son posiciones originales; no son IDs ni PCs. Se admiten
IDs únicos no consecutivos, incluido SIZE_MAX, conforme a S7/CFG. No se obtiene
una dirección definitiva ni se cambia la identidad del flujo.

El módulo reconstruye un CFG temporal con `vliw_cfg_construir` sobre el flujo
recibido y contrasta cantidades, partición, mapa, sucesores como conjuntos,
destinos y cobertura dinámica con el CFG suministrado. Reutiliza las reglas
reales de fronteras, referencias, control y S2, sin duplicar otro constructor.
La reconstrucción y su resultado temporal se liberan antes de construir aristas.

**Límite contractual importante:** `VliwCfg` no conserva IDs ni palabras de las
operaciones originales. Dos flujos diferentes pueden producir la misma partición,
mapa y destinos. Compartir generación no autentica contenido: un cambio de
inmediato que conserva forma y máscaras no puede descubrirse retrospectivamente.
El cliente garantiza que no reutiliza análisis después de modificar operaciones,
IDs, referencias o efectos; debe renovar generación y reconstruir los análisis.
La prueba incluye esa limitación como un caso aceptable, sin fingir detección.

## Metadatos físicos

Se llama a I1-A2a por palabra/slot y se comparan campos significativos, no padding:
operación, familia, slot, máscaras, efectos mínimos, forma de salto, AUTH/INIT y
pendientes. Se rechazan máscaras con bits altos, clobbers ficticios y efectos
omitidos. No se copia S2 ni se implementa un decodificador.

Los efectos adicionales solo utilizan bits S7. Memoria corresponde a LSU/CRIPTO;
estado y bóveda, a CRIPTO. Excepción/desconocidos/barrera explícita pueden
acreditar restricciones conservadoras compatibles con S7. La llamada requiere
DESCONOCIDOS, sin añadir memoria a BRU para simular al callee.

## Riesgos y distancias

Para **todos** los pares i<j, incluso entre bloques y con redefiniciones
intermedias, se exige un camino ponderado suficiente conforme a I1-A1:

| Riesgo | Condición física | Distancia mínima |
|---|---|---|
| RAW | escribe(i) ∩ lee(j), sin x0 | 3 bundles, N+3 sin forwarding |
| WAR | lee(i) ∩ escribe(j), sin x0 | 1 |
| WAW | escribe(i) ∩ escribe(j), sin x0 | 1 |
| Memoria | ambos con lectura/escritura de memoria | 1 |
| Control | cualquiera con control explícito | 1 |
| Barrera | cualquiera CRIPTO/global/call/ret | 3 |

x0 nunca genera riesgos GPR; escribirlo sigue siendo una operación no-NOP.
Un registro ausente S2 no es x0. Feistel conserva ambos destinos y fuentes;
LSU conserva base y dato de store, o destino de load. La referencia no es solo
la última escritura: las escrituras anteriores también necesitan cobertura.

Memoria mantiene orden incluso para load/load, direcciones disjuntas y rutas
aparentemente excluyentes. No se introduce análisis de alias ni relajación CFG.
El DAG es de emisión: los backedges CFG siguen siendo válidos, pero no se
copian como dependencias hacia atrás ni crean ciclos en el DAG.

CRIPTO, bóveda, estado, excepción, desconocidos, barrera explícita y call/ret
son barreras reales. Las excepciones criptográficas no se reclasifican como BRU.
No se ejecutan AUTH/INIT, bóveda ni instrucciones durante la construcción.

Cuando coinciden causas, prevalece barrera3 y después RAW3; para restricciones
de peso1 se elige WAR, WAW, memoria o control, en ese orden. Cada tipo elegido
tiene una justificación real. Una arista fuerte puede cubrir otras causas más
débiles; no se fabrican RAW, memoria, control o barreras para forzar aceptación.

## ABI, llamadas y retornos

La clasificación procede de `VliwCfgOperacion.control` y referencias acreditadas.
Escribir o leer x1 no infiere llamada/retorno. No se agregan al SYER clobbers
completos del callee: sus máscaras deben coincidir con la operación local S2.
Una barrera impide movimientos, pero no preserva un valor destruido por llamada.
La preservación, ABI, efectos externos y reubicaciones siguen siendo externos.
Si el cliente declara obligaciones pendientes o no acredita ABI, se rechaza.

Distinguir: DAG construido, restricciones visibles completas y programa con
semántica/ABI acreditados. Solo los dos primeros aspectos, dentro del contrato
recibido, corresponden a este constructor. No se crea `VliwEntrada` ni se
establece `metadatos_completos=true`.

## Cobertura transitiva y algoritmo

Se recorren destinos j crecientes y, para cada destino, orígenes i decrecientes.
Una matriz temporal de uint16_t guarda el mayor peso de caminos ya construidos;
cero representa ausencia de camino. Se añade i→j solo cuando el riesgo real
requiere más separación que la ruta existente. Cada nueva arista de peso p
propaga dist(k,i)+p para todos los ancestros k<i.

Invariante: las columnas anteriores están completas. Al examinar i→j, las
rutas que pasan por un sucesor intermedio de i ya fueron consideradas. Ningún
origen menor todavía pendiente puede crear después un camino desde i. Por tanto,
omitir una arista cubierta es seguro; añadir una insuficiente permite propagar
la nueva cobertura a todos sus ancestros. La propagación mantiene el máximo
peso, también para extremos que no tienen riesgo directo. Todos los índices
crecen estrictamente: no puede introducirse un ciclo.

Ejemplos: tres loads sin riesgos GPR necesitan dos aristas MEMORIA1; la ruta
cubre al primer/último par. WAW1 seguido de RAW3 cubre con peso4 una RAW antigua.
CONTROL1+CONTROL1 suma2 y **no** cubre RAW3: se conserva la arista RAW directa.
No se promete reducción transitiva mínima única; se evita añadir una arista
que en ese momento ya tiene cobertura suficiente.

## Complejidad, recursos y capacidad

n≤4092 antes de multiplicar o recorrer. Se comprueba overflow de n²×sizeof(uint16_t).
La matriz ocupa como máximo **33 488 928 bytes** (31,94 MiB). Con m aristas,
el algoritmo de riesgos cuesta O(n²+n·m), peor caso O(n³), y memoria O(n²+m).
A ello se suma la reconstrucción CFG existente y comparación de conjuntos
O(sumatoria grados²); el CFG ya tiene validaciones cuadráticas de identidades.
No se promete tiempo lineal ni se confunde este costo con el validador I1-A1.

El vector empieza en32 aristas, duplica capacidad y tiene un límite explícito
**65 536 aristas**. La reserva máxima del vector es ese límite por sizeof(VliwDependencia),
no millones de aristas por cada par. Se comprueba overflow antes de reservar;
realloc fallido conserva la memoria anterior para poder liberarla. Se rechaza
por CAPACIDAD al alcanzar el límite; no se promete aceptar toda densidad posible.
Las pruebas de estrés miden casos concretos; no acreditan todo el espacio denso
ni necesitan alcanzar ese límite para demostrar la cobertura de sus fixtures.

Un camino de separación≥1023 se rechaza antes de truncar uint16_t. El campo
`camino_critico` es separación máxima del DAG, no cantidad final de bundles.
4092 es capacidad estructural, mientras que1023 bundles incluyen slots, guardas
y NOP de S7; el vector 0x3FF0 sigue reservado. Un DAG aceptado puede no caber
al planificar. Una cadena WAW de1023 operaciones tiene separación1022; una de
1024 se rechaza. Barreras consecutivas consumen separación3.

## Propiedad y diagnósticos

`VliwDag` posee su arreglo de dependencias; no retiene punteros prestados.
Su vida no depende del CFG/flujo después del éxito. `salida` debe apuntar a NULL;
una salida ocupada no se reemplaza. Solo OK publica un resultado completo.
Ante error se liberan CFG temporal, matriz y resultado/aristas parciales.
El liberador admite NULL o puntero ya vaciado; no libera entradas.

Diagnóstico opcional por valor: estado, índice de operación o VLIW_VACIO global,
y mensaje literal sin liberación. Los errores del CFG se conservan como mensaje
con índice global DAG; su tabla detallada no se presenta como índice de operación.

| Estado | Causa |
|---|---|
| ARGUMENTO | Puntero requerido, cantidad de metadatos o salida ocupada |
| CFG | Reconstrucción rechazada o resultado distinto del flujo vigente |
| METADATOS | Diferencia S2 o efectos adicionales incompatibles |
| INCOMPLETO | Generación, créditos, obligaciones o destinos dinámicos pendientes |
| CAPACIDAD | Dimensión, overflow, aristas o camino imposible |
| MEMORIA | Fallo de cualquier reserva propia o de reconstrucción CFG |

## Pruebas y reproducción

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B tests/vliw/test_vliw_dag_fisico.py -v
```

Python compila C11 con -Wall -Wextra -Werror -pedantic en temporales externos.
CC admite flags de instrumentos; I1DAG_EVIDENCIA puede apuntar a JSON externo.
El enlace mínimo solo necesita DAG+CFG+metadatos+S2. No se modifica Makefile.

Los oráculos manuales cubren vacío, independencia, RAW/WAR/WAW y concurrencia,
x0/x1/Feistel, memoria conservadora, control, backedges, excepciones/barreras,
call/ret acreditados e identidades no consecutivas. Un cierre Floyd independiente
contrasta cobertura y validez de cada arista en cien flujos deterministas.
Los negativos mutan campos locales, CFG, generaciones, efectos y argumentos.
Se comprueba conservación de entradas y tablas CFG en éxitos/rechazos.

Recursos falla cada reserva observada en dos fixtures: secuencia de80 escrituras
con crecimientos de vector y call/ret con destinos/sucesores propios del CFG.
Se verifica limpieza, inmutabilidad y repetición sana. Estrés separa construcción
máxima independiente,31 cadenas GPR, WAW al límite y barreras imposibles.
No se ejecuta I1-A1 sobre4092 operaciones como si su costo estuviera acreditado.

Las cinco integraciones completas son fixtures físicos expresos: ALU/store,
LSU transitivo, salto al inicio, Feistel/segundo destino y call/ret estáticos.
Solo sus datos completos justifican la declaración S7; no provienen de BAL.
I1-A1 y S7 reciben las aristas generadas sin reemplazarlas; verificador S7,
S8-A y CB/S2 revisan el resultado. No se afirma ejecución ABI real o equivalencia.

ASan, UBSan, ambos, LSan y Memcheck se aplican a C; no prueban ausencia universal
de UB. Logs, códigos, tiempos, regresión y auditoría de comentarios se conservan
fuera del repositorio en evidencias I1-A2b-2. No se usan cifras históricas como
resultados actuales; GTK solo se verifica si están disponibles sus dependencias.

## Productor y preparación de I1-A3

**IMPLEMENTADO:** consumidor independiente con cobertura física conservadora,
API propia, controles de entrada, publicación atómica y fixtures verificables.
**PROPUESTO:** proyección productiva desde una exportación estructurada acordada.
**PENDIENTE DE ACUERDO:** productor, símbolos/origen, asignación/spills,
preservación ABI, efectos externos, reubicaciones/relajación, observables y
protocolo de invalidación. **BLOQUEADO:** BAL real hacia S7/S8-A hasta acreditar
esas obligaciones. PR #59 reescribe BAL usando DCE y recompila por el backend
tradicional; no conecta la IR optimizada al scheduler.

I1-A3 necesita nueva autorización y coordinación con Dylan/José/Meibel según
sus componentes. Este módulo no modifica sus interfaces ni resuelve sus tareas.
Un CFG y DAG válidos no certifican compilador optimizado, retorno de main,
equivalencia BAL ni mejora de rendimiento.
