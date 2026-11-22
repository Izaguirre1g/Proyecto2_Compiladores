# Integración independiente IR–VLIW y Arquitectura

Referencia auditada: Compiladores `9cf06e20b2ed6dcba418f8a82764f33152ed561f`;
Arquitectura `5e6e45f7baacff4a70ad44ec2e3a2c61c0defe26`.

## Ruta real y propiedad

La prueba llama `ir_vliw_desde_ir`: spilling→preparación del marco→lowering de
Dylan→CB/S2→cadena física de Javier→CFG/DAG/proyección→I1-A1→S7/verificador→
S8-A/CB/S2/S8-C. El driver no construye dependencias ni reemplaza el backend.
La IR original permanece inmutable por comparación de su representación pública.
El productor posee el resultado hasta transferirlo; se libera por su API real.
Los temporales y ejecutables del orquestador son externos a ambos repositorios.

PR #68 añade USE/DEF compartido, mapeo físico y latencias/efectos simbólicos.
El enlace de pruebas incluye esos módulos cuando existen en el snapshot probado.
La validez de registros después del spilling no añade lowering de variables BAL.

## Precondiciones y observación

Para fragmentos: PC inicial 0, RA 0, SP inicializado por tres bundles del arnés
a 1000, DMEM inicialmente cero. GP no se requiere en el subconjunto probado.
El frame de cada fixture debe caber debajo de SP. Los tres bundles anteriores
a la primera instrucción del fragmento respetan N+3. Sus bytes ocupan el tramo
[48, 48+tamaño_fragmento); se comparan exactamente contra CB y Arquitectura.
Solo se admiten los destinos relativos internos acreditados por el productor.
Dos NOP y un SYE relativo cero dan un punto de observación estable al final.
No hay HALT, llamadas ficticias, retorno semántico de main ni terminación añadida.

Se comparan los 32 GPR, los 65536 bytes de DMEM, estado criptográfico público y
traps. Los registros se capturan post-NBA al horizonte de avance; RAM se captura
tras flush acreditado por el banco público. MEM_MODE=2 usa L2_MODEL=0.
Las métricas globales/stalls/cache y los ciclos M1 se mantienen separados.
Los 1000 avances de fixtures incluyen el bucle de observación: no son un benchmark
ni prueban mejora de rendimiento. B1 puede concluir INCONCLUSO por no terminar.

## Casos y oráculos

17 casos IR ejercitan copia, suma/resta, negación, extremos -1024/1023,
negativos, independencia, RAW/WAR/WAW, presión, ramas, salto directo y backedge.
Los valores se fijan desde la operación lógica, fuera del scheduler. El caso de
presión produce 12 ranuras, 8 cargas, 12 stores y 17 desalojos con tres registros;
esos datos se registran como resultado, no se usan para fabricar el DAG.
DCE sobre el fragmento puro verifica transformación y restauración SP/RA: las
ranuras internas eliminadas no constituyen un observable BAL persistente.

La ruta BAL/frontend/AST/IR real usa `examples/vliw_ir_preview.bal` y
`--vliw-ir=3 -v -s -p`: acredita generación y control, sin resultado BAL de datos.
Los casos tradicionales `condicional`, `whale`, `llamadas` producen 20, 21 y 14,
respectivamente. B5 recalcula los cortes 109, 247 y 447; x16..31 comienzan con
centinelas distintos y se conservan. Este caso de llamada prueba su ruta
tradicional, no el ABI de llamadas del productor experimental.

## Reproducción

En una copia externa fijada al commit de Compiladores, con Python, compilador C,
flex/bison e Icarus disponibles:

```bash
make -B balc
python3 -B tools/vliw/validar_integracion_ir_rtl.py \
  --arquitectura /ruta/copia-arquitectura-develop \
  --salida /ruta/externa/nueva --balc /ruta/copia-compiladores/balc
ARQUITECTURA_ROOT=/ruta/copia-arquitectura-develop \
  python3 -B tests/vliw/test_integracion_ir_rtl.py -v
```

`--sin-rtl` no acredita hardware; `--balc` exige RTL para los tres casos BAL.
Las opciones `--perfil` son auxiliares y opt-in, sin fallback ni opciones -O nuevas.

## Rechazos y recursos

Se ejercitan 17 escenarios de rechazo de IR/configuración/control/capacidad,
salida ocupada y publicación atómica. Una muestra distribuida de 16 fallos de
reserva exige 16 recuperaciones; no es inyección exhaustiva de todas las
combinaciones. La imagen corrupta se rechaza antes de publicar métricas; los
modos inválidos y dumps GPR repetidos tampoco se aceptan.

## Límites pendientes

CALL/RETURN/PARAM, variables BAL por nombre, multiplicación y otras operaciones
no incluidas siguen sin lowering experimental. El marco total y sus offsets
deben ser representables; no se implementan expansiones de inmediatos largos,
reubicaciones generales ni ABI de funciones desde este arnés. El uso de x16..23
no prueba preservación entre llamadas: hacen falta saves/restores reales y
contratos de símbolos, continuidad y procedencia. D18 no se agrega a CE1108.
La GUI GTK no se acredita con estas pruebas de APIs/CLI y RTL.
