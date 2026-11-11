# CFG físico independiente — I1-A2b-1

## Objetivo y estado

**IMPLEMENTADO:** `vliw_cfg_construir` recibe operaciones ISA físicas y referencias lógicas explícitas; construye bloques básicos, mapa posición→bloque, sucesores y destinos compatibles con S7/S8-A. No modifica entradas. Producción depende únicamente de S2, además de la biblioteca C; incluye tipos existentes mediante sus cabeceras.

**PROPUESTO:** conectar este resultado con el futuro constructor DAG I1-A2b-2. **PENDIENTE DE ACUERDO / BLOQUEADO para BAL real:** productor estructurado, ABI/asignación/spills, efectos completos, reubicaciones y resultados observables. Un CFG correcto no certifica equivalencia BAL, un DAG correcto ni un plan válido. Las propuestas anteriores de `contrato-productor-fisico.md` se conservan como antecedentes, sin modificar ese archivo.

## API y responsabilidades

```c
VliwCfgEstado vliw_cfg_construir(const VliwCfgEntrada *entrada,
    VliwCfg **salida, VliwCfgDiagnostico *diagnostico);
void vliw_cfg_liberar(VliwCfg **cfg);
```

El llamador proporciona almacenamiento accesible e inmutable durante la llamada, tablas coherentes y salida/diagnóstico separados de la entrada. `salida` debe apuntar a NULL; se rechaza una salida ocupada sin reemplazarla. Solo OK publica. Los errores no modifican la entrada ni el puntero anterior de salida. El diagnóstico es opcional, por valor, con índice de la tabla mencionada en el mensaje y literal estático sin liberación. Un índice VLIW_VACIO representa un error global.

## Contrato de entrada e identidades

| Elemento público nuevo | Información explícita |
|---|---|
| `VliwCfgOperacion` | ID único, palabra de 32 bits, slot, `SimSalto` declarado y `VliwControl` externo |
| `VliwCfgFuncion` | ID, intervalo `[inicio,fin)` no vacío; las funciones cubren todo el flujo sin huecos ni solapamientos |
| `VliwCfgSimbolo` | ID simbólico inequívoco, ID de función y posición física asociada |
| `VliwCfgTransferencia` | Posición original de BRU, `VliwDestinoTipo`, ID tomado directo, continuación por posición, símbolos posibles dinámicos y declaración de cobertura |
| `VliwCfgRegion` | Intervalo indivisible que no admite líderes interiores |
| `VliwCfgEntrada` | Tablas, fronteras adicionales acreditadas, generaciones y obligaciones pendientes de CFG |

ID de operación, posición, ID de símbolo, ID de función y PC son conceptos distintos. Las operaciones admiten ID SIZE_MAX; los símbolos/funciones reservan VLIW_VACIO para ausencia, por lo que no puede ser su ID. Los IDs de cada tabla son únicos; varias identidades simbólicas diferentes pueden señalar una misma entrada. No se buscan nombres textuales ni se reciben PCs definitivos. Dos símbolos con igual ID se rechazan incluso si tienen la misma posición: para alias se usan IDs diferentes.

S2 debe aceptar cada palabra como operación no-NOP en su slot. Se contrasta su forma de salto con la declarada; no se reinterpreta una palabra reservada como ADD ni NOP. El control es suministrado por el productor: escribir x1 no permite inferir llamada, ni leerlo permite inferir retorno. Las formas reales son compatibles con las comprobaciones de I1-A1/S8-A: condicional para salto, SYE para directo, SYER para indirecto/retorno. No se valida un ABI completo ni el valor final del registro destino de SYER.

`generacion` y `generacion_referencias` deben coincidir. No se usan direcciones de memoria como identidad. Esta comparación detecta discrepancias declaradas; no autentica al productor ni descubre cambios a los que no se asignó otra generación.

`pendientes!=0` rechaza obligaciones CFG sin resolver. No se reutilizan ni se borran los bits parciales de I1-A2a: estas obligaciones son otro contrato. `fronteras_acreditadas=true` afirma que el productor suministró las fronteras y regiones necesarias, incluidas expansiones indivisibles. Sin esa afirmación se rechaza; con ella solo se comprueban los datos descritos, no restricciones omitidas por el productor.

