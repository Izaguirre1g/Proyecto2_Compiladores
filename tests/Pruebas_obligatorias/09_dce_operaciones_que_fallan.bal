%% ============================================================================
%% CODIGO MUERTO 3: OPERACIONES QUE PUEDEN FALLAR Y LLAMADAS ANIDADAS
%% ----------------------------------------------------------------------------
%% Una instruccion cuyo resultado nadie usa NO siempre se puede quitar.
%% El codigo muerto esta dentro de las funciones: las variables de main son
%% el RESULTADO del programa (BAL no tiene print) y se conservan aunque no
%% se lean.
%%   SE ELIMINA:
%%     - en dividir(), la cadena t1 -> t2 -> t3: t3 nunca se lee (tres
%%       pasadas del DCE: al quitar t3, t2 queda muerta, y luego t1).
%%     - en dividir(), "doble = a + a": nadie lo usa.
%%     - en guardar(), la COPIA del resultado de dividir(90, 3) en x1.
%%   NO SE ELIMINA:
%%     - en dividir(), q1 = 100 / b: nadie lo usa, pero divide entre una
%%       VARIABLE; si b fuera 0 el programa daria error, y quitarla
%%       cambiaria el comportamiento (regla de oro).
%%     - en guardar(), la LLAMADA dividir(90, 3): x1 no se lee, pero la
%%       llamada se conserva (se quita la asignacion, nunca la llamada).
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
    q1 * declare_int : 100 euclides b;
    t1 * declare_int : 5;
    t2 * declare_int : t1 gauss 1;
    t3 * declare_int : t2 gauss t2;
    give a euclides b;
}

create_funk #declare_int# guardar(v * declare_int[], i * declare_int, x * declare_int) {
    x1 * declare_int : dividir(90, 3);
    v[i] : x;
    give x;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[1] : [0];
    res * declare_int[4] : [0, 0, 0, 0];
    d * declare_int : 4;

    uno * declare_int : guardar(res, 1, 7);
    y * declare_int : guardar(res, 0, dividir(uno pitagoras 6, 2));
    res[2] : y gauss d;
    res[3] : dividir(guardar(res, 3, 50), 5);

    resultados[0] : y;
}
