%% ============================================================================
%% CODIGO MUERTO 1: ASIGNACIONES MUERTAS ENTRE LLAMADAS CON EFECTOS
%% ----------------------------------------------------------------------------
%% registrar() y acumular() ESCRIBEN en el arreglo que reciben: tienen un
%% efecto aunque nadie use lo que devuelven. Entre las llamadas, en
%% preparar(), hay codigo muerto de verdad.
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
%%     - "registrar(v, 2, 30);" usada como sentencia.
%%     - acumular() lee datos y escribe datos[0]: el orden de las llamadas
%%       importa (si se adelantara, sumaria otros valores).
%%     - las variables de main: son el RESULTADO del programa (BAL no tiene
%%       print; se mira la memoria de main al terminar), aunque no se lean.
%%       Por eso el codigo muerto de esta prueba esta en preparar().
%%
%% Resultados esperados:
%%   datos = [60, 20, 30, 0, 0]   (acumular suma 10 + 20 + 30 y lo deja en [0])
%%   resultados[0] = r2 + total = 40 + 60 = 100
%%   resultados[1] = total = 60   (queda en datos[0])
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

%% Llena v con las llamadas y devuelve r2 + total. Todo lo que no se usa
%% aqui adentro es codigo muerto (las variables locales desaparecen al
%% terminar la funcion).
create_funk #declare_int# preparar(v * declare_int[]) {
    basura * declare_int : 7 gauss 6;
    r1 * declare_int : registrar(v, 0, 10);
    cadena1 * declare_int : basura gauss 1;
    cadena2 * declare_int : cadena1 gauss 2;
    r2 * declare_int : registrar(v, 1, 20);
    registrar(v, 2, 30);
    total * declare_int : acumular(v, 3);
    muerto * declare_int : total gauss 100;
    give r2 gauss total;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[2] : [0, 0];
    datos * declare_int[5] : [0, 0, 0, 0, 0];

    resultados[0] : preparar(datos);
    resultados[1] : datos[0];
}