El vacío es válido si todas las tablas tienen cantidad cero y las acreditaciones/generaciones son coherentes. No se exige que punteros de cantidad cero sean NULL. La cota previa es 4092 operaciones, coherente con 4×1023 bundles de S7: no demuestra que un plan posterior quepa. Se detectan cantidades absurdas/overflow antes de recorrerlas; C no permite comprobar la accesibilidad de un puntero arbitrario.

## Fronteras, intervalos y mapa

El constructor marca como líderes el inicio de cada función, todas las etiquetas definidas, fronteras adicionales del productor y posiciones posteriores a una transferencia. Los destinos posibles ya se asocian a símbolos validados. Cada BRU termina bloque aunque la operación siguiente sea inalcanzable; el módulo no elimina código.

Después recorre líderes en orden y crea intervalos contiguos no vacíos. Cada posición recibe un único índice en `bloque_por_operacion`. El resultado no copia ni altera operaciones y no reutiliza un campo de bloque antiguo. Insertar prólogos, spills o expansiones cambia las fronteras; por eso este análisis no copia el CFG virtual de la IR.

Antes de construir se rechaza cualquier líder estrictamente interior a una región indivisible, incluso si proviene de una etiqueta no utilizada o de una BRU interior. Una región debe quedar dentro de una función. Regiones solapadas son admisibles si se satisfacen todas sus restricciones. Su completitud sigue siendo obligación del productor, no una propiedad deducida de palabras ISA.

## Sucesores y destinos

Los sucesores son conjuntos: tomado y caída coincidentes, o varios símbolos dinámicos para una misma entrada, producen un único bloque sucesor. No se crea un bloque de salida vacío ni se convierten etiquetas a direcciones.

| Caso | Regla implementada |
|---|---|
| Sin transferencia | Única caída al bloque siguiente; al final del flujo no hay sucesor |
| Condicional | Tomado por símbolo; continuación obligatoriamente posición i+1, o VLIW_VACIO si termina el flujo |
| SYE como salto | Destino directo acreditado dentro de la misma función; sin caída |
| Llamada directa | Símbolo de entrada de función y continuación i+1 dentro de la función llamadora |
| Llamada indirecta | Conserva tipo INDIRECTO y tomado=VLIW_VACIO; posibles destinos acreditados deben ser entradas de función; se agrega continuación |
| Retorno | Tipo RETORNO externo y forma SYER; tomado y continuación VLIW_VACIO; destinos posibles solo si el productor los aporta |
| Indirecto genérico | SYER sin convertirlo a directo; posibles símbolos dentro de su función |
| Backedge | Puede apuntar a bloque anterior, inicial o propio; es válido en CFG |

Cada BRU requiere exactamente una transferencia. Una operación no BRU no puede tenerla. Directos deben declarar `completos=true` y no una lista dinámica; los NO_RESUELTO se rechazan. Las llamadas sin continuación física se rechazan en este alcance: no se modelan tail-calls. Saltos no llamados entre funciones también se rechazan, en lugar de atribuirles implícitamente un ABI.

Se rechaza la caída entre funciones. Un fin de función que no es el último del flujo necesita transferencia explícita; no basta que el productor lo llame salida abstracta. La caída de condicional/call debe seguir dentro de su función, aunque haya una función físicamente adyacente.

Para indirectos, la lista solo contiene símbolos acreditados existentes. `completos=false` permite construir un CFG estructural limitado, con `destinos_dinamicos_acreditados=false` en el resultado. No se afirma que los sucesores conocidos cubran toda ejecución. Una lista vacía con `completos=true` se rechaza: no acredita una transferencia efectiva. `true` es declaración externa, nunca prueba semántica. La futura integración real queda bloqueada si requiere una cobertura que no se proporcionó.

Los ciclos CFG no son ciclos de un DAG: no se recorre recursivamente el destino ni se crean dependencias de emisión hacia atrás. Este módulo no calcula latencias ni inserta guardas/NOP; S7 mantiene esas responsabilidades.

## Propiedad, recursos y publicación

`VliwCfg` posee bloques, sucesores por bloque, mapa y destinos. No retiene punteros de entrada; tras éxito estas tablas propias sobreviven a las entradas. El cliente mantiene sus operaciones si quiere proyectarlas después. No debe modificar punteros/cantidades del resultado antes de liberarlo.

