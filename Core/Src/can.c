/**
 * @file can.c
 * @brief Logic for robust CAN transmission and BMS data processing.
 */

#include "can.h"
#include <string.h>

/* ==================== Externs ======================================== */
extern FDCAN_HandleTypeDef hfdcan1;
extern FDCAN_HandleTypeDef hfdcan2;

/* ==================== Global State =================================== */
EMUS_BMS_Data_t emusBmsData = {0};
volatile uint8_t txTelemetryRequest = 0;

// Variável de debug
uint32_t cellMsgCount = 0;

static uint32_t canConsecutiveFailures = 0;

/* Cell voltage encoding: byte = (V - 2.0) * 100, clamped to valid uint8 range. */
static inline uint8_t voltage_to_byte(float v) {
    if (v < 2.0f)  return 0;
    if (v > 4.55f) return 255;
    return (uint8_t)((v - 2.0f) * 100.0f);
}

/* ==================== Core Functions ================================= */

/**
 * @brief Transmits a CAN frame with a simple retry mechanism.
 * @retval CAN_TX_OK on success, CAN_TX_FAIL after retry exhaustion,
 *         CAN_TX_FATAL after CAN_TX_FAULT_THRESHOLD consecutive failures.
 */
CAN_TxStatus_t CAN_Transmit(FDCAN_HandleTypeDef *hfdcan, uint32_t id, uint8_t *data, uint32_t len, uint32_t idType) {
    FDCAN_TxHeaderTypeDef TxHeader;
    FDCAN_TxHeaderTypeDef *pHeader = &TxHeader;
    uint8_t pData[8] = {0};

    pHeader->Identifier = id;
    pHeader->IdType = idType;
    pHeader->TxFrameType = FDCAN_DATA_FRAME;
    pHeader->ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    pHeader->BitRateSwitch = FDCAN_BRS_OFF;
    pHeader->FDFormat = FDCAN_CLASSIC_CAN;
    pHeader->TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    pHeader->MessageMarker = 0;

    switch (len) {
        case 0: pHeader->DataLength = FDCAN_DLC_BYTES_0; break;
        case 1: pHeader->DataLength = FDCAN_DLC_BYTES_1; break;
        case 2: pHeader->DataLength = FDCAN_DLC_BYTES_2; break;
        case 3: pHeader->DataLength = FDCAN_DLC_BYTES_3; break;
        case 4: pHeader->DataLength = FDCAN_DLC_BYTES_4; break;
        case 5: pHeader->DataLength = FDCAN_DLC_BYTES_5; break;
        case 6: pHeader->DataLength = FDCAN_DLC_BYTES_6; break;
        case 7: pHeader->DataLength = FDCAN_DLC_BYTES_7; break;
        default: pHeader->DataLength = FDCAN_DLC_BYTES_8; break;
    }

    memcpy(pData, data, (len > 8) ? 8 : len);

    uint32_t retry = 0;
    while (HAL_FDCAN_AddMessageToTxFifoQ(hfdcan, pHeader, pData) != HAL_OK) {
        for(volatile int i=0; i<200; i++);
        if (++retry >= CAN_TX_RETRY_MAX) {
            canConsecutiveFailures++;
            return (canConsecutiveFailures >= CAN_TX_FAULT_THRESHOLD) ? CAN_TX_FATAL : CAN_TX_FAIL;
        }
    }
    canConsecutiveFailures = 0;
    return CAN_TX_OK;
}

/**
 * @brief Decodes raw BMS messages into global state.
 */
void CAN_ProcessBMSMessage(uint32_t id, uint8_t *data, uint8_t dlc) {
    uint32_t cleanId = id & 0x7FF;

    // 1. Individual Cell Voltages (IDs 0x20 a 0x3F)
    if (cleanId >= CAN_ID_BMS_CELL_VOLT_START && cleanId <= CAN_ID_BMS_CELL_VOLT_END) {
        uint8_t group = cleanId - CAN_ID_BMS_CELL_VOLT_START;
        uint16_t startIndex = group * 8;
        
        if (startIndex < 40) {
            cellMsgCount++;
            for (uint8_t i = 0; i < 8; i++) {
                uint16_t idx = startIndex + i;
                if (idx < 40) {
                    emusBmsData.cellVoltages[idx] = (data[i] * 0.01f) + 2.00f;
                }
            }
        }
        emusBmsData.lastUpdateTick = HAL_GetTick();
        return;
    }

    // 2. Outros parâmetros
    if (cleanId == CAN_ID_BMS_OVERALL_PARAMS) {
        emusBmsData.minCellVoltage = (data[0] * 0.01f) + 2.00f;
        emusBmsData.maxCellVoltage = (data[1] * 0.01f) + 2.00f;
        emusBmsData.avgCellVoltage = (data[2] * 0.01f) + 2.00f;
        uint32_t rawV = (uint32_t)((data[5] << 24) | (data[6] << 16) | (data[3] << 8) | data[4]);
        emusBmsData.totalVoltage = rawV * 0.01f;
    } 
    else if (cleanId == CAN_ID_BMS_SOC) {
        // Corrente agora lida exclusivamente via Sensor Externo (ID 0x521)
        // Clamp: BMS pode reportar 0xFF como "inválido"; força 0 para que derating de jusante atue.
        uint8_t rawSoc = data[6];
        emusBmsData.soc = (rawSoc <= 100) ? rawSoc : 0;
    }
    else if (cleanId == CAN_ID_BMS_DIAGNOSTICS) {
        emusBmsData.protectionFlags = (uint32_t)((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]);
    }
    else if (cleanId == CAN_ID_SENSOR_CURRENT) {
        // Sensor Externo: MSB no byte 5, LSB no byte 2. Escala 0.001A (1mA/bit)
        int32_t rawCurrent = (int32_t)((data[5] << 24) | (data[4] << 16) | (data[3] << 8) | data[2]);
        emusBmsData.current = (float)rawCurrent * 0.001f;
    }

    emusBmsData.lastUpdateTick = HAL_GetTick();
}

