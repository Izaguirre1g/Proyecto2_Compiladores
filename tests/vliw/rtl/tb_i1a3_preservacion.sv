// Contrasta llamadas del backend tradicional con s0–s15 inicialmente no cero.
// Reutiliza el banco público de Arquitectura: este conserva sus watchdogs,
// medición y protocolo de vaciado. Solo aportamos la precondición de registros,
// después de reset y antes del primer flanco activo. Los dumps son externos.
module tb_i1a3_preservacion;
    parameter integer MEM_MODE=0;
    tb_assembled_program #(.MEM_MODE(MEM_MODE),.L2_MODEL(0)) banco();
    initial begin
        wait(banco.reset === 1'b0);
        #1;
        // Los centinelas permiten detectar destrucción aunque el resultado BAL
        // fuera correcto. Este caso no ejecuta un callee que use el pool de Dylan.
        for(integer r=16;r<32;r=r+1)
            banco.DUT.REGFILE.regs[r]=32'h4a560000+r;
    end
endmodule
