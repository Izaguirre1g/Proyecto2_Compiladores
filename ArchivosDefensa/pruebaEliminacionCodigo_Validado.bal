%% Especializado para A 2x3 y B 3x2; dos whale y una multiplicacion por termino.
%% C=[58,64,139,154]. Se preserva el calculo de las matrices, no constantes precalculadas.
create_funk #declare_int# multiplicacion_matrices(A * declare_int[], B * declare_int[], C * declare_int[]) {
    temporal_externo * declare_int : 0;
    basura1 * declare_int : 0;
    basura2 * declare_int : 0;
    desperdicio * declare_int : 0;
    i * declare_int : 0;
    j * declare_int : 0;
    baseA * declare_int : 0;
    baseC * declare_int : 0;
    suma * declare_int : 0;
    whale (i < 2) {
        baseA : i gauss i gauss i;
        baseC : i gauss i;
        temporal_externo : i gauss 100;
        j : 0;
        whale (j < 2) {
            basura1 : i gauss j;
            suma : (A[baseA] pitagoras B[j]) gauss (A[baseA gauss 1] pitagoras B[j gauss 2]);
            suma : suma gauss (A[baseA gauss 2] pitagoras B[j gauss 4]);
            C[baseC gauss j] : suma;
            basura2 : suma gauss 999;
            j : j gauss 1;
        } stop;
        i : i gauss 1;
    } stop;
    desperdicio : basura2 gauss 2;
    give 0;
}
create_funk #declare_infinite_void# main() {
    a * declare_int[6] : [1,2,3,4,5,6];
    b * declare_int[6] : [7,8,9,10,11,12];
    c * declare_int[4] : [0,0,0,0];
    listo * declare_int : multiplicacion_matrices(a,b,c);
}
