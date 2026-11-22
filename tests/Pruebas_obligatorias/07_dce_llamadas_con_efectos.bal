%% ============================================================================
%% CODIGO MUERTO 1: ASIGNACIONES MUERTAS ENTRE LLAMADAS CON EFECTOS
%% ----------------------------------------------------------------------------
%% registrar() y acumular() ESCRIBEN en el arreglo que reciben: tienen un
%% efecto aunque nadie use lo que devuelven. Entre las llamadas hay codigo
%% muerto de verdad.
%%   SE ELIMINA (nadie lo lee y no tiene efectos):
%%     - basura, muerto: valores que nunca se leen.
%%     - cadena1 -> cadena2: cadena2 no se lee; al quitarla, cadena1 queda
%%       muerta tambien (se ve en la SEGUNDA pasada del DCE).
%%     - en registrar(), "copia" (nunca se lee) y en acumular(), "sobra".
%%   NO SE ELIMINA (regla de oro: el programa debe hacer lo mismo):
%%     - en acumular(), "promedio = s / n": nadie lo usa, pero divide entre
%%       una VARIABLE (si n fuera 0 el programa daria error): se conserva.
%%     - "r1 : registrar(...)": r1 nunca se lee, pero la LLAMADA escribe
%%       datos[0]. Se quita la asignacion, nunca la llamada.
%%     - "registrar(datos, 2, 30);" usada como sentencia.
%%     - acumular() lee datos y escribe datos[0]: el orden de las llamadas
%%       importa (si se adelantara, sumaria otros valores).
%%
%% Resultados esperados:
%%   datos = [60, 20, 30, 0, 0]   (acumular suma 10 + 20 + 30 y lo deja en [0])
%%   resultados[0] = r2 + total = 40 + 60 = 100
%%   resultados[1] = total = 60
%% ============================================================================
create_funk #declare_int# registrar(v * declare_int[], pos * declare_int, valor * declare_int) {
    copia * declare_int : valor gauss pos;
    v[pos] : valor;
    give valor pitagoras 2;
}

create_funk #declare_int# acumular(v * declare_int[], n * declare_int) {
    s * declare_int : 0;
    cycle i let 0 until n {
        s : s gauss v[i];
    } endgame;
    promedio * declare_int : s euclides n;
    sobra * declare_int : s gauss 1;
    v[0] : s;
    give s;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[2] : [0, 0];
    datos * declare_int[5] : [0, 0, 0, 0, 0];

    basura * declare_int : 7 pitagoras 6;
    r1 * declare_int : registrar(datos, 0, 10);
    cadena1 * declare_int : basura gauss 1;
    cadena2 * declare_int : cadena1 pitagoras 2;
    r2 * declare_int : registrar(datos, 1, 20);
    registrar(datos, 2, 30);
    total * declare_int : acumular(datos, 3);
    muerto * declare_int : total pitagoras 100;

    resultados[0] : r2 gauss total;
    resultados[1] : total;
}
