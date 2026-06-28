#pragma once
#include <systemc.h>
#include <cstdint>
#include <string>

static constexpr int NUM_ROUTERS  = 8;   // 1 raiz + 4 filhos + 3 folhas extras
static constexpr int FIFO_DEPTH   = 4;   // buffer de 4 palavras por entrada (spec SPIN)
static constexpr int NUM_UP_PORTS = 4;   // portas superiores U0-U3
static constexpr int NUM_DN_PORTS = 4;   // portas inferiores D0-D3
static constexpr int NUM_PORTS    = 8;   // total de portas por roteador RSPIN

// Índices de porta: 0-3 = Down (D0-D3), 4-7 = Up (U0-U3)
// "Down" = conexão para os filhos (nível inferior da árvore)
// "Up"   = conexão para o pai   (nível superior da árvore)
enum PortIdx : uint8_t {
    D0 = 0, D1 = 1, D2 = 2, D3 = 3,   // portas inferiores (para filhos/terminais)
    U0 = 4, U1 = 5, U2 = 6, U3 = 7    // portas superiores (para o pai)
};

inline bool is_up_port(uint8_t p)   { return p >= 4; }
inline bool is_down_port(uint8_t p) { return p <  4; }

enum FlitType : uint8_t {
    HEADER = 0,  // primeiro flit -> contém endereço destino em [9:0]
    BODY   = 1,  // flit de dados intermediário
    TAIL   = 2   // último flit do pacote
};

struct Flit {
    FlitType type;
    uint32_t data;      // 32 bits de dado/protocolo
    bool     parity;    // paridade par calculada sobre a palavra data
    uint8_t  src_id;
    uint64_t timestamp; // ciclo de injeção (para medir latência)

    Flit() : type(HEADER), data(0), parity(false), src_id(0), timestamp(0) {}

    static bool calc_parity(uint32_t word) {
        bool p = false;
        while (word != 0) {
            p = !p;
            word &= (word - 1);
        }
        return p;
    }

    void update_parity() {
        parity = calc_parity(data);
    }

    bool parity_ok() const {
        return parity == calc_parity(data);
    }

    // Constrói header com destino embutido em data[9:0]
    static Flit make_header(uint8_t src, uint8_t dst, uint32_t proto = 0) {
        Flit f;
        f.type      = HEADER;
        f.src_id    = src;
        f.data      = (proto & 0xFFFFFC00u) | (dst & 0x3FFu);
        f.timestamp = 0;
        f.update_parity();
        return f;
    }

    static Flit make_data(uint8_t src, uint32_t payload, FlitType t = BODY) {
        Flit f;
        f.type      = t;
        f.src_id    = src;
        f.data      = payload;
        f.timestamp = 0;
        f.update_parity();
        return f;
    }

    // Extrai endereço de destino do header
    uint8_t dst_id() const { return static_cast<uint8_t>(data & 0x3FFu); }

    bool operator==(const Flit& o) const {
        return type == o.type && data == o.data && parity == o.parity && src_id == o.src_id;
    }

    std::string to_string() const {
        static const char* tn[] = {"HEADER","BODY","TAIL"};
        char buf[80];
        if (type == HEADER)
            snprintf(buf, sizeof(buf), "[HEADER src=%d dst=%d data=0x%08X parity=%d]",
                     src_id, dst_id(), data, parity ? 1 : 0);
        else
            snprintf(buf, sizeof(buf), "[%s src=%d data=0x%08X parity=%d]",
                     tn[type], src_id, data, parity ? 1 : 0);
        return buf;
    }
};

inline void sc_trace(sc_trace_file* tf, const Flit& f, const std::string& name) {
    sc_trace(tf, f.data,   name + ".data");
    sc_trace(tf, f.parity, name + ".parity");
    sc_trace(tf, f.src_id, name + ".src");
    uint8_t dummy = 0; (void)dummy;
}

inline std::ostream& operator<<(std::ostream& os, const Flit& f) {
    return os << f.to_string();
}
