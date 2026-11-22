%% ============================================================================
%% REORDENAMIENTO 3: DEPENDENCIAS DE MEMORIA EN CICLOS, SALTOS Y LLAMADAS
%% ----------------------------------------------------------------------------
%% Ordenamiento burbuja: el caso mas peligroso para el reordenamiento, porque
%% lee y escribe el MISMO arreglo con indices que solo se conocen al ejecutar.
%%   - El ciclo interno lee v[j] y v[j+1] para comparar. Si el reordenamiento
%%     adelantara esas lecturas a la escritura de la vuelta anterior (que
%%     pudo cambiar v[j]), compararia valores viejos y el orden saldria mal.
%%   - El intercambio lo hace intercambiar(), que ESCRIBE en el arreglo que
%%     recibe: la llamada es una barrera, nada de memoria puede cruzarla.
%%   - Ciclos anidados: el limite del interno (5 - i) cambia en cada vuelta
%%     del externo, asi que su numero de vueltas no se conoce al compilar.
%%   - El whether depende de datos que el propio ciclo va cambiando.
%%   - "cambios" se acumula con el valor que devuelve la llamada.
%%
%% Datos: v = [13, -3, 8, 21, 0, -9]
%% Resultados esperados (resultados[k]):
%%   v ordenado = [-9, -3, 0, 8, 13, 21]
%%   [0] v[0] = -9            [1] v[5] = 21
%%   [2] v[2] + v[3] = 8      [3] cambios = 10 (intercambios hechos)
%% ============================================================================
create_funk #declare_int# intercambiar(v * declare_int[], j * declare_int) {
    t * declare_int : v[j];
    v[j] : v[j gauss 1];
    v[j gauss 1] : t;
    give 1;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[4] : [0, 0, 0, 0];
    v * declare_int[6] : [13, neumann 3, 8, 21, 0, neumann 9];
    cambios * declare_int : 0;
    limite * declare_int : 0;

    cycle i let 0 until 5 {
        limite : 5 neumann i;
        cycle j let 0 until limite {
            whether (v[j] > v[j gauss 1]) {
                cambios : cambios gauss intercambiar(v, j);
            }
        } endgame;
    } endgame;

    resultados[0] : v[0];
    resultados[1] : v[5];
    resultados[2] : v[2] gauss v[3];
    resultados[3] : cambios;
}
