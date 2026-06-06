# CAN Splitter (Gateway BMS e Monitoramento)

O **CAN Splitter** atua como um hub central (gateway) no barramento CAN de alta velocidade. Sua função principal é isolar a comunicação direta do **EMUS BMS** da rede geral do veículo, funcionando como um filtro de ruídos e tradutor para as placas dedicadas como o **Carregador** e a **Central Mestra (TMS Master)**.

Além de rotear os dados, ele opera como um cão de guarda (watchdog de rede), avaliando constantemente a latência e a vitalidade das mensagens provenientes do banco de baterias.

---

## 🏗️ Arquitetura de Hardware e Fluxo de Dados

O Microcontrolador tira proveito de dois periféricos FDCAN independentes para isolar completamente o ruidoso tráfego diagnóstico do BMS do tráfego limpo das Placas Lógicas Centrais.

```text
   REDE BATERIA (CAN1)                MCU: CAN SPLITTER                REDE VEICULO (CAN2)
  +-------------------+             +-------------------+             +-------------------+
  |                   |             |                   |             |                   |
  |     EMUS BMS      |------------>|   FDCAN1 (RX)     |------------>| CARREGADOR DILONG |
  |                   |    (A)      |        |          |    (B)      |                   |
  +-------------------+             |    WATCHDOG       |             +-------------------+
                                    |        |          |
                                    |   FDCAN2 (TX)     |             +-------------------+
                                    |                   |------------>|   TMS MASTER      |
                                    +-------------------+    (C)      |                   |
                                                                      +-------------------+

  (A) Mensagens BMS: 0x01, 0x05, 0x07, 0x20-0x3F
  (B) Telemetria Principal: 0x15408081, 0x15418081 (e Panic ID 0x0D428081)
  (C) Telemetria Células: 0x15438281 a 0x15478281
```

---

## 📡 Protocolo de Recepção (Ouvinte Emus BMS)

Todas as conversas advindas unicamente da Bateria caem no **FDCAN1**. A função `CAN_ProcessBMSMessage()` as decodifica preenchendo a global interna `emusBmsData` nos seguintes escopos:
 
- **`ID 0x01`**: Lê dados do escopo de tensão e calcula as tensões Mínima, Máxima e Média das células, assim como o consolidado global em *Volts*.
- **`ID 0x05`**: Capta a magnitude da corrente (em Amperes) e o percentual de State Of Charge (SOC).
- **`ID 0x07`**: Filtra as Flags de Segurança de hardware (proteções).
- **`IDs 0x20 a 0x3F`**: Mapeia individualmente as tensões de até 40 células para monitoramento detalhado.

---

## 📤 Protocolo de Transmissão (Locutor do Carro)

Apoiado na interrupção do **Timer 1 (`TIM1`)**, o pacote consolidado do Splitter é enviado periodicamente para o **FDCAN2** utilizando *Extended IDs*:

#### 1️⃣ Tensão e Corrente Totais - `ID 0x15408081`
| Byte(s)   | Dado                   | Tipo Primitivo |
|-----------|------------------------|----------------|
| **0 - 3** | Tensão Pack            | `float` 32-bit |
| **4 - 7** | Corrente Atual         | `float` 32-bit |

#### 2️⃣ Estatísticas e SOC - `ID 0x15418081`
| Byte(s)   | Dado                   | Escala / Tipo    |
|-----------|------------------------|------------------|
| **0 - 3** | Flags de Erro BMS      | Raw 32-bits      |
| **4**     | Voltagem Pior Célula   | (V - 2.0) * 100  |
| **5**     | Tensão Melhor Célula   | (V - 2.0) * 100  |
| **6**     | Média Geral            | (V - 2.0) * 100  |
| **7**     | SOC Direto             | % Direta (0-100) |

#### 3️⃣ Tensões Individuais das Células - `IDs 0x15438281 a 0x15478281`
| ID             | Células Mapeadas | Escala          |
|----------------|------------------|-----------------|
| **0x15438281** | 1 a 8            | (V - 2.0) * 100 |
| **0x15448281** | 9 a 16           | (V - 2.0) * 100 |
| **0x15458281** | 17 a 24          | (V - 2.0) * 100 |
| **0x15468281** | 25 a 32          | (V - 2.0) * 100 |
| **0x15478281** | 33 a 40          | (V - 2.0) * 100 |

---

## 🛡️ Firewall de Conectividade (WatchDog)

Sem contato contínuo com a bateria, as leituras são expostas como fantasmas e são inativas para tomadas de decisão. Pensando nisso o software monitora constantemente a data do recebimento de seu último log.

Caso passe-se mais um limite programado rígido de **3000ms (3 segundos)** através da checagem via `HAL_GetTick()`, o firewall da placa interrompe o roteamento e substitui as próximas levas por uma requisição fatal na malha.

Ele manda para o FDCAN2 as instruções:
- **ID de Pânico**: `0x0D428081`
- **Payload**: Byte zero constando `0x01` como Código oficial da Perda de Comunicação BMS. (Definido como `ERROR_CODE_BMS_LOST`).

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
