#pragma once
#include <systemc.h>
#include "packet/flit.h"
#include "router/input_fifo.h"
#include "router/central_buffer.h"
#include "router/arbiter.h"

// ─────────────────────────────────────────────────────────────────────────────
//  RSpin — roteador SPIN 
//
//  Estrutura interna:
//    • 4 unidades DN (D0-D3): FIFO de entrada inferior + encaminha para Up
//    • 4 unidades UP (U0-U3): FIFO de entrada superior + encaminha para Down
//    • 1 buffer central (QDN): 18 palavras para pacotes bloqueados rumo a Down
//
//  Arbitragem dupla:
//    • up_arb_ (ArbiterUP): arbitra entre DN0-DN3 para escolher quem sobe
//    • dn_arb_ (ArbiterDN): arbitra entre UP0-UP3 + buffer central para descer
//
//  Roteamento:
//    • DN→DN : determinístico (tabela estática)
//    • DN→UP : adaptativo    (pick_best_up_port, round-robin por crédito)
//    • UP→DN : determinístico (tabela estática)
//    • UP→UP : PROIBIDO      (pacote descartado com aviso)
//
//  Chaveamento: wormhole (canal mantido por wormhole_out_ até flit TAIL)
// ─────────────────────────────────────────────────────────────────────────────

