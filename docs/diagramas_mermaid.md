# Diagramas Mermaid

## Diagrama de Blocos da NoC

```mermaid
flowchart LR
    subgraph ORIGEM[No de origem]
        APP1[Modulo gerador] --> PKT[Pacote]
        PKT --> FLITS[Divisao em flits]
    end

    subgraph R1[Roteador 1]
        Q1[Fila de entrada]
        RT1[Logica de roteamento]
        Q1 --> RT1
    end

    subgraph R2[Roteador 2]
        Q2[Fila de entrada]
        RT2[Logica de roteamento]
        Q2 --> RT2
    end

    subgraph DESTINO[No de destino]
        RX[Recepcao dos flits] --> MSG[Mensagem recebida]
    end

    FLITS --> EN1[Enlace]
    EN1 --> Q1
    RT1 --> EN2[Enlace]
    EN2 --> Q2
    RT2 --> EN3[Enlace]
    EN3 --> RX
```

## Diagrama de Blocos do Flit

```mermaid
flowchart LR
    subgraph FLIT[Flit]
        TIPO[Tipo]
        PID[ID]
        ORIG[Origem]
        DEST[Destino]
        DATA[Payload]
        PAR[Paridade]
    end

    TIPO --- PID
    PID --- ORIG
    ORIG --- DEST
    DEST --- DATA
    DATA --- PAR
```

## Diagrama de Blocos do Pacote

```mermaid
flowchart LR
    subgraph PKT[Pacote]
        HDR[Cabecalho]
        F1[Flit de cabecalho]
        F2[Flit de dados]
        F3[Flit final]
        CTRL[Informacoes de controle]
    end

    HDR --> F1
    F1 --> F2
    F2 --> F3
    F3 --> CTRL

    HDR --> ORIG[Origem]
    HDR --> DEST[Destino]
    HDR --> PID[ID do pacote]

    CTRL --> TAM[Tamanho]
    CTRL --> PAR[Paridade / verificacao]
```

## Diagrama de Blocos dos Enlaces

```mermaid
flowchart LR
    R1[Roteador A] --> TX[Porta de saida]
    TX --> EN[Enlace]
    EN --> RX[Porta de entrada]
    RX --> R2[Roteador B]

    subgraph EN[Enlace]
        CANAL[Canal de dados]
        LAT[Atraso de transmissao]
        STATUS[Disponibilidade]
    end

    CANAL --> LAT
    LAT --> STATUS
```

## Fluxo Completo do Modelo

```mermaid
flowchart LR
    subgraph ORIGEM[No de origem]
        GEN[Geracao da mensagem]
        PKT[Criacao do pacote]
        SPLIT[Divisao em flits]
        GEN --> PKT
        PKT --> SPLIT
    end

    subgraph REDE[Network-on-Chip]
        INJ[Injecao dos flits na rede]
        Q1[Fila de entrada]
        R1[Roteador]
        E1[Enlace]
        Q2[Fila de entrada]
        R2[Roteador]
        E2[Enlace]

        INJ --> Q1
        Q1 --> R1
        R1 --> E1
        E1 --> Q2
        Q2 --> R2
        R2 --> E2
    end

    subgraph DESTINO[No de destino]
        RX[Recepcao dos flits]
        CHECK[Verificacao / paridade]
        MSG[Mensagem recebida]
        RX --> CHECK
        CHECK --> MSG
    end

    SPLIT --> INJ
    E2 --> RX
```

## Roteador em Estrutura SPIN

```mermaid
flowchart TB
    subgraph R[Roteador SPIN]
        L_IN[Entrada local] --> LQ[Fila local]
        C_IN[Entrada filho] --> CQ[Fila inferior]
        P_IN[Entrada pai] --> PQ[Fila superior]

        LQ --> RT[Logica de roteamento]
        CQ --> RT
        PQ --> RT

        RT --> BF[Verificacao de fila / disponibilidade]
        BF --> SW[Encaminhamento]

        SW --> L_OUT[Saida local]
        SW --> C_OUT[Saida para filho]
        SW --> P_OUT[Saida para pai]
    end
```