Se validan dimensiones, se reservan líderes/referencias temporales y se comprueban tablas. Luego se reservan resultado y arreglos, se construyen intervalos y se deduplican/reservan sucesores. Solo al terminar se escribe `*salida`. Ante fallo se libera todo lo adquirido, incluyendo una construcción parcial sin arreglo de bloques. `vliw_cfg_liberar` admite NULL y dobles llamadas con el puntero ya vaciado.

No se adquieren archivos ni recursos de E/S en producción. Fallos de reserva se informan como MEMORIA. Los tamaños de las reservas propias derivan de cantidades acotadas y de sucesores deduplicados, nunca de un PC o de multiplicaciones sin control.

## Diagnósticos y rechazos

Las categorías son OK, ARGUMENTO, ESTRUCTURA, IDENTIDAD, ISA, CONTROL, DESTINO, FRONTERA, INCOMPLETO, CAPACIDAD y MEMORIA. Se agrupan causas relacionadas, con mensaje específico: tabla/puntero ausente; partición; IDs duplicados; palabra inválida/NOP; forma/control; símbolo inexistente; caída o región indivisible; crédito/generación pendiente; cantidad fuera de límites; reserva fallida.

El índice indica operación, transferencia, símbolo, función, región o frontera según el mensaje; no siempre es posición de instrucción. El constructor retorna el primer rechazo y no repara datos ni publica un resultado parcial. Error estructural no implica fallo funcional del procesador ni ejecuta una instrucción.

## Compatibilidad con I1-A1, S7 y S8-A

`bloques` son `VliwBloque` existentes, con sucesores propios. `destinos` son `VliwDestino` existentes y se presentan mediante `VliwDestinos tabla={cfg->destinos,cfg->cantidad_destinos}`. `bloque_por_operacion` permite proyectar `VliwInstruccion.bloque`.

El resultado no es un `VliwEntrada`: no proporciona texto, máscaras, efectos, dependencias, reubicaciones ni declaración `metadatos_completos`. La prueba controlada añade manualmente datos completos de dos fixtures: suma→store con RAW3 y salto al bloque propio. Solo esos datos explícitos permiten I1-A1→S7→verificador→S8-A. Las aristas no fueron construidas por este módulo. Los ejemplos dinámicos con cobertura incompleta se verifican exclusivamente como CFG y no se promocionan a una integración completa.

## Pruebas y reproducción

Las suites nuevas compilan en temporales con C11 y advertencias como errores. Los oráculos de intervalos/mapas/conjuntos/destinos se fijan manualmente. Cubre vacío/secuencias, alias, fronteras/regiones, directos/condicionales/ciclos, call/ret/indirectos, varias funciones, rechazos/overflow, máximo estructural real, inmutabilidad, recursos y compatibilidad controlada. Una sonda enlaza solo constructor+S2.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B tests/vliw/test_vliw_cfg_fisico.py -v
```

CC admite flags de sanitizadores. I1CFG_EVIDENCIA permite JSON fuera del repositorio. La variante de recursos intercepta malloc/calloc únicamente en pruebas, falla cada reserva observada y repite sano. ASan/UBSan/LSan y Memcheck se aplican a C; Memcheck complementa detección de memoria no inicializada y fugas, que ASan no acredita completamente. Los resultados reales, auditoría de comentarios y regresiones se registran externamente en la carpeta I1-A2b-1; no se usan cifras históricas como pruebas nuevas.

## Garantías, límites y preparación de I1-A2b-2

Se garantiza coherencia de la partición y referencias explícitas dentro de este alcance, salida propia atómica e inmutabilidad. No se autentican símbolos, generaciones, clasificación ABI o cobertura dinámica declarados por un productor. No se reconstruyen restricciones de expansión omitidas ni se certifican errores observables BAL.

I1-A2b-2 deberá recibir el flujo acreditado y este CFG de la misma generación y aportar cobertura de dependencias físicas compatible con I1-A1. Productor real, clobbers/preservación, efectos, reubicaciones y observación siguen PENDIENTES DE ACUERDO con el equipo. Esa fase y la conexión BAL permanecen sin implementar; requieren nueva autorización.
