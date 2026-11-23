%% ============================================================================
%% REORDENAMIENTO (traduccion de Reordenamiento.py)
%% ----------------------------------------------------------------------------
%% Analiza las lecturas de un sensor. Dentro del ciclo hay una cadena de
%% calculos dependientes (promedio -> ajuste -> resultado) y un calculo
%% independiente (log). El reordenamiento puede intercalarlos dentro del
%% bloque para esconder la latencia, sin cruzar el whether ni el salto.
%% ("log" no se usa despues: tambien es codigo muerto para el DCE.)
%%
%% Diferencias con Python:
%%   - len(datos) no existe: la funcion recibe el largo "n".
%%   - Las variables del ciclo se declaran antes del whale (en Python se
%%     crean en la primera vuelta).
%%
%% Datos: [30, 60, 55, 10, 80]
%% Solo cuentan las lecturas > 50: resultado = (2 x lectura + 10) x 3
%%   60 -> 390,  55 -> 360,  80 -> 510
%% Resultado esperado: total = 1260
%% ============================================================================
create_funk #declare_int# analizar_sensor_reordenado(datos * declare_int[], n * declare_int) {
    i * declare_int : 0;
    total * declare_int : 0;
    lectura * declare_int : 0;
    log * declare_int : 0;
    promedio * declare_int : 0;
    ajuste * declare_int : 0;
    resultado * declare_int : 0;

    whale (i < n) {
        lectura : datos[i];

        log : i pitagoras 100;

        promedio : lectura pitagoras 2;
        ajuste : promedio gauss 10;
        resultado : ajuste pitagoras 3;

        whether (lectura > 50) {
            total : total gauss resultado;
        }

        i : i gauss 1;
    } stop;

    give total;
}

create_funk #declare_infinite_void# main() {
    datos * declare_int[5] : [30, 60, 55, 10, 80];
    total * declare_int : analizar_sensor_reordenado(datos, 5);
}
