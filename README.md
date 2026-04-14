# CAN Splitter (Gateway BMS e Monitoramento)

O **CAN Splitter** atua como um hub central (gateway) no barramento CAN de alta velocidade. Sua função principal é isolar a comunicação direta do **EMUS BMS** da rede geral do veículo, funcionando como um filtro de ruídos e tradutor para as placas dedicadas como o **Carregador** e a **Central Mestra (TMS Master)**.

Além de rotear os dados, ele opera como um cão de guarda (watchdog de rede), avaliando constantemente a latência e a vitalidade das mensagens provenientes do banco de baterias.

---

## 🏗️ Arquitetura de Hardware e Fluxo de Dados

O Microcontrolador tira proveito de dois periféricos FDCAN independentes para isolar completamente o ruidoso tráfego diagnóstico do BMS do tráfego limpo das Placas Lógicas Centrais.

```mermaid
graph LR
    subgraph Rede Fechada da Bateria (CAN1)
        BMS[EMUS BMS]
    end

    subgraph MCU: CAN Splitter (Gateway Mestre)
        RX[FDCAN1 RX : Decode de Mensagem]
        WD[Monitor de Vitalidade de Rede]
        TX[FDCAN2 TX : Retransmissor Lógico]
    end

    subgraph Rede Central do Veículo (CAN2)
        CHARGER[Carregador Dilong]
        MASTER[TMS Master Central]
    end

    BMS -- IDs 0x01, 0x05, 0x07 --> RX
    RX -->|Grava mem| WD
    WD -->|Leitura Periódica| TX
    
    WD -- Falha > 3s --> |ID: 0x0D428081| CHARGER
    WD -- Falha > 3s --> |ID: 0x0D428081| MASTER
    
    TX -- IDs de Telemetria: 0x19308082, 0x19318082 --> CHARGER
    TX -- IDs de Telemetria: 0x19308082, 0x19318082 --> MASTER
```

---

## 📡 Protocolo de Recepção (Ouvinte Emus BMS)

Todas as conversas advindas unicamente da Bateria caem no **FDCAN1**. A função `CAN_ProcessBMSMessage()` as decodifica preenchendo a global interna `emusBmsData` nos seguintes escopos:
 
- **`ID 0x01`**: Lê dados do escopo de tensão e calcula as tensões Mínima, Máxima e Média das células, assim como o consolidado global em *Volts* para a CPU local.
- **`ID 0x05`**: Capta a magnitude da corrente (em Amperes) fluindo na alta tensão e o percentual cravado de State Of Charge (SOC).
- **`ID 0x07`**: Filtra as Flags de Segurança de hardware emitidos remotamente pela BMS antes do shutdown.

---

## 📤 Protocolo de Transmissão (Locutor do Carro)

Apoiado na interrupção do **Timer 1 (`TIM1`)**, o pacote da variável limpa, estabilizada e unificada do Splitter é periodicamente repassado para a frente (**FDCAN2**) a todos os aparelhos sob 2 *Extended IDs*:

#### 1️⃣ Tensão e Corrente Totais - `ID 0x19308082`
| Byte(s)   | Dado                   | Tipo Primitivo |
|-----------|------------------------|----------------|
| **0 - 3** | Tensão Pack            | `float` 32-bit |
| **4 - 7** | Corrente Atual         | `float` 32-bit |

#### 2️⃣ Estatísticas Menores e Alertas - `ID 0x19318082`
| Byte(s)   | Dado                   | Escala          |
|-----------|------------------------|-----------------|
| **0 - 3** | Flags de Erro BMS      | Raw 32-bits     |
| **4**     | Voltagem da Pior Célula| V * 50 (`uint8`)|
| **5**     | Tensão da Melhor Célula| V * 50 (`uint8`)|
| **6**     | Média Geral em Alta    | V * 50 (`uint8`)|
| **7**     | SOC Direto             | % Direta (0-100)|

---

## 🛡️ Firewall de Conectividade (WatchDog)

Sem contato contínuo com a bateria, as leituras são expostas como fantasmas e são inativas para tomadas de decisão. Pensando nisso o software monitora constantemente a data do recebimento de seu último log.

Caso passe-se mais um limite programado rígido de **3000ms (3 segundos)** através da checagem via `HAL_GetTick()`, o firewall da placa interrompe o roteamento e substitui as próximas levas por uma requisição fatal na malha.

Ele manda para o FDCAN2 as instruções:
- **ID de Pânico**: `0x0D428081`
- **Payload**: Byte zero constando `0x01` como Código oficial da Perda de Comunicação BMS.

Isso previne que dispositivos dependentes da medição da bateria prossigam com a carga ou aceleração.

---

## 🧪 Ambiente Virtual / Testes In Silico (Loopback) Regulável

Existe um simulador embutido para atestar a leitura de todos os mostradores (painéis, carregadores etc) **mesmo quando a placa do veículo se encontra desconectada fisicamente do Emus BMS**. 

Para rodar essa fábrica de tráfegos fantasmas, ative os `#define` no topo de do arquivo base `can.h` com uma simples descomentada, recompile e o simulador assumirá o bus de dados:

```c
/* ==================== Configuração de Teste ========================== */
#define testLoopbackCAN1
#define testLoopbackCAN2
```
