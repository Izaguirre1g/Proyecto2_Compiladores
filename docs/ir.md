# Proyecto II — IR de tres direcciones (primera integración)

## Contrato de datos

- `include/ir.h`: `IrPrograma` conserva un arreglo ordenado de `IrInstr` y bloques básicos `IrBloque`.
- Cada instrucción almacena `op`, `dst` (destino), `a`, `b`, `operador` y `linea`.
- Los identificadores provienen del AST; los temporales se crean como `$t1`, `$t2`, etc. y las etiquetas como `$L1`, `$L2`, etc. Esos prefijos no son identificadores válidos de BAL.
- `IR_BIN`: `dst = a operador b`; `IR_UNARY`: `dst = operador a`. `bool` convierte un valor lógico a `0/1`.
- `IR_JFALSE` / `IR_JTRUE` hacen salto condicional; `IR_JUMP` incondicional; `IR_LABEL` define un destino.
- `IR_ARG` pasa un argumento en orden de aparición; `IR_CALL` produce un valor temporal incluso cuando la llamada es una sentencia (el resultado puede ignorarse). `IR_PARAM_DECL` anota un parámetro formal.
- Los bloques contienen intervalos de instrucciones `[inicio, fin)` y sus sucesores indexados; no se calcula todavía liveness.
- **Los `IR_CALL` e `IR_ARG` son operaciones con efectos / barreras para optimizaciones hasta que exista un análisis de efectos.**
- La IR no usa nombres de registros físicos ni impone formato de slots; el lowering al ISA pertenece al backend y a la calendarización.

## Cómo probar

```sh
make test-ir                         # solo GCC, no necesita Bison o Flex
make                               # requiere Flex, Bison y dependencias del Proyecto I
./balc --ir examples/factorial.bal  # muestra IR y CFG, sin crear .asm/.bin
```

`--ir` usa `sem_visitar_proyecto`: el proyecto (incluidas sus importaciones) se valida semánticamente **antes** de crear IR. Solo genera IR para el AST del archivo principal; todavía no enlaza cuerpos de funciones importadas. `--ir` es excluyente con las demás opciones para evitar modificar el backend existente.

## Primera etapa implementada

- Declaraciones y asignaciones escalares, expresiones de dos y un operando.
- Cortocircuito para `&&` y `||` usando etiquetas y saltos; evita evaluar el operando derecho cuando no corresponde.
- Funciones y parámetros formales; llamadas y retornos.
- `whether` / `alif` / `also`, `whale` y `cycle` con paso constante positivo.
- Líderes de bloque: entrada de función, etiquetas y posición siguiente a una terminación de flujo. Aristas de saltos y continuación de flujo.

## Límites intencionales (no generar IR incorrecta silenciosamente)

- No se bajan aún declaraciones globales, listas, matrices, índices, `seek/seize`, miembros `alias.miembro`, ni inicializadores de colecciones.
- `cycle` con paso dinámico o negativo da diagnóstico y falla (no se inventa una traducción).
- El generador no asigna registros, no hace spilling, DCE, loop unrolling ni empaquetado VLIW todavía.
- La IR es una visualización/representación independiente: **el `.bin` normal sigue usando el generador previo**. La integración definitiva `IR -> optimizaciones -> registros -> VLIW` es el siguiente hito del equipo.
- Cadenas y caracteres conservan el texto del AST; el backend deberá codificar y preservar el tipo adecuado.

## Responsables de la integración

Dylan mantiene el contrato IR y el futuro spilling; José consume bloques para DCE; Meibel usa CFG y operaciones para unrolling/reordenamiento; Javier realiza calendarización VLIW y lowering al ISA. Las funciones futuras deben declarar reglas de efectos y dependencias antes de reordenar operaciones.
