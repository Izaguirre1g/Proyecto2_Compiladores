%% ============================================================================
%% LOOP UNROLLING (traduccion de LoopUnrolling.py)
%% ----------------------------------------------------------------------------
%% Recorre varios bloques de numeros y suma el doble de cada uno.
%%
%% Diferencias con Python:
%%   - BAL no tiene listas de listas de distinto largo. Los bloques van
%%     seguidos en UN arreglo ("bloques") y otro arreglo dice cuantos
%%     elementos tiene cada bloque ("largos"):
%%         [[1, 2, 3], [4, 5], [6, 7, 8, 9]]
%%      -> bloques = [1, 2, 3, 4, 5, 6, 7, 8, 9], largos = [3, 2, 4]
%%     "inicio" es donde empieza el bloque actual dentro de "bloques".
%%   - "for bloque in bloques" es un cycle sobre la cantidad de bloques, y
%%     len(bloque) es largos[b].
%%   - El while interno se conserva como whale. Su limite (largos[b])
%%     cambia en cada bloque: no se conoce al compilar, asi que el
%%     unrolling solo puede desenrollarlo PARCIAL (grupos + residuo).
%%
%% Resultado esperado:
%%   total = 2 x (1 + 2 + ... + 9) = 2 x 45 = 90
%% ============================================================================
create_funk #declare_int# procesar_bloques(bloques * declare_int[], largos * declare_int[], cantidad * declare_int) {
    total * declare_int : 0;
    inicio * declare_int : 0;
    i * declare_int : 0;

    cycle b let 0 until cantidad {
        i : 0;
        whale (i < largos[b]) {
            total : total gauss (bloques[inicio gauss i] pitagoras 2);
            i : i gauss 1;
        } stop;
        inicio : inicio gauss largos[b];
    } endgame;

    give total;
}

create_funk #declare_infinite_void# main() {
    bloques * declare_int[9] : [1, 2, 3, 4, 5, 6, 7, 8, 9];
    largos * declare_int[3] : [3, 2, 4];
    total * declare_int : procesar_bloques(bloques, largos, 3);
}
