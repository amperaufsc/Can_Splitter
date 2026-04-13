# Canal Splitter - EMUS G1 BMS Gateway & Telemetry

O **Can_Splitter** é um firmware de gateway robusto desenvolvido para o STM32G491RETX, projetado para atuar como uma ponte inteligente (unidirecional) entre um sistema de gerenciamento de bateria **EMUS G1 BMS** e o barramento principal do veículo.

## 🚀 Funcionalidades Principais

- **Gateway de Isolamento:** Separa fisicamente a rede de alta prioridade das baterias (CAN1) da rede do veículo/telemetria (CAN2).
- **Tradução de Protocolo:** Converte as mensagens brutas da EMUS (que usam offsets e endianness mista) em pacotes de telemetria de alta precisão (IEEE 754 Floats).
- **Segurança Fail-Safe:** Implementa lógica de retentativas e travamento seguro (`Error_Handler`) em caso de falha crítica no barramento.
- **Simulador Interno:** Ferramenta de diagnóstico para simular o comportamento de uma bateria sem a necessidade de hardware real conectado (Loopback).

## 📊 Mapeamento de Barramentos

| Periférico | Função | Configuração |
| :--- | :--- | :--- |
| **FDCAN1** | Interface BMS (Input) | Classic CAN @ 500kbps (ou conforme projeto) |
| **FDCAN2** | Interface Veículo (Output) | Classic CAN @ 500kbps (Telemetria) |

## 📡 Protocolo de Telemetria (Saída CAN2)

Os dados são consolidados e transmitidos a cada **100ms** utilizando IDs estendidos (29 bits).

### ID 1: `0x19308082` (Dados de Energia)
| Bytes | Dado | Tipo | Unidade |
| :--- | :--- | :--- | :--- |
| **0-3** | Total Voltage | float (32-bit) | V |
| **4-7** | Instantaneous Current | float (32-bit) | A |

### ID 2: `0x19318082` (Estado e Células)
| Bytes | Dado | Tipo | Unidade |
| :--- | :--- | :--- | :--- |
| **0-3** | Protection Flags | uint32_t | Bitmask |
| **4** | Min Cell Voltage | uint8_t | 0.02V/bit (DIV 50) |
| **5** | Max Cell Voltage | uint8_t | 0.02V/bit (DIV 50) |
| **6** | Avg Cell Voltage | uint8_t | 0.02V/bit (DIV 50) |
| **7** | User SOC | uint8_t | % |

> [!TIP]
> **Dica de Leitura:** Para ler a tensão da célula na telemetria, basta dividir o valor recebido por 50. Exemplo: valor `172` / 50 = `3.44V`.

## 🛠️ Ferramentas de Teste e Diagnóstico (Loopback)

O projeto inclui macros de pré-compilação no arquivo `can.h` para validar a lógica em bancada:

- `#define testLoopbackCAN1`: Habilita o modo de loopback interno e o simulador de frames BMS (0x01, 0x05, 0x07).
- `#define testLoopbackCAN2`: Habilita a recepção dos dados traduzidos para validação visual.

## 📂 Estrutura do Código Modular

- **`Core/Src/can.c`**: Contém a lógica de processamento (`CAN_ProcessBMSMessage`), simulação (`CAN_SimulateBMS`) e formatação de telemetria (`CAN_TransmitTelemetry`).
- **`Core/Inc/can.h`**: Definição das estruturas de dados (`EMUS_BMS_Data_t`) e macros de configuração.
- **`Core/Src/main.c`**: Gerenciamento de interrupções (Callbacks) e loop de tempo real via TIM1.

---
**Desenvolvido para FSAE Telemetry Standard.**
