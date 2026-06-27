#pragma once
#include <systemc.h>
#include <queue>
#include "packet/flit.h"

// ─────────────────────────────────────────────────────────────────────────────
//  InputFIFO — buffer de 4 palavras por porta de entrada (spec SPIN)
//
//  Controle de fluxo por créditos:
//    push_credit (saída) → informa ao upstream quantas posições livres há
//    pop_en      (entrada) → árbitro/crossbar consome 1 flit por ciclo
// ─────────────────────────────────────────────────────────────────────────────
SC_MODULE(InputFIFO) {
    sc_in<bool>  clk;
    sc_in<bool>  rst;

    // Upstream (quem envia para esta porta)
    sc_in<bool>  push_valid;
    sc_in<Flit>  push_flit;
    sc_out<bool> push_credit; // 1 = há espaço (crédito disponível)

    // Para o árbitro / crossbar interno
    sc_out<bool> out_valid;
    sc_out<Flit> out_flit;
    sc_in<bool>  pop_en;      // 1 = árbitro consome o flit da cabeça

    SC_CTOR(InputFIFO) {
        SC_METHOD(update);
        sensitive << clk.pos();
        dont_initialize();
    }

    void update() {
        if (rst.read()) {
            while (!q_.empty()) q_.pop();
            push_credit.write(true);
            out_valid.write(false);
            out_flit.write(Flit{});
            return;
        }

        // Consome primeiro (libera crédito antes de verificar push)
        if (pop_en.read() && !q_.empty())
            q_.pop();

        // Insere se há crédito e dado válido
        if (push_valid.read() && push_credit.read())
            q_.push(push_flit.read());

        // Atualiza saídas
        push_credit.write((int)q_.size() < FIFO_DEPTH);
        out_valid.write(!q_.empty());
        if (!q_.empty())
            out_flit.write(q_.front());
        else
            out_flit.write(Flit{});
    }

private:
    std::queue<Flit> q_;
};
