#include <systemc.h>
#include <iostream>
#include <string>
#include "network/spin_network.h"
#include "testbench/testbench.h"

int sc_main(int argc, char* argv[]) {
    bool gen_vcd = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--vcd") gen_vcd = true;

    sc_clock clk("clk", 10, SC_NS);  // 100 MHz
    sc_signal<bool> rst("rst");

    sc_signal<bool> in_valid  [NUM_ROUTERS];
    sc_signal<Flit> in_flit   [NUM_ROUTERS];
    sc_signal<bool> in_credit [NUM_ROUTERS];
    sc_signal<bool> out_valid [NUM_ROUTERS];
    sc_signal<Flit> out_flit  [NUM_ROUTERS];
    sc_signal<bool> out_credit[NUM_ROUTERS];

    SpinNetwork noc("spin_noc");
    noc.clk(clk); noc.rst(rst);
    for (int i = 0; i < NUM_ROUTERS; ++i) {
        noc.ext_in_valid  [i](in_valid[i]);
        noc.ext_in_flit   [i](in_flit[i]);
        noc.ext_in_credit [i](in_credit[i]);
        noc.ext_out_valid [i](out_valid[i]);
        noc.ext_out_flit  [i](out_flit[i]);
        noc.ext_out_credit[i](out_credit[i]);
    }

    Testbench tb("tb");
    tb.clk(clk);
    for (int i = 0; i < NUM_ROUTERS; ++i) {
        tb.in_valid  [i](in_valid[i]);
        tb.in_flit   [i](in_flit[i]);
        tb.in_credit [i](in_credit[i]);
        tb.out_valid [i](out_valid[i]);
        tb.out_flit  [i](out_flit[i]);
        tb.out_credit[i](out_credit[i]);
    }

    sc_trace_file* tf = nullptr;
    if (gen_vcd) {
        tf = sc_create_vcd_trace_file("sim/noc_trace");
        sc_trace(tf, clk,   "clk");
        sc_trace(tf, rst,   "rst");
        for (int i = 0; i < NUM_ROUTERS; ++i) {
            sc_trace(tf, in_valid[i],  "in_valid_r"  + std::to_string(i));
            sc_trace(tf, out_valid[i], "out_valid_r" + std::to_string(i));
            sc_trace(tf, in_credit[i], "in_credit_r" + std::to_string(i));
        }
        std::cout << "[VCD] sim/noc_trace.vcd\n";
    }

    rst.write(true);
    sc_start(20, SC_NS);
    rst.write(false);

    sc_start();

    if (tf) sc_close_vcd_trace_file(tf);
    std::cout << "[SIM] Tempo total: " << sc_time_stamp() << "\n";
    return 0;
}