/**
 * @brief Generates artificial BMS traffic for loopback testing.
 */
void CAN_SimulateBMS(void) {
    static uint8_t simStep = 0;
    static uint8_t tracker = 0;
    uint8_t dummy01[8] = {160, 160, 160, 0x38, 0x40, 0x00, 0x00, 0x00};
    uint8_t dummy05[8] = {0x01, 0xF4, 0x00, 0x00, 0x00, 0x00, 80, 0x00};
    uint8_t dummy07[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t dummyCell[8] = {160, 160, 160, 160, 160, 160, 160, 160};
    uint8_t dummySensor[8] = {0x00, tracker, 0x56, 0x08, 0x01, 0x00, 0x00, 0x00}; // 67.67A (67670 mA)

    switch (simStep) {
        case 0: CAN_Transmit(&hfdcan1, CAN_ID_BMS_OVERALL_PARAMS, dummy01, 8, FDCAN_STANDARD_ID); break;
        case 1: CAN_Transmit(&hfdcan1, CAN_ID_BMS_SOC, dummy05, 8, FDCAN_STANDARD_ID); break;
        case 2: CAN_Transmit(&hfdcan1, CAN_ID_BMS_DIAGNOSTICS, dummy07, 8, FDCAN_STANDARD_ID); break;
        case 3: CAN_Transmit(&hfdcan1, CAN_ID_BMS_CELL_VOLT_START + 0, dummyCell, 8, FDCAN_STANDARD_ID); break;
        case 4: CAN_Transmit(&hfdcan1, CAN_ID_BMS_CELL_VOLT_START + 1, dummyCell, 8, FDCAN_STANDARD_ID); break;
        case 5: CAN_Transmit(&hfdcan1, CAN_ID_BMS_CELL_VOLT_START + 2, dummyCell, 8, FDCAN_STANDARD_ID); break;
        case 6: CAN_Transmit(&hfdcan1, CAN_ID_BMS_CELL_VOLT_START + 3, dummyCell, 8, FDCAN_STANDARD_ID); break;
        case 7: CAN_Transmit(&hfdcan1, CAN_ID_BMS_CELL_VOLT_START + 4, dummyCell, 8, FDCAN_STANDARD_ID); break;
        case 8: CAN_Transmit(&hfdcan1, CAN_ID_SENSOR_CURRENT, dummySensor, 6, FDCAN_STANDARD_ID); break;
    }
    simStep = (simStep + 1) % 9;
    tracker = (tracker + 1) % 0x10;
}

/**
 * @brief Consolidates and transmits processed BMS data to vehicle bus (CAN2).
 *        Should be called from main loop (not ISR) — retry busy-wait would
 *        otherwise starve FDCAN1 RX.
 */
void CAN_TransmitTelemetry(void) {
    uint8_t txDataBuffer[8];

#ifdef testLoopbackCAN1
    CAN_SimulateBMS();
#endif

    /* Boot guard: lastUpdateTick == 0 means we never received a BMS frame yet,
     * so treat as timeout and skip TX of stale zeros. */
    if (emusBmsData.lastUpdateTick == 0 ||
        (HAL_GetTick() - emusBmsData.lastUpdateTick) > 3000) {
        memset(txDataBuffer, 0, 8);
        txDataBuffer[0] = ERROR_CODE_BMS_LOST;
        if (CAN_Transmit(&hfdcan2, CAN_ID_BMS_TIMEOUT, txDataBuffer, 8, FDCAN_EXTENDED_ID) == CAN_TX_FATAL) {
            Error_Handler();
        }
        return;
    }

    // 1. ID Telemetria 1 (Voltagem e Corrente)
    memcpy(&txDataBuffer[0], &emusBmsData.totalVoltage, 4);
    memcpy(&txDataBuffer[4], &emusBmsData.current, 4);
    if (CAN_Transmit(&hfdcan2, CANSplitterID1, txDataBuffer, 8, FDCAN_EXTENDED_ID) == CAN_TX_FATAL) {
        Error_Handler();
    }

    // 2. ID Telemetria 2 (Stats e SOC)
    memcpy(&txDataBuffer[0], &emusBmsData.protectionFlags, 4);
    txDataBuffer[4] = voltage_to_byte(emusBmsData.minCellVoltage);
    txDataBuffer[5] = voltage_to_byte(emusBmsData.maxCellVoltage);
    txDataBuffer[6] = voltage_to_byte(emusBmsData.avgCellVoltage);
    txDataBuffer[7] = emusBmsData.soc;
    if (CAN_Transmit(&hfdcan2, CANSplitterID2, txDataBuffer, 8, FDCAN_EXTENDED_ID) == CAN_TX_FATAL) {
        Error_Handler();
    }

    // 3. Telemetria Células (CAN2)
    uint32_t cellIDs[5] = {CANSplitterID4, CANSplitterID5, CANSplitterID6, CANSplitterID7, CANSplitterID8};
    for (uint8_t group = 0; group < 5; group++) {
        for (uint8_t i = 0; i < 8; i++) {
            txDataBuffer[i] = voltage_to_byte(emusBmsData.cellVoltages[group * 8 + i]);
        }
        if (CAN_Transmit(&hfdcan2, cellIDs[group], txDataBuffer, 8, FDCAN_EXTENDED_ID) == CAN_TX_FATAL) {
            Error_Handler();
        }
    }
}
