%% ============================================================================
%% CODIGO MUERTO 3: OPERACIONES QUE PUEDEN FALLAR Y LLAMADAS ANIDADAS
%% ----------------------------------------------------------------------------
%% Una instruccion cuyo resultado nadie usa NO siempre se puede quitar:
%%   SE ELIMINA:
%%     - la cadena a -> b -> c: c nunca se lee (tres pasadas del DCE).
%%     - en dividir(), "doble = a + a": nadie lo usa.
%%   NO SE ELIMINA:
%%     - q1 = 100 / d: divide entre una VARIABLE; si d fuera 0 el programa
%%       daria error, y quitarla cambiaria el comportamiento (regla de oro).
%%     - x1 = dividir(90, 3): x1 no se lee, pero es una llamada.
%%     - llamadas anidadas: guardar(res, 0, dividir(uno * 6, 2)), con
%%       uno = guardar(res, 1, 7): DOS escrituras en res, en un orden fijo.
%%     - res[3] : dividir(guardar(res, 3, 50), 5): guardar escribe 50 en
%%       res[3] y DESPUES la asignacion lo cambia por 10. Si se invirtiera
%%       el orden quedaria 50.
%%
%% Resultados esperados:
%%   res = [21, 7, 25, 10]
%%     res[1] = 7; 7 * 6 = 42; 42 / 2 = 21 -> res[0] = 21
%%     res[2] = y + d = 21 + 4 = 25
%%     res[3] = 50 / 5 = 10 (pisa el 50 que escribio guardar)
%%   resultados[0] = y = 21
%% ============================================================================
create_funk #declare_int# dividir(a * declare_int, b * declare_int) {
    doble * declare_int : a gauss a;
    give a euclides b;
}

create_funk #declare_int# guardar(v * declare_int[], i * declare_int, x * declare_int) {
    v[i] : x;
    give x;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[1] : [0];
    res * declare_int[4] : [0, 0, 0, 0];
    d * declare_int : 4;

    q1 * declare_int : 100 euclides d;
    x1 * declare_int : dividir(90, 3);
    a * declare_int : 5;
    b * declare_int : a gauss 1;
    c * declare_int : b gauss b;

    uno * declare_int : guardar(res, 1, 7);
    y * declare_int : guardar(res, 0, dividir(uno pitagoras 6, 2));
    res[2] : y gauss d;
    res[3] : dividir(guardar(res, 3, 50), 5);

    resultados[0] : y;
}
