#pragma once
#include <systemc.h>
#include <queue>
#include "packet/flit.h"

// ─────────────────────────────────────────────────────────────────────────────
//  CentralBuffer — buffer central de 18 palavras (spec SPIN, slides 9 e 10)
//
//  Armazena pacotes bloqueados destinados às portas de saída
//  INFERIORES (Down), evitando que uma FIFO de entrada inteira fique travada
//  por head-of-line blocking.
//
//  Interface:
//    push_valid / push_flit  → entrada de pacotes bloqueados (do roteador)
//    push_ready              → indica espaço disponível (< 18 flits)
//    pop_en                  → árbitro de saídas Down consome da cabeça
//    out_valid / out_flit    → cabeça do buffer para o árbitro/crossbar
// ─────────────────────────────────────────────────────────────────────────────
SC_MODULE(CentralBuffer) {
    sc_in<bool>  clk;
    sc_in<bool>  rst;

    // Do roteador → buffer central (pacote bloqueado)
    sc_in<bool>  push_valid;
    sc_in<Flit>  push_flit;
    sc_out<bool> push_ready;    // 1 = buffer não está cheio

    // Buffer central → árbitro de saídas Down
    sc_out<bool> out_valid;
    sc_out<Flit> out_flit;
    sc_in<bool>  pop_en;

    SC_CTOR(CentralBuffer) {
        SC_METHOD(update);
        sensitive << clk.pos();
        dont_initialize();
    }

    void update() {
        if (rst.read()) {
            while (!q_.empty()) q_.pop();
            push_ready.write(true);
            out_valid.write(false);
            out_flit.write(Flit{});
            return;
        }
        if (pop_en.read() && !q_.empty()) q_.pop();
        if (push_valid.read() && push_ready.read()) q_.push(push_flit.read());

        push_ready.write((int)q_.size() < CBUF_DEPTH);
        out_valid.write(!q_.empty());
        out_flit.write(q_.empty() ? Flit{} : q_.front());
    }

    int size() const { return (int)q_.size(); }

private:
    std::queue<Flit> q_;
};
