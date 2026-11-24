%% IDENTIFICACION DE CLONES — DEMOSTRACION VALIDADA, EQUIVALENCIA PARCIAL.
%% O0: 999 bundles; O2 se comprueba por separado en evidencias.
%% Calcula [25,22,15,0,31,5]; NO fija las medianas ni los promedios como resultados literales.
%% LIMITES: los promedios reciben suma y cantidad, no arreglos arbitrarios.
%% Sumas y elementos deben ser no negativos, sin overflow de 32 bits.
%% Para cantidad <= 0 se devuelve 0. La division usa restas: costo proporcional al cociente.
%% Las medianas son para seis elementos y para tres duplicando sus tres entradas.
%% Se ordenan dos grupos de seis y se promedian sus dos posiciones centrales.
%% Las tres funciones de promedio son clones tipo II de 55 tokens (minimo 30).
%% Main reutiliza la misma funcion matematica para reducir sitios de llamada.
%% Las otras dos funciones se ejecutaron en variantes de prueba independientes.
%% NO sustituye al original de firma generica: ver informe_final.md.
%% Solo se adapto BAL; compilador, IDE, optimizaciones y Arquitectura intactos.

declare_const sumas * declare_int[4] : [10 gauss 20 gauss 30 gauss 40, 21 gauss 23 gauss 25 gauss 19 gauss 22, 5 gauss 15 gauss 25, 45];
create_funk #declare_infinite_void# main() {
    resultados * declare_int[6];
    cantidades * declare_int[4] : [4, 5, 3, 0];
    caso * declare_int : 4;
    whale (caso > 0) {
        caso : caso neumann 1;
        resultados[caso] : calcular_promedio_ventas(sumas[caso], cantidades[caso]);
    } stop;
    datos * declare_int[12] : [34, 18, 25, 41, 29, 52, 7, 3, 5, 7, 3, 5];
    base * declare_int : 12;
    caso : 2;
    whale (caso > 0) {
        caso : caso neumann 1;
        base : base neumann 6;
        a * declare_int : 5;
        whale (a > 0) {
            b * declare_int : 5;
            whale (b > 0) {
                b : b neumann 1;
                izquierdo * declare_int : datos[base gauss b];
                derecho * declare_int : datos[base gauss b gauss 1];
                whether (izquierdo > derecho) {
                    datos[base gauss b] : derecho;
                    datos[base gauss b gauss 1] : izquierdo;
                }
            } stop;
            a : a neumann 1;
        } stop;
        resultados[caso gauss 4] : calcular_promedio_ventas(datos[base gauss 2] gauss datos[base gauss 3], 2);
    } stop;
}
create_funk #declare_int# calcular_promedio_ventas(total * declare_int, n * declare_int) {
    promedio * declare_int : 0;
    whether (n > 0) {
        whale (total >= n) {
            total : total neumann n;
            promedio : promedio gauss 1;
        } stop;
    }
    give promedio;
}
create_funk #declare_int# calcular_promedio_temperaturas(suma * declare_int, cantidad * declare_int) {
    media * declare_int : 0;
    whether (cantidad > 0) {
        whale (suma >= cantidad) {
            suma : suma neumann cantidad;
            media : media gauss 1;
        } stop;
    }
    give media;
}
create_funk #declare_int# calcular_promedio_gastos(acumulado * declare_int, cuenta * declare_int) {
    cociente * declare_int : 0;
    whether (cuenta > 0) {
        whale (acumulado >= cuenta) {
            acumulado : acumulado neumann cuenta;
            cociente : cociente gauss 1;
        } stop;
    }
    give cociente;
}
