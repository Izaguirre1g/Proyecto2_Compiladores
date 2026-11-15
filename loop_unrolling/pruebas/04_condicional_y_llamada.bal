%% Loop unrolling 4: cuerpo con condicional (etiquetas internas) y llamada.
%% Cada copia recibe etiquetas y temporales nuevos; la llamada se conserva
%% en cada copia y en el mismo orden.
%% Resultado esperado: pares = 2 (i = 0 y 2), total = 1+2+3+4 = 10.
create_funk #declare_int# siguiente(x * declare_int) {
    give x gauss 1;
}

create_funk #declare_infinite_void# main() {
    pares * declare_int : 0;
    total * declare_int : 0;
    cycle i let 0 until 4 {
        whether (i euler 2 == 0) {
            pares : pares gauss 1;
        }
        total : total gauss siguiente(i);
    } endgame;
}
