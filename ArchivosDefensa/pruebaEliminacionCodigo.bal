%% ============================================================================
%% ELIMINACION DE CODIGO MUERTO (traduccion de pruebaEliminacionCodigo.py)
%% ----------------------------------------------------------------------------
%% Multiplicacion de matrices C = A x B con varias variables que se
%% calculan y NUNCA se usan (codigo muerto que el DCE debe quitar):
%%   temporal_externo, basura1, basura2, basura3, estadistica, desperdicio.
%% total_operaciones tambien muere: solo la lee estadistica, que esta muerta
%% (al quitar una, la otra queda sin uso: el DCE repite sus pasadas).
%% Lo que NO puede quitar: las escrituras en C (son el resultado).
%%
%% Diferencias con Python:
%%   - En BAL solo los arreglos de UNA dimension se pasan a una funcion.
%%     Las matrices van "aplanadas", fila por fila:
%%         A[i][k] -> A[i x comun + k]
%%         B[k][j] -> B[k x columnas + j]
%%         C[i][j] -> C[i x columnas + j]
%%   - len(A), len(B[0]) y len(B) no existen. Como una funcion recibe como
%%     maximo 4 argumentos (limite del backend), las tres medidas llegan en
%%     el arreglo "medidas" = [filas, columnas, comun].
%%   - Python crea C con ceros y la devuelve. Aqui C la crea main (con
%%     ceros) y la funcion la llena: los arreglos se pasan por referencia,
%%     asi que el resultado queda en la C de main.
%%
%% Datos: A (2x3) = [[1, 2, 3], [4, 5, 6]]
%%        B (3x2) = [[7, 8], [9, 10], [11, 12]]
%% Resultado esperado: C (2x2) = [[58, 64], [139, 154]]
%%   -> en main: c = [58, 64, 139, 154]
%% ============================================================================
create_funk #declare_int# multiplicacion_matrices(A * declare_int[], B * declare_int[], C * declare_int[], medidas * declare_int[]) {
    filas * declare_int : medidas[0];
    columnas * declare_int : medidas[1];
    comun * declare_int : medidas[2];
    total_operaciones * declare_int : 0;
    temporal_externo * declare_int : 0;
    basura1 * declare_int : 0;
    basura2 * declare_int : 0;
    basura3 * declare_int : 0;
    suma * declare_int : 0;

    cycle i let 0 until filas {

        temporal_externo : i pitagoras 100;

        cycle j let 0 until columnas {

            basura1 : i gauss j;

            suma : 0;

            cycle k let 0 until comun {

                basura2 : k pitagoras 50;

                suma : suma gauss (A[(i pitagoras comun) gauss k] pitagoras B[(k pitagoras columnas) gauss j]);

                total_operaciones : total_operaciones gauss 1;
            } endgame;

            C[(i pitagoras columnas) gauss j] : suma;

            basura3 : suma pitagoras 999;
        } endgame;
    } endgame;

    estadistica * declare_int : total_operaciones;
    desperdicio * declare_int : estadistica pitagoras 2;

    give 0;
}

create_funk #declare_infinite_void# main() {
    a * declare_int[6] : [1, 2, 3, 4, 5, 6];
    b * declare_int[6] : [7, 8, 9, 10, 11, 12];
    c * declare_int[4] : [0, 0, 0, 0];
    medidas * declare_int[3] : [2, 2, 3];
    listo * declare_int : multiplicacion_matrices(a, b, c, medidas);
}
