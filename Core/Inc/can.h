/**
 * @file can.h
 * @brief Header for robust CAN transmission matching Master architecture.
 */

#ifndef __CAN_H
#define __CAN_H

#include "main.h"

/* ==================== Configuração de Teste ========================== */

/** @brief Ativa a simulação interna da BMS (Envio de frames 0x01, 0x05, 0x07 no CAN1) */
#define testLoopbackCAN1
#define testLoopbackCAN2
/* ==================== Defines de Sistema ============================= */

/** @brief Maximum retries for adding message to TX FIFO */
#define CAN_TX_RETRY_MAX 1000

/** @brief Consecutive failures before triggering a fatal system fault */
#define CAN_TX_FAULT_THRESHOLD 10

/** @brief Incoming BMS Message IDs (Standard IDs) */
#define CAN_ID_BMS_OVERALL_PARAMS  0x01
#define CAN_ID_BMS_SOC             0x05
#define CAN_ID_BMS_DIAGNOSTICS     0x07
#define CAN_ID_BMS_CELL_VOLT_START 0x20
#define CAN_ID_BMS_CELL_VOLT_END   0x3F

/** @brief Extended IDs for Vehicle Telemetry (CAN2) */
#define CANSplitterID1  0x15408081
#define CANSplitterID2  0x15418081
#define CANSplitterID4  0x15438081
#define CANSplitterID5  0x15448081
#define CANSplitterID6  0x15458081
#define CANSplitterID7  0x15468081
#define CANSplitterID8  0x15478081

/** @brief Error/Timeout IDs */
#define CAN_ID_BMS_TIMEOUT  0x0D428081
#define ERROR_CODE_BMS_LOST 1<<0

/* ==================== Types ========================================== */

/** @brief Status of a CAN transmission attempt */
typedef enum {
    CAN_TX_OK,      /**< Success */
    CAN_TX_FAIL,    /**< Drop current frame after retries */
    CAN_TX_FATAL    /**< Persistent failure: Safety Shutdown required */
} CAN_TxStatus_t;

/**
 * @brief Structure to hold decoded BMS data in physical units.
 */
typedef struct {
    float minCellVoltage;
    float maxCellVoltage;
    float avgCellVoltage;
    float totalVoltage;
    float current;
    uint8_t soc;
    uint32_t protectionFlags;
    uint32_t lastUpdateTick;
    float cellVoltages[40]; 
} EMUS_BMS_Data_t;

/* ==================== Prototypes ===================================== */

void CAN_Transmit(FDCAN_HandleTypeDef *hfdcan, uint32_t id, uint8_t *data, uint32_t len, uint32_t idType);
void CAN_ProcessBMSMessage(uint32_t id, uint8_t *data, uint8_t dlc);
void CAN_SimulateBMS(void);
void CAN_TransmitTelemetry(void);

#endif /* __CAN_H */
