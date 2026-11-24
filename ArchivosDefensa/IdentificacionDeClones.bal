%% ============================================================================
%% IDENTIFICACION DE CLONES (traduccion de IdentificacionDeClones.py)
%% ----------------------------------------------------------------------------
%% Cuatro funciones; tres de ellas calculan un promedio con el MISMO codigo:
%%   - calcular_promedio_ventas y calcular_promedio_gastos tienen el mismo
%%     cuerpo (total, contador, promedio, v); solo cambian el nombre de la
%%     funcion y del parametro (ventas / gastos).
%%   - calcular_promedio_temperaturas hace lo mismo pero cambia ademas los
%%     nombres de las variables (suma, cantidad, media, t).
%%   - calcular_mediana_edades es distinta: no es clon.
%% Como en las tres cambian nombres, el detector las reporta juntas como UN
%% clon TIPO II de 3 copias (mismo camino en la gramatica, otros nombres):
%%     ./balc --clones ArchivosDefensa/IdentificacionDeClones.bal
%% (o el boton "Buscar clones" del IDE). Las sentencias de main tambien se
%% parecen entre si; con "--minimo 30" solo queda el clon de las funciones.
%%
%% OJO: este archivo es para la DETECCION DE CLONES (analiza el codigo, no lo
%% ejecuta). Completo no cabe en la memoria de instrucciones (1024 bundles)
%% en -O0: cada division (euclides) ocupa unos 90 bundles. Cada funcion se
%% comprobo por separado en el simulador con los resultados de abajo.
%%
%% Diferencias con Python (BAL no tiene listas dinamicas ni decimales):
%%   - len(lista) no existe: cada funcion recibe el arreglo y su largo "n".
%%   - "/" de Python da decimales; aqui euclides es la division ENTERA.
%%   - Cada funcion tiene un solo give al final (el "return 0" del caso
%%     vacio se hace dejando el resultado en 0).
%%   - sorted() no existe: la mediana ordena con burbuja. Como los arreglos
%%     se pasan por referencia, ordena el arreglo que recibe.
%%
%% Resultados esperados (resultados[k]):
%%   [0] promedio de ventas [10, 20, 30, 40]            = 100 / 4 = 25
%%   [1] promedio de temperaturas [21, 23, 25, 19, 22]  = 110 / 5 = 22
%%   [2] promedio de gastos [5, 15, 25]                 = 45 / 3  = 15
%%   [3] promedio de gastos con 0 elementos             = 0
%%   [4] mediana de edades [34, 18, 25, 41, 29, 52] (par)
%%       ordenadas [18, 25, 29, 34, 41, 52] -> (29 + 34) / 2 = 31
%%       (Python daria 31.5; en BAL la division es entera)
%%   [5] mediana de [7, 3, 5] (impar) -> ordenadas [3, 5, 7] -> 5
%% ============================================================================
create_funk #declare_int# calcular_promedio_ventas(ventas * declare_int[], n * declare_int) {
    total * declare_int : 0;
    contador * declare_int : 0;
    cycle v let 0 until n {
        total : total gauss ventas[v];
        contador : contador gauss 1;
    } endgame;
    promedio * declare_int : 0;
    whether (contador =/= 0) {
        promedio : total euclides contador;
    }
    give promedio;
}

create_funk #declare_int# calcular_promedio_temperaturas(temperaturas * declare_int[], n * declare_int) {
    suma * declare_int : 0;
    cantidad * declare_int : 0;
    cycle t let 0 until n {
        suma : suma gauss temperaturas[t];
        cantidad : cantidad gauss 1;
    } endgame;
    media * declare_int : 0;
    whether (cantidad =/= 0) {
        media : suma euclides cantidad;
    }
    give media;
}

create_funk #declare_int# calcular_promedio_gastos(gastos * declare_int[], n * declare_int) {
    total * declare_int : 0;
    contador * declare_int : 0;
    cycle v let 0 until n {
        total : total gauss gastos[v];
        contador : contador gauss 1;
    } endgame;
    promedio * declare_int : 0;
    whether (contador =/= 0) {
        promedio : total euclides contador;
    }
    give promedio;
}

create_funk #declare_int# calcular_mediana_edades(datos * declare_int[], n * declare_int) {
    %% sorted(edades): burbuja sobre el mismo arreglo.
    temporal * declare_int : 0;
    limite * declare_int : 0;
    cycle a let 0 until n neumann 1 {
        limite : n neumann 1 neumann a;
        cycle b let 0 until limite {
            whether (datos[b] > datos[b gauss 1]) {
                temporal : datos[b];
                datos[b] : datos[b gauss 1];
                datos[b gauss 1] : temporal;
            }
        } endgame;
    } endgame;

    mediana * declare_int : 0;
    whether (n =/= 0) {
        medio * declare_int : n euclides 2;
        whether (n euler 2 == 0) {
            mediana : (datos[medio neumann 1] gauss datos[medio]) euclides 2;
        }
        also {
            mediana : datos[medio];
        }
    }
    give mediana;
}

create_funk #declare_infinite_void# main() {
    resultados * declare_int[6] : [0, 0, 0, 0, 0, 0];
    ventas * declare_int[4] : [10, 20, 30, 40];
    temperaturas * declare_int[5] : [21, 23, 25, 19, 22];
    gastos * declare_int[3] : [5, 15, 25];
    edades * declare_int[6] : [34, 18, 25, 41, 29, 52];
    impares * declare_int[3] : [7, 3, 5];

    resultados[0] : calcular_promedio_ventas(ventas, 4);
    resultados[1] : calcular_promedio_temperaturas(temperaturas, 5);
    resultados[2] : calcular_promedio_gastos(gastos, 3);
    resultados[3] : calcular_promedio_gastos(gastos, 0);
    resultados[4] : calcular_mediana_edades(edades, 6);
    resultados[5] : calcular_mediana_edades(impares, 3);
}
