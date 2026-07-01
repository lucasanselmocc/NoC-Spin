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

// ─────────────────────────────────────────────────────────────────────────────
//  DualArbiter — árbitros separados para saídas Up e Down (spec SPIN)
//
//  Spec SPIN:
//    Árbitro das saídas SUPERIORES: atende requisições das FIFOs de entrada
//      inferiores (portas DN) que querem subir na árvore.
//    Árbitro das saídas INFERIORES: atende requisições das FIFOs de entrada
//      superiores (portas UP) E do buffer central (pacotes bloqueados).
//
//  Uso no RSpin:
//    up_arb  → escolhe qual flit de DN0-DN3 vai para qual saída Up
//    dn_arb  → escolhe qual flit de UP0-UP3 (ou buffer central) vai para Down
// ─────────────────────────────────────────────────────────────────────────────

// Árbitro para saídas superiores: N = NUM_DN_PORTS fontes (DN0-DN3)
using ArbiterUP = ArbiterN<NUM_DN_PORTS>;

// Árbitro para saídas inferiores: N = NUM_UP_PORTS + 1 fontes (UP0-UP3 + buffer central)
using ArbiterDN = ArbiterN<NUM_UP_PORTS + 1>;

// Alias legado usado em main.cpp (árbitro único para todos os 8 slots)
// using Arbiter8 = ArbiterN<NUM_PORTS>;
// using Arbiter4 = ArbiterN<4>;
