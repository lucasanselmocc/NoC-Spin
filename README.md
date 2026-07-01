# NoC-Spin

Implementação de uma Network-on-Chip (NoC) com topologia **SPIN** (Scalable, Programmable, Integrated Network), modelada em **SystemC**, para a disciplina de Organização de Computadores.

O projeto simula uma rede em árvore gorda (fat tree) quaternária de 2 níveis, com 8 roteadores (`RSpin`), controle de fluxo por créditos, roteamento wormhole e verificação de integridade dos flits por bit de paridade.

## O que o projeto contém

```
NoC-Spin-main/
├── CMakeLists.txt              # build do projeto (requer SystemC instalado)
├── docs/
│   ├── diagramas_mermaid.md    # diagramas de blocos (rede, flit, pacote, enlaces, roteador)
│   └── Artigo_NoCSpin/         # artigo em LaTeX (template IEEE)
├── scripts/
│   └── run_tests.sh            # compila e executa a simulação de ponta a ponta
├── sim/
│   ├── noc_trace.vcd           # trace de simulação (abrir no GTKWave)
│   └── log_simulacao.txt       # log textual de uma execução
└── src/
    ├── packet/flit.h           # definição do flit (header/body/tail) e cálculo de paridade
    ├── router/
    │   ├── input_fifo.h        # FIFO de entrada (profundidade 4) com controle de crédito
    │   ├── arbiter.h           # árbitro round-robin parametrizável (ArbiterN)
    │   └── router.h            # roteador RSPIN (RSpin): roteamento + wormhole + verificação de paridade
    ├── network/spin_network.h  # topologia: instancia e interliga os 8 roteadores em árvore gorda
    └── testbench/
        ├── testbench.h         # gera pacotes de teste (unicast, simultâneos, etc.)
        └── main.cpp            # ponto de entrada (sc_main), monta o testbench e roda a simulação
```

### Principais componentes

- **Flit** (`src/packet/flit.h`): unidade básica de transporte, com tipos `HEADER`, `BODY` e `TAIL`. O `HEADER` carrega o endereço de destino embutido nos 10 bits menos significativos do campo `data`. Cada flit carrega um bit de paridade par, calculado sobre `data`, usado pelo roteador para descartar flits corrompidos.
- **InputFIFO** (`input_fifo.h`): buffer de 4 posições por porta de entrada de cada roteador, com sinalização de crédito (`push_credit`) para controle de fluxo — evita perda de flits quando o roteador de destino está ocupado (backpressure).
- **Arbiter / ArbiterN** (`arbiter.h`): árbitro round-robin genérico (template em `N` portas), usado para decidir qual porta de entrada tem acesso ao crossbar de saída a cada ciclo, garantindo justiça (fairness) entre requisições concorrentes.
- **RSpin** (`router.h`): o roteador propriamente dito. Junta as 8 FIFOs de entrada, o árbitro, a tabela de roteamento e a lógica de roteamento wormhole (mantém o canal de saída aberto entre o `HEADER` e o `TAIL` de um mesmo pacote). Também valida a regra SPIN de que um pacote vindo de uma porta "Up" não pode subir novamente, e descarta flits com paridade inválida.
- **SpinNetwork** (`spin_network.h`): monta a topologia — 8 roteadores organizados em árvore de 2 níveis (1 raiz, 3 intermediários, 4 folhas), interliga as portas `Up`/`Down` entre eles e conecta os terminais externos de cada roteador. Constrói as tabelas de roteamento estáticas.
- **Testbench** (`testbench.h` + `main.cpp`): injeta pacotes de teste na rede (unicast simples e envios simultâneos) e verifica se chegam corretamente ao destino, imprimindo um log e, opcionalmente, gerando um trace `.vcd`.

## Como compilar e rodar

### Pré-requisitos

- CMake >= 3.16
- Compilador C++17 (g++ ou clang)
- **SystemC** instalado (biblioteca `libsystemc` + headers `systemc.h`)
  - Por padrão o projeto procura em `/usr/local/systemc`. Se o SystemC estiver em outro caminho, informe via `-DSYSTEMC_HOME=/caminho/para/systemc`.

### Opção 1 — script automático (recomendado)

```bash
export SYSTEMC_HOME=/caminho/para/systemc   # se não estiver em /usr/local/systemc
./scripts/run_tests.sh
```

O script cria a pasta `build/`, roda o CMake, compila e já executa a simulação, gerando o trace em `sim/noc_trace.vcd`.

### Opção 2 — manual

```bash
mkdir -p build
cmake -S . -B build -DSYSTEMC_HOME=/caminho/para/systemc
cmake --build build -- -j$(nproc)
./build/spin_noc_sim --vcd
```

A flag `--vcd` habilita a geração do trace de simulação. Sem ela, a simulação roda apenas com o log textual no terminal.

### Visualizando o trace

```bash
gtkwave sim/noc_trace.vcd
```

### O que a simulação faz

O testbench (`src/testbench/testbench.h`) roda cenários de teste, entre eles:

- **TC1**: unicast simples R5 → R7
- **TC2**: unicast simples R6 → R0
- **TC3**: dois envios simultâneos (R5→R7 e R6→R4), verificando se a rede lida com concorrência sem perder pacotes

Ao final, a simulação imprime quantos pacotes foram enviados e recebidos com sucesso.

## Documentação adicional

- `docs/diagramas_mermaid.md`: diagramas de blocos da rede, do flit, do pacote, dos enlaces e do roteador SPIN (renderizam automaticamente no GitHub ou em qualquer visualizador Mermaid).
- `docs/Artigo_NoCSpin/`: artigo descrevendo o projeto no template IEEE (LaTeX).
