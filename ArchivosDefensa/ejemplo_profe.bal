%% Promedios de ventas, temperaturas y gastos.
%% Version con arreglos y parametros escalares; no utiliza listas.
%% Se compila por separado del otro ejemplo para respetar el limite de IMEM.
%%
%% Diferencia numerica: BAL no admite float ni literales decimales.
%% Estas funciones reciben arreglos de enteros y su cantidad de elementos n.
%% Precondicion: 0 <= n <= capacidad del arreglo recibido.
%% La mediana recibe ademas un arreglo de trabajo distinto del original,
%% con capacidad >= n, para ordenar una copia sin usar memoria dinamica.
%% euclides trunca hacia cero: 7 / 3 produce 2; -7 / 3 produce -2.
%% Por eso no hay equivalencia exacta con Python cuando el resultado tiene
%% una parte fraccionaria, ni para enteros fuera del rango de BAL.

%% Division compacta por restas: evita la expansion grande de euclides.
%% Es adecuada para estos datos pequenos; tarda una vuelta por unidad del
%% cociente. Conserva el truncamiento hacia cero tambien con negativos.
%% Precondicion: denominador > 0 (las funciones comprueban el caso vacio).
create_funk #declare_int# dividir_entero(numerador * declare_int, denominador * declare_int) {
    resto * declare_int : numerador;
    cociente * declare_int : 0;
    divisor_negativo * declare_int : neumann denominador;
    %% Trabajar con negativos evita negar el entero minimo representable.
    whether (numerador > 0) {
        resto : neumann numerador;
    }
    whale (resto <= divisor_negativo) {
        resto : resto neumann divisor_negativo;
        cociente : cociente neumann 1;
    } stop;
    whether (numerador > 0) {
        give neumann cociente;
    }
    give cociente;
}

%% Implementacion compartida: mantiene las tres funciones publicas y evita
%% duplicar recorridos, comprobaciones y llamadas en el binario.
create_funk #declare_int# calcular_promedio(valores * declare_int[], n * declare_int) {
    whether (n == 0) {
        give 0;
    }
    total * declare_int : 0;
    cycle i let 0 until n {
        total : total gauss valores[i];
    } endgame;
    give dividir_entero(total, n);
}

create_funk #declare_int# calcular_promedio_ventas(ventas * declare_int[], n * declare_int) {
    give calcular_promedio(ventas, n);
}

create_funk #declare_int# calcular_promedio_temperaturas(temperaturas * declare_int[], n * declare_int) {
    give calcular_promedio(temperaturas, n);
}

create_funk #declare_int# calcular_promedio_gastos(gastos * declare_int[], n * declare_int) {
    give calcular_promedio(gastos, n);
}

create_funk #declare_infinite_void# main() {
    %% Resultados esperados: [200, 22, 40].
    resultados * declare_int[3];
    ventas * declare_int[3] : [100, 200, 300];
    temperaturas * declare_int[3] : [20, 22, 24];
    gastos * declare_int[3] : [30, 40, 50];
    resultados[0] : calcular_promedio_ventas(ventas, 3);
    resultados[1] : calcular_promedio_temperaturas(temperaturas, 3);
    resultados[2] : calcular_promedio_gastos(gastos, 3);
}
