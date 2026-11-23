%% ============================================================================
%% SPILLING 3: VALORES VIVOS A TRAVES DE LLAMADAS Y RECURSION
%% ----------------------------------------------------------------------------
%% Lo mas exigente para el spilling junto con la convencion de llamadas:
%%   - horner(): cuatro parametros (el maximo del backend -O0), que llegan
%%     en x8..x11. Calcula c0 + 2c1 + 4c2 + 8c3: cada coeficiente pesa
%%     distinto, asi que si dos argumentos se cruzaran el resultado cambiaria.
%%   - en main, a..f se calculan ANTES de las llamadas y se usan DESPUES:
%%     tienen que sobrevivir a cinco llamadas (la funcion usa los mismos
%%     registros temporales), ademas de p1, p2, s y anidado.
%%   - suma_dobles(): recursiva; "doble" se calcula ANTES de la llamada
%%     recursiva y se usa DESPUES (cada nivel lo guarda en su marco, 30
%%     niveles de profundidad).
%%   - horner(p1, e, suma_dobles(2), a): una llamada como argumento de otra,
%%     con p1 (resultado de una llamada anterior) como primer argumento.
%%
%% Resultados esperados (resultados[k]):
%%   [0] p1 = horner(1, 2, 3, 4) = 1 + 4 + 12 + 32 = 49
%%   [1] p2 = horner(6, 5, 4, 3) = 6 + 10 + 16 + 24 = 56
%%   [2] s  = suma_dobles(30) = 2 + 4 + ... + 60 = 930
%%   [3] anidado = horner(49, 5, 6, 1) = 49 + 10 + 24 + 8 = 91
%%   [4] vivos = (a + b + c + d + e + f) + p1 - p2 + s + anidado = 21 + 49 - 56 + 930 + 91 = 1035
%% ============================================================================
create_funk #declare_int# horner(c0 * declare_int, c1 * declare_int, c2 * declare_int,
                                 c3 * declare_int) {
    r * declare_int : c3;
    r : r gauss r gauss c2;
    r : r gauss r gauss c1;
    r : r gauss r gauss c0;
    give r;
}

create_funk #declare_int# suma_dobles(n * declare_int) {
    r * declare_int : 0;
    whether (n > 0) {
        doble * declare_int : n gauss n;
        parcial * declare_int : suma_dobles(n neumann 1);
        r : parcial gauss doble;
    }
    give r;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[5] : [0, 0, 0, 0, 0];
    a * declare_int : 1;
    b * declare_int : 2;
    c * declare_int : 3;
    d * declare_int : 4;
    e * declare_int : 5;
    f * declare_int : 6;

    p1 * declare_int : horner(a, b, c, d);
    p2 * declare_int : horner(f, e, d, c);
    s * declare_int : suma_dobles(30);
    anidado * declare_int : horner(p1, e, suma_dobles(2), a);
    vivos * declare_int : ((a gauss b) gauss (c gauss d)) gauss ((e gauss f) gauss (p1 neumann p2))
                        gauss s gauss anidado;

    resultados[0] : p1;
    resultados[1] : p2;
    resultados[2] : s;
    resultados[3] : anidado;
    resultados[4] : vivos;
}
