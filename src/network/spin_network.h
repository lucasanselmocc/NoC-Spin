#pragma once
#include <systemc.h>
#include "router/router.h"

// ─────────────────────────────────────────────────────────────────────────────
//  SpinNetwork — árvore gorda quaternária com 8 roteadores RSPIN
//
//  Topologia (2 níveis):
//              R0  (raiz)
//           D0  D1  D2  D3
//           R1  R2  R3  R4   (intermediários)
//          D0  D0  D0
//          R5  R6  R7        (folhas)
//
//  Cada roteador tem terminal local na porta D3 (testbench).
//  Links internos usam D0 (filho→pai: Up) / D0..D2 (pai→filho: Down).
// ─────────────────────────────────────────────────────────────────────────────

SC_MODULE(SpinNetwork) {
    sc_in<bool> clk;
    sc_in<bool> rst;

    sc_in<bool>  ext_in_valid  [NUM_ROUTERS];
    sc_in<Flit>  ext_in_flit   [NUM_ROUTERS];
    sc_out<bool> ext_in_credit [NUM_ROUTERS];
    sc_out<bool> ext_out_valid [NUM_ROUTERS];
    sc_out<Flit> ext_out_flit  [NUM_ROUTERS];
    sc_in<bool>  ext_out_credit[NUM_ROUTERS];

    SC_CTOR(SpinNetwork) {
        // ── 1. Instanciar roteadores ─────────────────────────────────────────
        for (int i = 0; i < NUM_ROUTERS; ++i) {
            char name[8]; snprintf(name, sizeof(name), "r%d", i);
            r_[i] = new RSpin(name);
            r_[i]->router_id = i;
            r_[i]->clk(clk);
            r_[i]->rst(rst);
        }

        // ── 2. Conectar links internos entre roteadores ──────────────────────
        //   link(filho, porta_up, pai, porta_down)
        //   Usa D0 como porta-up dos filhos, e D0..D3 como portas-down do pai
        link(1, U0, 0, D0);   // R1.U0 ↔ R0.D0
        link(2, U0, 0, D1);   // R2.U0 ↔ R0.D1
        link(3, U0, 0, D2);   // R3.U0 ↔ R0.D2
        link(4, U0, 0, D3);   // R4.U0 ↔ R0.D3  (usa D3 do pai; terminal de R0 vai para U1)
        link(5, U0, 1, D0);   // R5.U0 ↔ R1.D0
        link(6, U0, 2, D0);   // R6.U0 ↔ R2.D0
        link(7, U0, 3, D0);   // R7.U0 ↔ R3.D0

        // ── 3. Conectar terminais externos ───────────────────────────────────
        // R0: terminal na porta U1 (D3 está ocupada com R4)
        // R1..R4: terminal na porta D1
        // R5..R7: terminal na porta D1 (folhas, D0 ocupado com link interno)
        // Para simplificar: todos usam a primeira porta livre disponível
        // Mapa de porta terminal por roteador:
        //   R0→U1, R1→D1, R2→D1, R3→D1, R4→D1, R5→D1, R6→D1, R7→D1
        bind_terminal(0, U1);
        bind_terminal(1, D1);
        bind_terminal(2, D1);
        bind_terminal(3, D1);
        bind_terminal(4, D1);
        bind_terminal(5, D1);
        bind_terminal(6, D1);
        bind_terminal(7, D1);

        // ── 4. Portas restantes → NC ─────────────────────────────────────────
        bind_all_nc();

        // ── 5. Tabelas de roteamento ─────────────────────────────────────────
        build_routing_tables();
    }

    ~SpinNetwork() { for (auto rr : r_) delete rr; }

private:
    RSpin* r_[NUM_ROUTERS];

    // Rastreia quais portas já foram conectadas (para NC)
    bool port_used_[NUM_ROUTERS][NUM_PORTS] = {};

    // Links internos: máx 7 links × 2 direções = 14 sinais
    static constexpr int MAX_LINKS = 16;
    sc_signal<bool> lv_[MAX_LINKS];   // valid
    sc_signal<Flit> lf_[MAX_LINKS];   // flit
    sc_signal<bool> lc_[MAX_LINKS];   // credit
    int lnk_ = 0;

    // NC sinks (uma por roteador/porta para saídas, globais para entradas)
    sc_signal<bool> nc_iv_{"nc_iv", false};
    sc_signal<Flit> nc_if_{"nc_if"};
    sc_signal<bool> nc_oc_{"nc_oc", true};   // out_credit=true → downstream sempre pronto
    // Saídas NC: precisam de sinal único por porta (sc_out não permite múltiplos drivers)
    sc_signal<bool> nc_ov_[NUM_ROUTERS][NUM_PORTS];
    sc_signal<Flit> nc_of_[NUM_ROUTERS][NUM_PORTS];
    sc_signal<bool> nc_ic_[NUM_ROUTERS][NUM_PORTS]; // in_credit NC

    // Conecta link bidirecional filho.up ↔ pai.dn
    void link(int filho, PortIdx up, int pai, PortIdx dn) {
        int la = lnk_++, lb = lnk_++;

        // filho.out[up] → pai.in[dn]
        r_[filho]->out_valid [up](lv_[la]);
        r_[filho]->out_flit  [up](lf_[la]);
        r_[filho]->out_credit[up](lc_[la]);
        r_[pai  ]->in_valid  [dn](lv_[la]);
        r_[pai  ]->in_flit   [dn](lf_[la]);
        r_[pai  ]->in_credit [dn](lc_[la]);

        // pai.out[dn] → filho.in[up]
        r_[pai  ]->out_valid [dn](lv_[lb]);
        r_[pai  ]->out_flit  [dn](lf_[lb]);
        r_[pai  ]->out_credit[dn](lc_[lb]);
        r_[filho]->in_valid  [up](lv_[lb]);
        r_[filho]->in_flit   [up](lf_[lb]);
        r_[filho]->in_credit [up](lc_[lb]);

        port_used_[filho][up] = true;
        port_used_[pai  ][dn] = true;
    }

    // Conecta terminal externo (testbench) a uma porta do roteador
    void bind_terminal(int rid, PortIdx p) {
        r_[rid]->in_valid [p](ext_in_valid [rid]);
        r_[rid]->in_flit  [p](ext_in_flit  [rid]);
        r_[rid]->in_credit[p](ext_in_credit[rid]);
        r_[rid]->out_valid[p](ext_out_valid[rid]);
        r_[rid]->out_flit [p](ext_out_flit [rid]);
        r_[rid]->out_credit[p](ext_out_credit[rid]);
        port_used_[rid][p] = true;
    }

    // Conecta todas as portas ainda não usadas a sinais NC
    void bind_all_nc() {
        for (int i = 0; i < NUM_ROUTERS; ++i) {
            for (int p = 0; p < NUM_PORTS; ++p) {
                if (port_used_[i][p]) continue;
                r_[i]->in_valid [p](nc_iv_);
                r_[i]->in_flit  [p](nc_if_);
                r_[i]->in_credit[p](nc_ic_[i][p]);
                r_[i]->out_valid[p](nc_ov_[i][p]);
                r_[i]->out_flit [p](nc_of_[i][p]);
                r_[i]->out_credit[p](nc_oc_);
            }
        }
    }

    // ── Tabelas de roteamento ─────────────────────────────────────────────────
    // porta terminal de cada roteador (onde o testbench injeta/recebe)
    // R0→U1, R1..R7→D1
    void build_routing_tables() {
        auto term = [](int id) -> int8_t {
            return (id == 0) ? (int8_t)U1 : (int8_t)D1;
        };

        // R0 (raiz): conhece tudo diretamente
        r_[0]->routing_table[0] = term(0);
        r_[0]->routing_table[1] = D0;
        r_[0]->routing_table[2] = D1;
        r_[0]->routing_table[3] = D2;
        r_[0]->routing_table[4] = D3;
        r_[0]->routing_table[5] = D0;  // R5 está abaixo de R1 (D0)
        r_[0]->routing_table[6] = D1;  // R6 abaixo de R2 (D1)
        r_[0]->routing_table[7] = D2;  // R7 abaixo de R3 (D2)

        // R1: filho esquerdo de R0
        r_[1]->routing_table[0] = U0;
        r_[1]->routing_table[1] = term(1);
        r_[1]->routing_table[2] = U0;
        r_[1]->routing_table[3] = U0;
        r_[1]->routing_table[4] = U0;
        r_[1]->routing_table[5] = D0;  // R5 é filho de R1
        r_[1]->routing_table[6] = U0;
        r_[1]->routing_table[7] = U0;

        // R2
        r_[2]->routing_table[0] = U0;
        r_[2]->routing_table[1] = U0;
        r_[2]->routing_table[2] = term(2);
        r_[2]->routing_table[3] = U0;
        r_[2]->routing_table[4] = U0;
        r_[2]->routing_table[5] = U0;
        r_[2]->routing_table[6] = D0;  // R6 é filho de R2
        r_[2]->routing_table[7] = U0;

        // R3
        r_[3]->routing_table[0] = U0;
        r_[3]->routing_table[1] = U0;
        r_[3]->routing_table[2] = U0;
        r_[3]->routing_table[3] = term(3);
        r_[3]->routing_table[4] = U0;
        r_[3]->routing_table[5] = U0;
        r_[3]->routing_table[6] = U0;
        r_[3]->routing_table[7] = D0;  // R7 é filho de R3

        // R4 (sem filhos roteadores)
        for (int d = 0; d < NUM_ROUTERS; ++d) r_[4]->routing_table[d] = U0;
        r_[4]->routing_table[4] = term(4);

        // R5, R6, R7 (folhas): tudo sobe
        for (int d = 0; d < NUM_ROUTERS; ++d) r_[5]->routing_table[d] = U0;
        r_[5]->routing_table[5] = term(5);

        for (int d = 0; d < NUM_ROUTERS; ++d) r_[6]->routing_table[d] = U0;
        r_[6]->routing_table[6] = term(6);

        for (int d = 0; d < NUM_ROUTERS; ++d) r_[7]->routing_table[d] = U0;
        r_[7]->routing_table[7] = term(7);
    }
};
