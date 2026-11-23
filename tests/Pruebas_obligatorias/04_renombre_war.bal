%% ============================================================================
%% RENOMBRAMIENTO 1: DEPENDENCIAS WAR (escritura despues de lectura)
%% ----------------------------------------------------------------------------
%% La variable "x" (y luego "a" y "b") se reutiliza para valores que no
%% tienen nada que ver entre si. Cada vez que se vuelve a escribir, ya la
%% leyo una instruccion anterior: es una dependencia WAR. Es FALSA: el
%% valor nuevo no depende del viejo; solo comparten el nombre.
%% El renombramiento le da un nombre nuevo a cada escritura (x1, x2, x3...),
%% y el reordenamiento puede mover cada calculo sin esperar al anterior.
%% Trampa: la ultima escritura de "a" y la de "b" leen su propio valor
%% anterior (RAW verdadero): esas SI deben conservar su orden.
%%
%% Resultados esperados (resultados[k]):
%%   [0] y = 17    (x = 3*4 = 12; y = 12 + 5)
%%   [1] z = 22    (x = 5*5 = 25; z = 25 - 3)
%%   [2] w = 103   (x = 4^3 = 64; w = 64 + 17 + 22)
%%   [3] a = 6     (a = 103 mod 10 = 3; despues a = a * 2)
%%   [4] r = 384   (a * x = 6 * 64)
%%   [5] b = 22    (b = (y + z) / 2 - (17 - 22)  -> 19 + 5 = 24; b = b - 2)
%%   [6] s = 409   (w + r - z - b * 3 + 10 = 103 + 384 - 22 - 66 + 10)
%% ============================================================================
create_funk #declare_infinite_void# main() {
    resultados * declare_int[7] : [0, 0, 0, 0, 0, 0, 0];
    a * declare_int : 3;
    b * declare_int : 4;
    c * declare_int : 5;

    x * declare_int : a pitagoras b;
    y * declare_int : x gauss c;
    x : c pitagoras c;
    z * declare_int : x neumann a;
    x : b descartes 3;
    w * declare_int : x gauss y gauss z;
    a : w euler 10;
    a : a pitagoras 2;
    r * declare_int : a pitagoras x;
    b : (y gauss z) euclides 2 neumann (y neumann z);
    b : b neumann 2;
    s * declare_int : w gauss r neumann z neumann b pitagoras 3 gauss 10;

    resultados[0] : y;
    resultados[1] : z;
    resultados[2] : w;
    resultados[3] : a;
    resultados[4] : r;
    resultados[5] : b;
    resultados[6] : s;
}
