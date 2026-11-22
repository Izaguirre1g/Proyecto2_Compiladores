%% ============================================================================
%% SPILLING 2: MUCHAS VARIABLES VIVAS DURANTE UN CICLO
%% ----------------------------------------------------------------------------
%% Ocho acumuladores se actualizan en CADA vuelta y todos siguen vivos de una
%% vuelta a la siguiente (y hasta el final). Con 3 registros no caben: el
%% spilling los guarda y los carga DENTRO del cuerpo del ciclo, en cada
%% vuelta, sin perder ninguno ni confundirlos.
%%   - s1, s2, s3: cada uno se alimenta del anterior (s2 += s1, s3 += s2):
%%     si un valor recargado fuera el de otra variable, los tres cambian.
%%   - s4 y signo: suma alternada (signo cambia en cada vuelta).
%%   - fib1, fib2: rotacion tipo Fibonacci dentro del mismo cuerpo.
%%   - mx, mn: maximo y minimo de una funcion que sube y baja, con whether
%%     (los acumuladores tienen que sobrevivir a los saltos).
%% El cuerpo es largo, asi que el unrolling no lo desenrolla: el spilling se
%% ve tal cual dentro del ciclo.
%%
%% Resultados esperados para i = 1..20 (resultados[k]):
%%   [0] s1 = 210     (suma de i)
%%   [1] s2 = 1540    (suma de las sumas parciales: numeros triangulares)
%%   [2] s3 = 8855    (suma de los triangulares acumulados)
%%   [3] s4 = -10     (1 - 2 + 3 - ... - 20)
%%   [4] fib2 = 10946 (fibonacci de 21)
%%   [5] mx = 4       (maximo de valor = s4 + 2 * signo - i / 4)
%%   [6] mn = -13     (minimo de la misma funcion)
%% ============================================================================
create_funk #declare_infinite_void# main() {
    resultados * declare_int[7] : [0, 0, 0, 0, 0, 0, 0];
    s1 * declare_int : 0;
    s2 * declare_int : 0;
    s3 * declare_int : 0;
    s4 * declare_int : 0;
    signo * declare_int : 1;
    fib1 * declare_int : 0;
    fib2 * declare_int : 1;
    tmp * declare_int : 0;
    mx * declare_int : neumann 1000;
    mn * declare_int : 1000;
    valor * declare_int : 0;

    cycle i let 1 until 21 {
        s1 : s1 gauss i;
        s2 : s2 gauss s1;
        s3 : s3 gauss s2;
        s4 : s4 gauss signo pitagoras i;
        signo : neumann signo;
        tmp : fib1 gauss fib2;
        fib1 : fib2;
        fib2 : tmp;
        valor : s4 gauss signo gauss signo neumann (i euclides 4);
        whether (valor > mx) {
            mx : valor;
        }
        whether (valor < mn) {
            mn : valor;
        }
    } endgame;

    resultados[0] : s1;
    resultados[1] : s2;
    resultados[2] : s3;
    resultados[3] : s4;
    resultados[4] : fib2;
    resultados[5] : mx;
    resultados[6] : mn;
}
