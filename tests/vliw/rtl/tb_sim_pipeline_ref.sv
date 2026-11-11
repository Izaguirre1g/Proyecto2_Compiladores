/* Referencia ejecutable de S3: observa top y la ALU originales sin alterar RTL.
 * Se muestrean señales antes del flanco y registros después de sus NBA. */
`timescale 1ns/1ps
module tb_sim_pipeline_ref;
    logic clk = 0, reset = 1;
    wire [31:0] debug_result;
    wire [4:0] debug_rd;
    wire debug_write;
    top DUT(.clk(clk), .reset(reset), .debug_result(debug_result),
            .debug_rd(debug_rd), .debug_write(debug_write));

    logic [3:0] operacion;
    logic [31:0] a, b;
    wire [31:0] resultado;
    alu REFERENCIA(.alu_op(operacion), .operand_a(a), .operand_b(b), .result(resultado));

    // EX/WB no conserva PC en el RTL. Esta sonda acompaña su captura solo
    // para identificar el bundle observado en WB; no realimenta el procesador.
    logic [31:0] wb_pc = 0;
    always @(posedge clk) begin
        // La sonda acompaña el PC de EX hacia el contexto WB con asignación no bloqueante. El
        // reset la limpia igual que el registro observado; no altera validez ni control del
        // procesador.
        if (reset) wb_pc <= 0;
        else wb_pc <= DUT.ex_pc;
    end

    integer archivo, leidos, indice, codigo, ciclos, reset_despues = 0, ignorado;
    string ruta, previo;
    // El modo ALU contrasta resultados combinacionales; el modo pipeline observa capturas ID y
    // publicación WB sin realimentar el núcleo.
    initial begin
        if ($test$plusargs("ALU")) begin
            if (!$value$plusargs("VECTORES=%s", ruta)) $fatal(1, "falta VECTORES");
            archivo = $fopen(ruta, "r");
            if (!archivo) $fatal(1, "no se pueden abrir los vectores");
            // El código interno pertenece al RTL; índice identifica una de las
            // 18 operaciones ISA en el ejecutable C que lee los mismos datos.
            while (!$feof(archivo)) begin
                leidos = $fscanf(archivo, "%d %d %h %h\n", indice, codigo, a, b);
                if (leidos == 4) begin
                    operacion = codigo;
                    #1;
                    $display("ALU %0d %08h %08h %08h", indice, a, b, resultado);
                end else if (!$feof(archivo)) $fatal(1, "vector ALU inválido");
            end
            $fclose(archivo);
        end else begin
            if (!$value$plusargs("CICLOS=%d", ciclos)) $fatal(1, "falta CICLOS");
            ignorado = $value$plusargs("RESET_DESPUES=%d", reset_despues);
            // Dos flancos de reset eliminan cualquier dependencia del estado
            // inicial de las memorias de registros; no son ciclos de ejecución.
            #2; clk = 1; #2; clk = 0;
            #2; clk = 1; #2; clk = 0; reset = 0; #2;
            for (integer c = 1; c <= ciclos; c = c + 1) begin
                // El reset intermedio elimina incluso un resultado pendiente en
                // EX/WB. No cuenta como ejecución y reinicia la numeración C.
                if (reset_despues > 0 && c == reset_despues + 1) begin
                    reset = 1; #1; clk = 1; #1; clk = 0;
                    reset = 0; #2;
                end
                // No se muestrea ID después de WB: hacerlo ocultaría la lectura
                // antigua del consumidor N+2 en el mismo flanco del productor.
                previo = $sformatf("PIPE %0d %08h %0d %0d %08h %0d %08h %0d %08h %0d %0d %0d %03h %08h %08h %08h %08h %08h %0d %0d %08h",
                    (reset_despues > 0 && c > reset_despues ? c-reset_despues : c), DUT.pc, DUT.fetch_valid, DUT.id_valid, DUT.id_pc,
                    DUT.id_ex_valid, DUT.ex_pc, DUT.ex_wb_valid, wb_pc,
                    DUT.alu_rs1, DUT.alu_rs2, DUT.alu_use_imm, DUT.alu_imm,
                    DUT.alu_rs1_data, DUT.alu_rs2_data, DUT.ex_operand_a,
                    DUT.ex_operand_b, DUT.alu_result, DUT.wb_we[0],
                    DUT.wb_waddr[0], DUT.wb_wdata[0]);
                // La traza previo ya contiene operandos antes de WB; después del flanco y las
                // NBA comparamos PC y los 32 registros actualizados.
                #1; clk = 1; #1;
                $write("%s %08h", previo, DUT.pc);
                // Los 32 registros posteriores comprueban también la ausencia
                // de escrituras indebidas, no únicamente el registro esperado.
                for (integer r = 0; r < 32; r = r + 1)
                    $write(" %08h", DUT.REGFILE.regs[r]);
                $write("\n");
                clk = 0; #2;
            end
        end
        $finish;
    end
endmodule
