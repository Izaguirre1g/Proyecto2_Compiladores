%% ============================================================================
%% REORDENAMIENTO - adaptacion de ArchivosDefensa/Reordenamiento.py
%% Basado en develop 370528a. BAL permite variables locales y constantes
%% globales; la salida se deja como variable local de main.
%% Datos: [30, 60, 55, 10, 80]. Solo cuentan 60, 55 y 80.
%% Resultado esperado: salida_total = 390 + 360 + 510 = 1260.
%% El calculo independiente log es codigo muerto para DCE. No se debe
%% afirmar que la pasada de reordenamiento movio instrucciones sin medirlo.
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
    salida_total * declare_int : analizar_sensor_reordenado(datos, 5);
}
