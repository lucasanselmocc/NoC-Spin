#pragma once
#include <systemc.h>
#include "packet/flit.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Arbiter — árbitro round-robin para N portas (parametrizável)
//
//  Spec SPIN: árbitro de um nível (round-robin) para canais de saída.
//  Um árbitro por grupo de saída (superiores e inferiores são separados).
// ─────────────────────────────────────────────────────────────────────────────
template<int N>
SC_MODULE(ArbiterN) {
    sc_in<bool>  clk;
    sc_in<bool>  rst;

    sc_in<bool>  req[N];         // requisição de cada porta de entrada
    sc_out<int>  grant;          // índice da porta vencedora (-1 = nenhuma)
    sc_out<bool> grant_valid;

    SC_CTOR(ArbiterN) : last_(0) {
        SC_METHOD(arbitrate);
        sensitive << clk.pos();
        dont_initialize();
    }

    void arbitrate() {
        if (rst.read()) {
            last_ = 0;
            grant.write(-1);
            grant_valid.write(false);
            return;
        }
        for (int i = 1; i <= N; ++i) {
            int c = (last_ + i) % N;
            if (req[c].read()) {
                grant.write(c);
                grant_valid.write(true);
                last_ = c;
                return;
            }
        }
        grant.write(-1);
        grant_valid.write(false);
    }

private:
    int last_;
};

// Instâncias usadas no RSPIN
using Arbiter4 = ArbiterN<4>;
using Arbiter8 = ArbiterN<8>;
