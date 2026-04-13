/**
 * @file can.c
 * @brief Implementação robusta de transmissão CAN seguindo o padrão do Master.
 */

#include "can.h"
#include <string.h>

/* ==================== Extern Globals ================================= */
extern FDCAN_HandleTypeDef hfdcan1;
extern FDCAN_HandleTypeDef hfdcan2;

extern FDCAN_TxHeaderTypeDef FDCAN1TxHeader;
extern uint8_t FDCAN1TxData[8];

extern FDCAN_TxHeaderTypeDef FDCAN2TxHeader;
extern uint8_t FDCAN2TxData[8];

/* ==================== Public Variables =============================== */
EMUS_BMS_Data_t emusBmsData = {0};

/* ==================== Private Variables ============================== */
static uint8_t canConsecutiveFailures = 0;

/* ==================== Internal Functions ============================= */
// ... (manteve o sendSingleFrame original)
static CAN_TxStatus_t sendSingleFrame(FDCAN_HandleTypeDef *hfdcan, FDCAN_TxHeaderTypeDef *pHeader, uint8_t *pData) {
    if (hfdcan->State == HAL_FDCAN_STATE_RESET) {
        return CAN_TX_FAIL;
    }

    uint8_t retry = 0;
    while (HAL_FDCAN_AddMessageToTxFifoQ(hfdcan, pHeader, pData) != HAL_OK) {
        HAL_Delay(1); 
        if (++retry >= CAN_TX_RETRY_MAX) {
            canConsecutiveFailures++;
            if (canConsecutiveFailures >= CAN_TX_FAULT_THRESHOLD) {
                return CAN_TX_FATAL;
            }
            return CAN_TX_FAIL;
        }
    }

    canConsecutiveFailures = 0;
    return CAN_TX_OK;
}

/* ==================== Public Functions =============================== */

void CAN_Transmit(FDCAN_HandleTypeDef *hfdcan, uint32_t id, uint8_t *data, uint32_t len, uint32_t idType) {
    FDCAN_TxHeaderTypeDef *pHeader;
    uint8_t *pData;

    if (hfdcan->Instance == FDCAN1) {
        pHeader = &FDCAN1TxHeader;
        pData = FDCAN1TxData;
    } else {
        pHeader = &FDCAN2TxHeader;
        pData = FDCAN2TxData;
    }

    pHeader->Identifier = id;
    pHeader->IdType = idType;
    
    if (len == 0) pHeader->DataLength = FDCAN_DLC_BYTES_0;
    else if (len <= 8) pHeader->DataLength = len;
    else pHeader->DataLength = FDCAN_DLC_BYTES_8;

    memcpy(pData, data, (len > 8) ? 8 : len);

    CAN_TxStatus_t result = sendSingleFrame(hfdcan, pHeader, pData);
    if (result == CAN_TX_FATAL) {
        Error_Handler();
    }
}

/**
 * @brief Decodes EMUS BMS messages into the global emusBmsData structure.
 */
void CAN_ProcessBMSMessage(uint32_t id, uint8_t *data) {
    switch (id) {
        case 0x01: // Battery Voltage Overall Parameters
        {
            // Basis of 2.00V, 0.01V units
            emusBmsData.minCellVoltage = (data[0] * 0.01f) + 2.00f;
            emusBmsData.maxCellVoltage = (data[1] * 0.01f) + 2.00f;
            emusBmsData.avgCellVoltage = (data[2] * 0.01f) + 2.00f;

            // Total Voltage (4 bytes): (Data5 << 24) | (Data6 << 16) | (Data3 << 8) | Data4
            uint32_t rawV = (uint32_t)((data[5] << 24) | (data[6] << 16) | (data[3] << 8) | data[4]);
            emusBmsData.totalVoltage = rawV * 0.01f;
            break;
        }
        case 0x05: // State of Charge parameters
        {
            // Current (16-bit signed, Big Endian): Byte 0 (MSB), Byte 1 (LSB). 0.1A units.
            int16_t rawI = (int16_t)((data[0] << 8) | data[1]);
            emusBmsData.current = rawI * 0.1f;

            // User SOC: Byte 6
            emusBmsData.soc = data[6];
            break;
        }
        case 0x07: // Diagnostic Codes
        {
            // Protection Flags (32-bit, Little Endian): Byte 0 (LSB) to Byte 3 (MSB)
            emusBmsData.protectionFlags = (uint32_t)((data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0]);
            break;
        }
        default:
            break;
    }
    emusBmsData.lastUpdateTick = HAL_GetTick();
}

/**
 * @brief Generates artificial BMS traffic for loopback testing.
 */
void CAN_SimulateBMS(void) {
    uint8_t dummy01[8] = {101, 145, 122, 0x00, 0x70, 0x00, 0x1B, 0x00}; // ~3.01V, ~3.45V, ~3.22V, 705.12V (example)
    uint8_t dummy05[8] = {0x01, 0xF4, 0x00, 0x00, 0x00, 0x00, 80, 0x00};   // 50.0A (charging), 80% SOC
    uint8_t dummy07[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // OK

    CAN_Transmit(&hfdcan1, 0x01, dummy01, 8, FDCAN_STANDARD_ID);
    CAN_Transmit(&hfdcan1, 0x05, dummy05, 8, FDCAN_STANDARD_ID);
    CAN_Transmit(&hfdcan1, 0x07, dummy07, 8, FDCAN_STANDARD_ID);
}

/**
 * @brief Consolidates and transmits processed BMS data to the vehicle bus (CAN2).
 * Formats data as floats (TotalV, Current) and scaled bytes (Cell Voltages, SOC)
 * across two specific telemetry IDs.
 */
void CAN_TransmitTelemetry(void) {
    uint8_t txDataBuffer[8];

    // --- Ferramenta de Simulação (Loopback) ---
#ifdef testLoopbackCAN1
    CAN_SimulateBMS();
#endif

    // 1. ID 0x19308082: Total Voltage (4B Float) + Current (4B Float)
    memcpy(&txDataBuffer[0], &emusBmsData.totalVoltage, 4);
    memcpy(&txDataBuffer[4], &emusBmsData.current, 4);
    CAN_Transmit(&hfdcan2, CANSplitterID1, txDataBuffer, 8, FDCAN_EXTENDED_ID);

    // 2. ID 0x19318082: Protection Flags (4B) + Cell Stats (3B) + SOC (1B)
    // Scale: 0.02V per bit (range 0 to 5.1V) - NO OFFSET
    memcpy(&txDataBuffer[0], &emusBmsData.protectionFlags, 4);
    txDataBuffer[4] = (uint8_t)(emusBmsData.minCellVoltage * 50.0f);
    txDataBuffer[5] = (uint8_t)(emusBmsData.maxCellVoltage * 50.0f);
    txDataBuffer[6] = (uint8_t)(emusBmsData.avgCellVoltage * 50.0f);
    txDataBuffer[7] = emusBmsData.soc;
    CAN_Transmit(&hfdcan2, CANSplitterID2, txDataBuffer, 8, FDCAN_EXTENDED_ID);
}
