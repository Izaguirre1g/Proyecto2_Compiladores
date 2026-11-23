%% Constantes globales y locales (plegado de constantes en
%% src/Analisis_Semantico/semantic_modules.c). Resultados esperados:
%%   DOBLE = 20, NEG = -3, LOCAL = 17, whether (DEBUG) -> a = 1,
%%   fila(1) = TABLA[1][2] = 6, tapada(100) = 101 (el parametro tapa a BASE),
%%   TABLA[j][0] con j = 1 -> 4, v[2] = 9 (v se declara con tamano N).
declare_const BASE * declare_int : 10;
declare_const DOBLE * declare_int : BASE pitagoras 2;
declare_const NEG * declare_int : neumann 3;
declare_const DEBUG * declare_boolean : DOBLE > 15;
declare_const TABLA * declare_int[2][3] : [[1, 2, 3], [4, 5, 6]];
declare_const N * declare_int : 3;

create_funk #declare_int# fila(i * declare_int) {
    give TABLA[i][2];
}

create_funk #declare_int# tapada(BASE * declare_int) {
    give BASE gauss 1;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[8] : [0, 0, 0, 0, 0, 0, 0, 0];
    v * declare_int[N] : [7, 8, 9];
    declare_const LOCAL * declare_int : DOBLE gauss NEG;
    a * declare_int : 0;
    whether (DEBUG) {
        a : 1;
    }
    b * declare_int : fila(1);
    c * declare_int : tapada(100);
    j * declare_int : 1;
    d * declare_int : TABLA[j][0];
    resultados[0] : DOBLE;
    resultados[1] : NEG;
    resultados[2] : LOCAL;
    resultados[3] : a;
    resultados[4] : b;
    resultados[5] : c;
    resultados[6] : d;
    resultados[7] : v[2];
}
