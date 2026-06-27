#pragma once
#include <systemc.h>
#include <iostream>
#include "network/spin_network.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Testbench — injeta e coleta pacotes na rede SPIN
//
//  Casos de teste:
//    TC1 — Unicast folha→folha:   R5 → R7  (sobe R5→R1→R0, desce R0→R3→R7)
//    TC2 — Unicast folha→raiz:    R6 → R0
//    TC3 — Tráfego simultâneo:    R5→R7 e R6→R4 ao mesmo tempo
// ─────────────────────────────────────────────────────────────────────────────
SC_MODULE(Testbench) {
    sc_in<bool>  clk;

    sc_out<bool> in_valid  [NUM_ROUTERS];
    sc_out<Flit> in_flit   [NUM_ROUTERS];
    sc_in<bool>  in_credit [NUM_ROUTERS];
    sc_in<bool>  out_valid [NUM_ROUTERS];
    sc_in<Flit>  out_flit  [NUM_ROUTERS];
    sc_out<bool> out_credit[NUM_ROUTERS];

    int packets_sent_     = 0;
    int packets_received_ = 0;

    SC_CTOR(Testbench) {
        SC_THREAD(run);
        sensitive << clk.pos();
    }

    void run() {
        // Inicialização
        for (int i = 0; i < NUM_ROUTERS; ++i) {
            in_valid[i].write(false);
            in_flit[i].write(Flit{});
            out_credit[i].write(true);  // sempre aceitando
        }
        wait(5);

        banner("Spin NoC Testbench — inicio");

        // TC1: R5 → R7 (atravessa a rede: folha→inter→raiz→inter→folha)
        log("TC1: Unicast R5 -> R7");
        send_packet(5, 7, 0xAAAA1111);
        wait_recv(7, 5, "TC1");

        // TC2: R6 → R0
        log("TC2: Unicast R6 -> R0 (folha para raiz)");
        send_packet(6, 0, 0xBBBB2222);
        wait_recv(0, 6, "TC2");

        // TC3: simultâneo
        log("TC3: Simultaneo R5->R7 e R6->R4");
        inject_header(5, 7, 0xCCCC0001);
        inject_header(6, 4, 0xDDDD0002);
        wait(2);
        inject_tail(5, 7, 0xCCCC00FF);
        inject_tail(6, 4, 0xDDDD00FF);
        wait(30);
        check_recv(7, "TC3-A (R5->R7)");
        check_recv(4, "TC3-B (R6->R4)");

        banner("Concluido! Enviados=" + std::to_string(packets_sent_) +
               " Recebidos=" + std::to_string(packets_received_));
        sc_stop();
    }

private:
    void send_packet(uint8_t src, uint8_t dst, uint32_t data) {
        Flit h = Flit::make_header(src, dst);
        h.timestamp = sc_time_stamp().value();
        inject_flit_wait(src, h);

        Flit t = Flit::make_data(src, data, TAIL);
        inject_flit_wait(src, t);
        packets_sent_++;
    }

    void inject_flit_wait(uint8_t port, Flit f) {
        while (!in_credit[port].read()) wait();
        in_valid[port].write(true);
        in_flit[port].write(f);
        wait();
        in_valid[port].write(false);
    }

    void inject_header(uint8_t src, uint8_t dst, uint32_t proto) {
        if (!in_credit[src].read()) return;
        Flit h = Flit::make_header(src, dst, proto);
        h.timestamp = sc_time_stamp().value();
        in_valid[src].write(true);
        in_flit[src].write(h);
    }

    void inject_tail(uint8_t src, uint8_t dst, uint32_t data) {
        (void)dst;
        Flit t = Flit::make_data(src, data, TAIL);
        in_valid[src].write(true);
        in_flit[src].write(t);
        wait();
        in_valid[src].write(false);
        in_valid[src].write(false);
        packets_sent_++;
    }

    void wait_recv(uint8_t router, uint8_t expected_src, const std::string& tag,
                   int timeout = 60) {
        for (int i = 0; i < timeout; ++i) {
            wait();
            if (out_valid[router].read()) {
                Flit f = out_flit[router].read();
                if (f.type == HEADER && f.src_id == expected_src) {
                    uint64_t lat = sc_time_stamp().value() - f.timestamp;
                    std::cout << "  [OK] " << tag << " recebido em R" << (int)router
                              << " | latencia=" << lat << "ns"
                              << " | " << f << "\n";
                    packets_received_++;
                    return;
                }
            }
        }
        std::cout << "  [TIMEOUT] " << tag << " nao chegou em R" << (int)router << "\n";
    }

    void check_recv(uint8_t router, const std::string& tag) {
        if (out_valid[router].read()) {
            std::cout << "  [OK] " << tag << " recebido em R" << (int)router << "\n";
            packets_received_++;
        } else {
            std::cout << "  [MISS] " << tag << " nao detectado em R" << (int)router << "\n";
        }
    }

    void log(const std::string& s) {
        std::cout << "\n[" << sc_time_stamp() << "] " << s << "\n";
    }

    void banner(const std::string& s) {
        std::cout << "\n==============================\n  " << s << "\n==============================\n";
    }
};