SC_MODULE(RSpin) {
    sc_in<bool>  clk;
    sc_in<bool>  rst;

    uint8_t router_id = 0;
    uint8_t level     = 0;

    sc_in<bool>  in_valid [NUM_PORTS];
    sc_in<Flit>  in_flit  [NUM_PORTS];
    sc_out<bool> in_credit[NUM_PORTS];

    sc_out<bool> out_valid [NUM_PORTS];
    sc_out<Flit> out_flit  [NUM_PORTS];
    sc_in<bool>  out_credit[NUM_PORTS];

    int8_t routing_table[NUM_ROUTERS];

    // Wormhole: guarda qual porta de saída foi alocada para cada porta de entrada
    // -1 = porta de entrada livre (sem pacote em curso)
    int8_t wormhole_out_[NUM_PORTS];

    // Round-robin para seleção adaptativa de porta Up
    uint8_t up_rr_last_ = 0;

    SC_CTOR(RSpin) {
        for (int i = 0; i < NUM_ROUTERS; ++i) routing_table[i] = -1;
        for (int i = 0; i < NUM_PORTS;   ++i) wormhole_out_[i] = -1;


        // ── FIFOs de entrada (uma por porta) ────────────────────────────────
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

        // ── Buffer central QDN (pacotes bloqueados rumo a Down) ─────────────
        cbuf_ = new CentralBuffer("cbuf_dn");
        cbuf_->clk(clk); cbuf_->rst(rst);
        cbuf_->push_valid(cbuf_push_valid_);
        cbuf_->push_flit(cbuf_push_flit_);
        cbuf_->push_dst_port(cbuf_push_dst_port_);
        cbuf_->push_ready(cbuf_push_ready_);
        cbuf_->out_valid(cbuf_out_valid_);
        cbuf_->out_flit(cbuf_out_flit_);
        cbuf_->out_dst_port(cbuf_out_dst_port_);
        cbuf_->pop_en(cbuf_pop_en_);

        // ── Árbitro UP: DN0-DN3 disputam saídas superiores ──────────────────
        up_arb_ = new ArbiterUP("up_arb");
        up_arb_->clk(clk); up_arb_->rst(rst);
        for (int p = 0; p < NUM_DN_PORTS; ++p)
            up_arb_->req[p](fifo_valid_[p]);   // apenas portas Down como fonte
        up_arb_->grant(up_grant_);
        up_arb_->grant_valid(up_grant_valid_);

        // ── Árbitro DN: UP0-UP3 + buffer central disputam saídas inferiores ─
        dn_arb_ = new ArbiterDN("dn_arb");
        dn_arb_->clk(clk); dn_arb_->rst(rst);
        for (int p = 0; p < NUM_UP_PORTS; ++p)
            dn_arb_->req[p](fifo_valid_[NUM_DN_PORTS + p]);  // portas Up como fonte
        dn_arb_->req[NUM_UP_PORTS](cbuf_out_valid_);          // buffer central
        dn_arb_->grant(dn_grant_);
        dn_arb_->grant_valid(dn_grant_valid_);

        SC_METHOD(process_up_traffic);   // DN→UP (subindo)
        sensitive << clk.pos();
        dont_initialize();

        SC_METHOD(process_dn_traffic);   // UP→DN (descendo) + buffer central
        sensitive << clk.pos();
        dont_initialize();
    }

    ~RSpin() {
        for (auto f : fifos_) delete f;
        delete cbuf_;
        delete up_arb_;
        delete dn_arb_;
    }

private:
    InputFIFO*     fifos_[NUM_PORTS];
    CentralBuffer* cbuf_;
    ArbiterUP*     up_arb_;
    ArbiterDN*     dn_arb_;

    sc_signal<bool> fifo_valid_[NUM_PORTS];
    sc_signal<Flit> fifo_flit_ [NUM_PORTS];
    sc_signal<bool> fifo_pop_  [NUM_PORTS];

    sc_signal<bool> cbuf_push_valid_;
    sc_signal<Flit> cbuf_push_flit_;
    sc_signal<int> cbuf_push_dst_port_;
    sc_signal<bool> cbuf_push_ready_;
    sc_signal<bool> cbuf_out_valid_;
    sc_signal<Flit> cbuf_out_flit_;
    sc_signal<int> cbuf_out_dst_port_;
    sc_signal<bool> cbuf_pop_en_;

    sc_signal<int>  up_grant_;
    sc_signal<bool> up_grant_valid_;
    sc_signal<int>  dn_grant_;
    sc_signal<bool> dn_grant_valid_;

    // ── Roteamento adaptativo entre portas Up ──────────────────────────────
    // Pacote vindo de porta inferior pode escolher
    // entre múltiplas portas superiores de forma adaptativa (por crédito).
    // Se a porta sugerida tem crédito → usa; senão, round-robin
    // entre U0-U3 buscando a primeira com crédito disponível.
    int pick_best_up_port(int hint) {
        if (hint >= 0 && is_up_port((uint8_t)hint) && out_credit[hint].read())
            return hint;

        int base = (hint >= (int)NUM_DN_PORTS) ? (hint - NUM_DN_PORTS + 1) : 0;
        for (int i = 0; i < (int)NUM_UP_PORTS; ++i) {
            int idx = (base + i) % NUM_UP_PORTS;
            int p   = NUM_DN_PORTS + idx;
            if (out_credit[p].read()) {
                up_rr_last_ = (uint8_t)idx;
                printf("[R%d] ADAPTIVE: Up%d → Up%d\n",
                       router_id, hint - (int)NUM_DN_PORTS, idx);
                return p;
            }
        }
        return hint;   // nenhuma Up com crédito → backpressure
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  process_up_traffic — trata fluxo DN→UP (subindo na árvore)
    //
    //  Árbitro UP escolhe entre DN0-DN3; roteamento é adaptativo entre U0-U3.
    //  Se src é Up, pacote NÃO pode subir de novo → descarta.
    // ─────────────────────────────────────────────────────────────────────────
    void process_up_traffic() {
        // Limpa saídas e sinais de pop das portas Down
        for (int p = 0; p < NUM_DN_PORTS; ++p) {
            fifo_pop_[p].write(false);
        }
        for (int p = NUM_DN_PORTS; p < NUM_PORTS; ++p) {
            out_valid[p].write(false);
        }
        cbuf_push_valid_.write(false);
        cbuf_push_dst_port_.write(-1);

        if (rst.read()) {
            for (int i = 0; i < NUM_DN_PORTS; ++i) wormhole_out_[i] = -1;
            up_rr_last_ = 0;
            return;
        }

        if (!up_grant_valid_.read()) return;

        int src_port = up_grant_.read();   // índice 0-3 = DN0-DN3
        if (src_port < 0 || src_port >= NUM_DN_PORTS) return;
        if (!fifo_valid_[src_port].read()) return;

        // Sanidade: porta de entrada deve ser Down
        if (is_up_port((uint8_t)src_port)) return;

        Flit flit = fifo_flit_[src_port].read();
        int  dst_port = -1;

        if (!flit.parity_ok()) {
            printf("[R%d @%s] ERRO: flit corrompido descartado na porta_entrada=%d src=%d type=%d data=0x%08X\n",
               router_id, sc_time_stamp().to_string().c_str(),
               src_port, flit.src_id, (int)flit.type, flit.data);
            fifo_pop_[src_port].write(true);
            if (flit.type == TAIL) wormhole_out_[src_port] = -1;
            return;
        }

        if (flit.type == HEADER) {
            uint8_t dst = flit.dst_id();
            if (dst >= NUM_ROUTERS) // endereço inválido 
            { fifo_pop_[src_port].write(true); return; }

            dst_port = routing_table[dst];

            if (dst_port >= 0 && is_down_port((uint8_t)dst_port)) {
                // DN→DN: roteamento determinístico (sem ação adaptativa)
            } else if (dst_port >= 0 && is_up_port((uint8_t)dst_port)) {
                // DN→UP: roteamento adaptativo (escolhe melhor porta Up)
                dst_port = pick_best_up_port(dst_port);
            }

            wormhole_out_[src_port] = (int8_t)dst_port;

            printf("[R%d @%s] UP_TRAFFIC HEADER src=%d dst=%d in=%d out=%d\n",
                   router_id, sc_time_stamp().to_string().c_str(),
                   flit.src_id, dst, src_port, dst_port);

        } else {
            dst_port = wormhole_out_[src_port];
            if (flit.type == TAIL) {
                wormhole_out_[src_port] = -1;
                printf("[R%d @%s] UP_TRAFFIC TAIL src=%d in=%d out=%d (wormhole fechado)\n",
                       router_id, sc_time_stamp().to_string().c_str(),
                       flit.src_id, src_port, dst_port);
            }
        }

        if (dst_port < 0 || dst_port >= NUM_PORTS) {
            fifo_pop_[src_port].write(true); return;
        }

        if (is_up_port((uint8_t)dst_port)) {
            // DN→UP: encaminha para saída superior
            if (out_credit[dst_port].read()) {
                out_valid[dst_port].write(true);
                out_flit [dst_port].write(flit);
                fifo_pop_[src_port].write(true);
            }
            // Sem crédito → backpressure na FIFO de entrada
        } else {
            // DN→DN: destino é saída inferior → move para buffer central se possível
            // (evita head-of-line blocking: spec SPIN slide 9-10)
            if (cbuf_push_ready_.read()) {
                cbuf_push_valid_.write(true);
                cbuf_push_flit_.write(flit);
                cbuf_push_dst_port_.write((int8_t)dst_port);
                fifo_pop_[src_port].write(true);
                printf("[R%d] DN→DN via buffer central (src=%d dst_port=%d)\n",
                       router_id, flit.src_id, dst_port);
            }
            // Buffer central cheio → backpressure
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  process_dn_traffic — trata fluxo UP→DN (descendo) e buffer central→DN
    //
    //  Árbitro DN escolhe entre UP0-UP3 e buffer central; roteamento é
    //  determinístico (só existe um caminho para cada filho na árvore).
    //  Regra SPIN: se src é Up, só pode ir para Down → garantido aqui.
    // ─────────────────────────────────────────────────────────────────────────
    void process_dn_traffic() {
        // Limpa saídas inferiores
        for (int p = 0; p < NUM_DN_PORTS; ++p) {
            out_valid[p].write(false);
        }
        for (int p = NUM_DN_PORTS; p < NUM_PORTS; ++p)
            fifo_pop_[p].write(false);
        cbuf_pop_en_.write(false);

        if (rst.read()) {
            for (int i = NUM_DN_PORTS; i < NUM_PORTS; ++i) wormhole_out_[i] = -1;
            return;
        }

        if (!dn_grant_valid_.read()) return;

        int grant = dn_grant_.read();   // 0-3=UP0-UP3, 4=buffer central
        bool from_cbuf = (grant == NUM_UP_PORTS);

        Flit flit;
        int  src_port = -1;

        if (from_cbuf) {
            if (!cbuf_out_valid_.read()) return;
            flit = cbuf_out_flit_.read();
            // Para buffer central, dst_port vem do wormhole aberto pela FIFO original
            // Aqui usamos o dst embutido no HEADER (re-decodifica)
            src_port = -1;   // especial: vem do buffer central
        } else {
            src_port = NUM_DN_PORTS + grant;   // UP0=4, UP1=5, ...
            if (src_port >= NUM_PORTS) return;
            if (!fifo_valid_[src_port].read()) return;
            flit = fifo_flit_[src_port].read();
        }

        int dst_port = -1;

        if (flit.type == HEADER) {
            uint8_t dst = flit.dst_id();
            if (dst >= NUM_ROUTERS) {
                if (!from_cbuf) fifo_pop_[src_port].write(true);
                else cbuf_pop_en_.write(true);
                return;
            }

            dst_port = routing_table[dst];

            // UP→UP proibido (spec SPIN slide 10)
            if (dst_port >= 0 && is_up_port((uint8_t)dst_port)) {
                printf("[R%d] AVISO: UP→UP proibido, descartado (src=%d)\n",
                       router_id, flit.src_id);
                if (!from_cbuf) fifo_pop_[src_port].write(true);
                else cbuf_pop_en_.write(true);
                return;
            }

            if (!from_cbuf)
                wormhole_out_[src_port] = (int8_t)dst_port;

            printf("[R%d @%s] DN_TRAFFIC HEADER src=%d dst=%d out=%d%s\n",
                   router_id, sc_time_stamp().to_string().c_str(),
                   flit.src_id, dst, dst_port,
                   from_cbuf ? " [cbuf]" : "");

        } else {
            if (!from_cbuf) {
                dst_port = wormhole_out_[src_port];
                if (flit.type == TAIL) {
                    wormhole_out_[src_port] = -1;
                    printf("[R%d @%s] DN_TRAFFIC TAIL src=%d out=%d (wormhole fechado)\n",
                           router_id, sc_time_stamp().to_string().c_str(),
                           flit.src_id, dst_port);
                }
            } else {
                // Flits BODY/TAIL do buffer central, decodifica destino do src_id
                // (simplificação: re-roteamos pelo dst embarcado, o que é válido 
                // para BODY/TAIL pois o canal wormhole original já foi aberto)
                dst_port = routing_table[flit.dst_id()];
            }
        }

        if (dst_port < 0 || dst_port >= NUM_DN_PORTS) {
            if (!from_cbuf) fifo_pop_[src_port].write(true);
            else cbuf_pop_en_.write(true);
            return;
        }

        if (out_credit[dst_port].read()) {
            out_valid[dst_port].write(true);
            out_flit [dst_port].write(flit);
            if (!from_cbuf) fifo_pop_[src_port].write(true);
            else            cbuf_pop_en_.write(true);
        }
        // Se não há crédito, mantém o flit na FIFO (backpressure natural)
    }
};
