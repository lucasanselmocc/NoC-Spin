#pragma once
#include <systemc.h>
#include <iostream>
#include "network/spin_network.h"

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
        for (int i = 0; i < NUM_ROUTERS; ++i) {
            in_valid[i].write(false);
            in_flit[i].write(Flit{});
            out_credit[i].write(true);
        }
        wait(5);

        banner("Spin NoC Testbench — inicio");

        // TC1: R5 → R7
        log("TC1: Unicast R5 -> R7");
        send_packet(5, 7, 0xAAAA1111);
        wait_recv(7, 5, "TC1");

        // TC2: R6 → R0
        log("TC2: Unicast R6 -> R0");
        send_packet(6, 0, 0xBBBB2222);
        wait_recv(0, 6, "TC2");

        // TC3: simultâneo — injeta e depois espera em ambos
        log("TC3: Simultaneo R5->R7 e R6->R4");
        send_packet_nb(5, 7, 0xCCCC0001);
        send_packet_nb(6, 4, 0xDDDD0002);
        packets_sent_ += 2;

        // Espera os dois chegarem com timeout independente
        bool got_a = false, got_b = false;
        for (int i = 0; i < 200 && !(got_a && got_b); ++i) {
            wait();
            if (!got_a && out_valid[7].read()) {
                Flit f = out_flit[7].read();
                if (f.src_id == 5) {
                    log_ok("TC3-A (R5->R7)", 7, f);
                    got_a = true;
                    packets_received_++;
                }
            }
            if (!got_b && out_valid[4].read()) {
                Flit f = out_flit[4].read();
                if (f.src_id == 6) {
                    log_ok("TC3-B (R6->R4)", 4, f);
                    got_b = true;
                    packets_received_++;
                }
            }
        }
        if (!got_a) std::cout << "  [MISS] TC3-A nao chegou em R7\n";
        if (!got_b) std::cout << "  [MISS] TC3-B nao chegou em R4\n";

        // TC4: teste do buffer central: R1 recebe pacote de R0 para R5
        //   R0 injeta para R1 (DN→DN no R0); R1 deve rotear para R5 via D0
        //   O buffer central de R0 é exercitado pois o pacote de R0→R1 é DN→DN
        log("TC4: R0 -> R5 (exercita buffer central em R0)");
        send_packet(0, 5, 0xEEEE0005);
        wait_recv(5, 0, "TC4");

        // TC5: raiz→folha distante (R0→R7)
        log("TC5: R0 -> R7");
        send_packet(0, 7, 0xFF007777);
        wait_recv(7, 0, "TC5");

        banner("Concluido! Enviados=" + std::to_string(packets_sent_) +
               " Recebidos=" + std::to_string(packets_received_));
        sc_stop();
    }

private:
    // Envia pacote e incrementa contador (bloqueante — espera crédito)
    void send_packet(uint8_t src, uint8_t dst, uint32_t data) {
        Flit h = Flit::make_header(src, dst);
        h.timestamp = sc_time_stamp().value();
        inject_wait(src, h);

        Flit t = Flit::make_data(src, data, TAIL);
        inject_wait(src, t);
        packets_sent_++;
    }

    // Envia pacote sem bloquear na espera de recebimento (para TC3)
    void send_packet_nb(uint8_t src, uint8_t dst, uint32_t data) {
        Flit h = Flit::make_header(src, dst);
        h.timestamp = sc_time_stamp().value();
        inject_wait(src, h);

        Flit t = Flit::make_data(src, data, TAIL);
        inject_wait(src, t);
    }

    void inject_wait(uint8_t port, Flit f) {
        while (!in_credit[port].read()) wait();
        in_valid[port].write(true);
        in_flit[port].write(f);
        wait();
        in_valid[port].write(false);
    }

    void wait_recv(uint8_t router, uint8_t expected_src,
                   const std::string& tag, int timeout = 100) {
        for (int i = 0; i < timeout; ++i) {
            wait();
            if (out_valid[router].read()) {
                Flit f = out_flit[router].read();
                if (f.src_id == expected_src && f.type == HEADER) {
                    log_ok(tag, router, f);
                    packets_received_++;
                    return;
                }
            }
        }
        std::cout << "  [TIMEOUT] " << tag << " nao chegou em R" << (int)router << "\n";
    }

    void log_ok(const std::string& tag, uint8_t router, const Flit& f) {
        uint64_t lat = (sc_time_stamp().value() - f.timestamp) / 1000;
        std::cout << "  [OK] " << tag << " em R" << (int)router
                  << " | latencia=" << lat << "ns | " << f << "\n";
    }

    void log(const std::string& s) {
        std::cout << "\n[" << sc_time_stamp() << "] " << s << "\n";
    }

    void banner(const std::string& s) {
        std::cout << "\n==============================\n  " << s
                  << "\n==============================\n";
    }
};
