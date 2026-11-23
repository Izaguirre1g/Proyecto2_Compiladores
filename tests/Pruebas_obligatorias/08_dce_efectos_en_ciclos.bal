%% ============================================================================
%% CODIGO MUERTO 2: CODIGO MUERTO E INALCANZABLE JUNTO A LLAMADAS EN CICLOS
%% ----------------------------------------------------------------------------
%% Los dos tipos de codigo muerto, mezclados con llamadas que tienen efecto:
%%   TIPO 1 (asignaciones que nadie usa):
%%     - en llenar(), "basura" y "sombra" se calculan en cada vuelta y nunca
%%       se leen; sombra depende de basura (al quitar sombra, basura queda
%%       muerta). Estan en una funcion porque las variables de main son el
%%       resultado del programa y se conservan aunque no se lean.
%%     - en marcar(), "viejo" lee el arreglo y nadie lo usa: una lectura de
%%       memoria sin efecto tambien se puede quitar.
%%   TIPO 2 (codigo inalcanzable, con plegado de constantes):
%%     - whether (1 > 2): la condicion siempre es falsa; la llamada de
%%       adentro nunca se ejecuta y SI se puede quitar.
%%     - whale (2 < 1): un ciclo que nunca da ni una vuelta.
%%   NO SE ELIMINA:
%%     - marcar() dentro del ciclo de llenar(): escribe en hist en cada vuelta.
%%     - "copia : marcar(hist, 4, 10)": su resultado se usa y ademas escribe.
%%
%% Resultados esperados:
%%   ciclo: hist[i] += i + 1  ->  hist = [1, 2, 3, 4, 5]
%%   copia = marcar(hist, 4, 10) -> hist[4] = 15, devuelve 15
%%   resultados[0] = hist[0] + hist[4] = 16
%%   resultados[1] = copia = 15
%%   resultados[2] = hist[1] + hist[2] + hist[3] = 9  (1000 nunca se suma)
%% ============================================================================
create_funk #declare_int# marcar(v * declare_int[], i * declare_int, valor * declare_int) {
    viejo * declare_int : v[i];
    v[i] : v[i] gauss valor;
    give v[i];
}

%% El ciclo que llena el histograma: hist[i] += i + 1.
create_funk #declare_int# llenar(v * declare_int[]) {
    basura * declare_int : 0;
    sombra * declare_int : 0;
    cycle i let 0 until 5 {
        basura : i gauss i;
        marcar(v, i, i gauss 1);
        sombra : basura gauss 1;
    } endgame;
    give 5;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[3] : [0, 0, 0];
    hist * declare_int[5] : [0, 0, 0, 0, 0];

    llenar(hist);

    whether (1 > 2) {
        marcar(hist, 0, 1000);
    }
    whale (2 < 1) {
        marcar(hist, 1, 1000);
    } stop;

    copia * declare_int : marcar(hist, 4, 10);

    resultados[0] : hist[0] gauss hist[4];
    resultados[1] : copia;
    resultados[2] : hist[1] gauss hist[2] gauss hist[3];
}
