/* Sonda de los decodificadores RTL originales para contrastar S2.
 * Solo aplica palabras y publica señales combinacionales; no ejecuta bundles. */
`timescale 1ns/1ps
module tb_sim_decode_ref;
    logic [31:0] word;
    logic auth, init;
    logic [3:0] aop, lop, bop, cop;
    logic [4:0] ard, ars1, ars2, lrd, lrs1, lrs2, brd, brs1, brs2;
    logic [4:0] crd, crsa, crsb, cimm;
    logic [10:0] aimm, limm, bimm;
    logic [15:0] jimm, addr;
    logic [1:0] lk, rk;
    logic ause, lwe, lvalid, isbranch, isjump, bwe, cwe, cvalid, fault;

    /* Una palabra alimenta las cuatro unidades. El consumidor del registro
     * elige las señales del slot indicado por el vector, sin alterar el RTL. */
    decoder_alu alu(.instruction(word), .alu_op(aop), .rd(ard), .rs1(ars1),
        .rs2(ars2), .imm(aimm), .use_imm(ause));
    decoder_lsu lsu(.instruction(word), .lsu_op(lop), .rd(lrd), .rs1(lrs1),
        .rs2(lrs2), .imm(limm), .we(lwe), .valid(lvalid));
    decoder_bru bru(.instruction(word), .bru_op(bop), .rd(brd), .rs1(brs1),
        .rs2(brs2), .br_imm(bimm), .jmp_imm(jimm), .is_branch(isbranch),
        .is_jump(isjump), .we(bwe));
    decoder_crypto crypto(.instruction(word), .auth(auth), .init(init),
        .crypto_op(cop), .lk(lk), .rk(rk), .rd(crd), .rs_a(crsa), .rs_b(crsb),
        .addr(addr), .imm(cimm), .we(cwe), .valid(cvalid), .priv_fault(fault));

    string ruta;
    integer archivo, leidos, slot, a, i;
    initial begin
        if (!$value$plusargs("VECTORES=%s", ruta)) $fatal(1, "Falta VECTORES");
        archivo = $fopen(ruta, "r");
        if (!archivo) $fatal(1, "No se pudo abrir el archivo de vectores");
        while (!$feof(archivo)) begin
            /* Cada fila contiene slot, palabra hexadecimal, AUTH e INIT.
             * Esperar una unidad permite estabilizar los always combinacionales. */
            leidos = $fscanf(archivo, "%d %h %d %d\n", slot, word, a, i);
            if (leidos == 4) begin
                auth = a != 0;
                init = i != 0;
                #1;
                /* Se publica un registro por vector. No se normalizan salidas
                 * no utilizadas: Python compara solo los operandos ISA y
                 * documenta los cables extra que RTL puede mantener activos. */
                $write("REF %0d %08h %0d %0d", slot, word, auth, init);
                $write(" %0d %0d %0d %0d %0d %0d", aop, ard, ars1, ars2, aimm, ause);
                $write(" %0d %0d %0d %0d %0d %0d %0d", lop, lrd, lrs1, lrs2, limm, lwe, lvalid);
                $write(" %0d %0d %0d %0d %0d %0d %0d %0d %0d", bop, brd, brs1, brs2,
                    bimm, jimm, isbranch, isjump, bwe);
                $write(" %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d", cop, lk, rk,
                    crd, crsa, crsb, addr, cimm, cwe, cvalid, fault);
                $display("");
            end else if (!$feof(archivo)) $fatal(1, "Vector incompleto");
        end
        $fclose(archivo);
        $finish;
    end
endmodule
