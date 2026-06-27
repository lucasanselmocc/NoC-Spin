#pragma once
#include <systemc.h>
#include "packet/flit.h"
#include "router/input_fifo.h"
#include "router/arbiter.h"

// ─────────────────────────────────────────────────────────────────────────────
//  RSpin — roteador da rede SPIN
//
//  Topologia: árvore gorda QUATERNÁRIA
//    8 portas: D0-D3 (inferiores, para filhos/terminais)
//              U0-U3 (superiores, para o pai)
//
//  Roteamento (spec SPIN):
//    Pacote vindo de porta INFERIOR (Di):
//      → pode sair por porta INFERIOR diferente (determinístico, se destino é filho)
//      → pode sair por porta SUPERIOR (adaptativo, sobe na árvore)
//    Pacote vindo de porta SUPERIOR (Ui):
//      → DEVE sair por porta INFERIOR (determinístico, desce na árvore)
//
//  Algoritmo implementado: tabela de roteamento preenchida pela rede
//    routing_table[dst] = porta de saída (0-7)
// ─────────────────────────────────────────────────────────────────────────────
SC_MODULE(RSpin) {
    sc_in<bool>  clk;
    sc_in<bool>  rst;

    uint8_t router_id;
    uint8_t level;        // 0 = raiz, 1 = intermediário, 2 = folha

    // ── Portas de entrada (8 portas) ─────────────────────────────────────────
    sc_in<bool>  in_valid [NUM_PORTS];
    sc_in<Flit>  in_flit  [NUM_PORTS];
    sc_out<bool> in_credit[NUM_PORTS];  // crédito de volta ao upstream

    // ── Portas de saída (8 portas) ───────────────────────────────────────────
    sc_out<bool> out_valid [NUM_PORTS];
    sc_out<Flit> out_flit  [NUM_PORTS];
    sc_in<bool>  out_credit[NUM_PORTS]; // crédito do downstream

    // ── Tabela de roteamento: dst_id → porta de saída ─────────────────────────
    // Preenchida pela SpinNetwork no momento da instanciação
    int8_t routing_table[NUM_ROUTERS];  // -1 = não alcançável

    SC_CTOR(RSpin) {
        // Inicializar tabela
        for (int i = 0; i < NUM_ROUTERS; ++i) routing_table[i] = -1;

        // Instanciar 8 FIFOs de entrada
        for (int p = 0; p < NUM_PORTS; ++p) {
            char name[16];
            snprintf(name, sizeof(name), "fifo_%d", p);
            fifos_[p] = new InputFIFO(name);
            fifos_[p]->clk(clk);
            fifos_[p]->rst(rst);
            fifos_[p]->push_valid(in_valid[p]);
            fifos_[p]->push_flit(in_flit[p]);
            fifos_[p]->push_credit(in_credit[p]);
            fifos_[p]->out_valid(fifo_valid_[p]);
            fifos_[p]->out_flit(fifo_flit_[p]);
            fifos_[p]->pop_en(fifo_pop_[p]);
        }

        // Árbitro único round-robin sobre todas as 8 entradas
        arb_ = new Arbiter8("arbiter");
        arb_->clk(clk);
        arb_->rst(rst);
        for (int p = 0; p < NUM_PORTS; ++p)
            arb_->req[p](fifo_valid_[p]);
        arb_->grant(grant_idx_);
        arb_->grant_valid(grant_valid_);

        SC_METHOD(route_and_forward);
        sensitive << clk.pos();
        dont_initialize();
    }

    ~RSpin() {
        for (auto f : fifos_) delete f;
        delete arb_;
    }

private:
    InputFIFO* fifos_[NUM_PORTS];
    Arbiter8*  arb_;

    sc_signal<bool> fifo_valid_[NUM_PORTS];
    sc_signal<Flit> fifo_flit_ [NUM_PORTS];
    sc_signal<bool> fifo_pop_  [NUM_PORTS];
    sc_signal<int>  grant_idx_;
    sc_signal<bool> grant_valid_;

    void route_and_forward() {
        // Limpa saídas
        for (int p = 0; p < NUM_PORTS; ++p) {
            out_valid[p].write(false);
            fifo_pop_[p].write(false);
        }

        if (!grant_valid_.read()) return;

        int src_port = grant_idx_.read();
        if (src_port < 0 || src_port >= NUM_PORTS) return;

        Flit flit = fifo_flit_[src_port].read();

        // Determina porta de saída
        int dst_port = -1;

        if (flit.type == HEADER) {
            uint8_t dst = flit.dst_id();
            if (dst == router_id) {
                // Entrega local: usa porta D0 como LOCAL (terminal conectado a D0)
                // Na implementação com terminais reais, seria porta LOCAL separada
                dst_port = D0;
            } else {
                dst_port = routing_table[dst];
            }

            // Validação das regras de roteamento SPIN:
            // Pacote vindo de UP (Ui) SÓ pode descer (sair por Di)
            if (is_up_port(src_port) && dst_port >= 0 && is_up_port(dst_port)) {
                // Violação! Pacote descendo não pode subir de novo
                // Drop do pacote (na prática usaria buffer central)
                return;
            }
        } else {
            // BODY/TAIL: mantém o mesmo canal de saída do HEADER
            // (wormhole: uma vez aberto o caminho, todos os flits seguem)
            // Simplificação: reusa a tabela com o src como chave de estado
            // Em implementação completa, guardaria estado por VC (virtual channel)
            dst_port = routing_table[flit.src_id];
        }

        if (dst_port < 0 || dst_port >= NUM_PORTS) return;

        // Só envia se downstream tem crédito
        if (out_credit[dst_port].read()) {
            out_valid[dst_port].write(true);
            out_flit [dst_port].write(flit);
            fifo_pop_[src_port].write(true);
        }
    }
};
