#pragma once
#include <systemc.h>
#include "packet/flit.h"
#include "router/input_fifo.h"
#include "router/arbiter.h"

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

    SC_CTOR(RSpin) {
        for (int i = 0; i < NUM_ROUTERS; ++i) routing_table[i] = -1;
        for (int i = 0; i < NUM_PORTS;   ++i) wormhole_out_[i] = -1;

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

        if (rst.read()) {
            for (int i = 0; i < NUM_PORTS; ++i) wormhole_out_[i] = -1;
            return;
        }

        if (!grant_valid_.read()) return;

        int src_port = grant_idx_.read();
        if (src_port < 0 || src_port >= NUM_PORTS) return;
        if (!fifo_valid_[src_port].read()) return;

        Flit flit = fifo_flit_[src_port].read();

        if (!flit.parity_ok()) {
            printf("[R%d @%s] ERRO: flit corrompido descartado na porta_entrada=%d src=%d type=%d data=0x%08X\n",
                   router_id, sc_time_stamp().to_string().c_str(),
                   src_port, flit.src_id, (int)flit.type, flit.data);
            fifo_pop_[src_port].write(true);
            if (flit.type == TAIL) wormhole_out_[src_port] = -1;
            return;
        }

        int dst_port = -1;

        if (flit.type == HEADER) {
            uint8_t dst = flit.dst_id();

            if (dst >= NUM_ROUTERS) return;  // endereço inválido

            dst_port = routing_table[dst];

            // Valida regra SPIN: pacote vindo de Up não pode subir de novo
            if (is_up_port((uint8_t)src_port) && dst_port >= 0 && is_up_port((uint8_t)dst_port)) {
                printf("[R%d] AVISO: pacote de Up tentou subir de novo, descartado\n", router_id);
                fifo_pop_[src_port].write(true);
                return;
            }

            // Abre canal wormhole
            wormhole_out_[src_port] = (int8_t)dst_port;

            printf("[R%d @%s] HEADER src=%d dst=%d → porta_entrada=%d porta_saida=%d\n",
                   router_id, sc_time_stamp().to_string().c_str(),
                   flit.src_id, dst, src_port, dst_port);

        } else {
            // BODY ou TAIL: segue o canal wormhole aberto pelo HEADER
            dst_port = wormhole_out_[src_port];

            if (flit.type == TAIL) {
                wormhole_out_[src_port] = -1;  // fecha o canal
                printf("[R%d @%s] TAIL src=%d → porta_entrada=%d porta_saida=%d (canal fechado)\n",
                       router_id, sc_time_stamp().to_string().c_str(),
                       flit.src_id, src_port, dst_port);
            }
        }

        if (dst_port < 0 || dst_port >= NUM_PORTS) {
            printf("[R%d] dst_port invalido (%d) para flit src=%d type=%d\n",
                   router_id, dst_port, flit.src_id, (int)flit.type);
            fifo_pop_[src_port].write(true);  // descarta
            return;
        }

        if (out_credit[dst_port].read()) {
            out_valid[dst_port].write(true);
            out_flit [dst_port].write(flit);
            fifo_pop_[src_port].write(true);
        }
        // Se não há crédito, mantém o flit na FIFO (backpressure natural)
    }
};
