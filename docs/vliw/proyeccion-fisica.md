# Proyección física independiente

## Alcance

IMPLEMENTADO: `vliw_proyeccion_construir` adapta un flujo físico explícito a
`VliwEntrada`/`VliwDestinos` propios. Javier mantiene este consumidor. No hay
lowering, asignación de registros, spilling, enlace ni entrada BAL en esta API.
Una proyección coherente no certifica equivalencia del programa fuente.

## Entrada real

`VliwProyeccionEntrada` contiene `VliwDagEntrada fisica`, el `VliwDag` recibido,
un arreglo de textos individuales con cantidad y `VliwGarantiasFisicas`.
La entrada DAG ya enlaza flujo, CFG, metadatos I1-A2a, efectos adicionales,
generación y créditos de efectos/ABI. Se reutilizan esos tipos públicos.

Cada posición tiene una operación no-NOP de la ISA, slot, ID único, texto y
metadatos físicos. Los IDs externos pueden ser dispersos: las aristas y el mapa
CFG usan posiciones, no IDs ni PCs. Todos los arreglos son accesibles realmente
y permanecen inmutables durante la construcción; C no autentica punteros.

`pendientes` debe ser cero. Para no vacío se exige declaración explícita de
completitud y reubicaciones resueltas. Esta última incluye ausencia o tratamiento
externo ya acreditado de constantes de código, también si aparecen en ALU/x13.
DAG exige además efectos acreditados y ABI para call/ret. No se deduce ABI de x1
ni se agregan clobbers del callee a las máscaras de un SYER.

La copia de `metadatos_completos` procede exclusivamente de
`garantias.completitud_declarada`, nunca de un éxito de S2/CFG/DAG. Distinguir:
estructura válida, coherencia física local, declaración externa y equivalencia
BAL demostrada. Esta API solo comprueba las dos primeras y exige la tercera.

## Validaciones y algoritmo

1. Acotar cantidad estructural a 4092 y comprobar punteros, textos y garantías.
2. Reconstruir DAG mediante su API real, que revalida CFG y metadatos mediante
   I1-A2a/S2. Comparar generación, operaciones, camino crítico y cada arista
   recibida por campos, incluidos tipo y distancia. Rechazar diferencias;
   nunca sustituir un DAG erróneo por la reconstrucción de contraste.
3. Copiar las instrucciones, aristas originales, partición/sucesores CFG y
   destinos originales. Cada texto pasa a almacenamiento propio.
4. Ejecutar I1-A1 real sobre la copia, conservando su diagnóstico e índices.
5. Ensamblar cada texto con CB en contexto privado y contrastar todos los
   campos semánticos S2 con la operación física. Máscaras iguales no bastan:
   otro opcode, inmediato, llave, dirección o desplazamiento debe rechazarse.
6. Publicar únicamente tras éxito completo; liberar todos los recursos privados
   en cualquier rechazo.

El texto admite menos de 160 bytes, una operación ASCII sin separadores,
etiquetas o comentarios inyectados, conforme a I1-A1/S8-A. Para BRU directa o
condicional, el último operando es marcador que S8-A sustituirá por el destino
lógico. Solo ese offset se normaliza a cero al contrastar localmente; no se
normalizan inmediatos de ALU, LSU, SYER o CRIPTO. No hay otro decodificador.

## Identidad y generación

Generaciones compatibles detectan inconsistencias declaradas, no autoría ni
historia del productor. CFG no conserva una firma histórica de palabras/IDs.
Una modificación que conserve forma, máscaras y estructura puede seguir siendo
coherente al reconstruir: el productor debe renovar generación y análisis al
modificar el flujo. No se presenta este límite como autenticación criptográfica.

Se exige el DAG determinista que genera el constructor actual, en su orden
exacto; no se aceptan grafos alternativos solo porque tengan cantidades iguales.
Los backedges CFG se conservan como control; nunca se fabrican aristas hacia atrás.

## API y resultado

```c
VliwProyeccionEstado vliw_proyeccion_construir(
    const VliwProyeccionEntrada *, VliwProyeccion **,
    VliwProyeccionDiagnostico *);
void vliw_proyeccion_liberar(VliwProyeccion **);
```

`VliwProyeccion` posee generación y vistas públicas entrada/destinos. Posee
instrucciones/textos, dependencias, bloques/sucesores y destinos; no retiene
préstamos. La salida sobrevive al borrado de los datos originales. El llamador
no debe modificar sus punteros/cantidades antes de liberar. El liberador admite
NULL y deja NULL; no libera memoria del productor.

El diagnóstico conserva categoría, fase de proyección, estado original, índices
y mensaje propio; también el diagnóstico I1-A1 cuando corresponde. Las fases
son inicio, contraste DAG, copia, validación I1-A1, contraste de texto y completa.
Se distinguen argumentos, obligaciones, reubicación, DAG, texto, integración,
capacidad, memoria y E/S. El diagnóstico debe estar separado de las entradas.

## Recursos, capacidad y costo

Salida inicialmente NULL y publicación atómica. Los hijos se inicializan antes
de adquirir sus recursos, de modo que la limpieza parcial no lee punteros
indeterminados. Los tamaños de copias derivan del CFG/DAG revalidados, no de
cantidades no comprobadas. No se comparan representaciones con padding.

El costo incluye una reconstrucción DAG y validación I1-A1: no es una copia
lineal del grafo. La copia añade O(n+m+sucesores+bytes de texto). Se heredan
las cotas 4092 operaciones y 65536 aristas; aceptar estructura no garantiza
que S7 pueda producir un plan de 1023 bundles. No se promete rendimiento para
todo grafo denso ni se elimina cobertura para acelerar un caso.

## Verificación y límites

La suite `test_vliw_proyeccion_fisica.py` compila en temporales C11 estrictos y
recorre positivos, riesgos, control, negativos y recursos, además de enlace
mínimo. Revisa vida independiente, copia exacta de destinos/aristas, tablas y
metadatos por campos, todas las reservas observadas y recuperación. Los
resultados definitivos y comandos de instrumentos están en la evidencia externa.

IMPLEMENTADO: adaptación consumidora y comprobación local con APIs reales.
PENDIENTE DE ACUERDO: acreditación productiva, ABI, observables y reubicaciones.
BLOQUEADO: conectar BAL optimizado hasta recibir una exportación física real.
