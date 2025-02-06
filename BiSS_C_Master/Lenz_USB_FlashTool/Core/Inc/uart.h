/*!
 * @file biss_c_master.h
 * @author Kirill Rostovskiy (kmrost@lenzencoders.com)
 * @brief BiSS C Master library
 * @version 0.1
 * @copyright Lenz Encoders (c) 2024
 */

#ifndef __UART_H
#define __UART_H
#ifdef __cplusplus
extern "C" {
#endif
	
#include "stdint.h"
#include "hw_cfg.h"

//move to c

#define TX_BUFFER_SIZE 		252U

typedef enum{
	UART_COMMAND_WRITE_BANK = 0x00,
	UART_COMMAND_WRITE_CRC  = 0x03U,
	UART_COMMAND_PAGE       = 0x04U,
	UART_COMMAND_POWER_OFF  = 0x0BU,
	UART_COMMAND_POWER_ON   = 0x0CU,
	UART_COMMAND_WRITE_REG  = 0x0DU,
	UART_COMMAND_READ_ANGLE = 0x81U,
	UART_COMMAND_READ_REG 	= 0x82U,
}UART_Command_t;

typedef struct {
	uint8_t len;
	uint8_t adr_h;
	uint8_t adr_l;
	UART_Command_t cmd;
	uint8_t Buf[TX_BUFFER_SIZE];
}UartTxStr_t;


extern UartTxStr_t UART_TX;
//End move

//void UART_Init(void);
void UART_Transmit(UartTxStr_t *TxStr);

uint8_t check_request(uint8_t *received_data);
// Begin init Renishaw
void InitRenishaw(void);
// End init Renishaw
void InitUart(void);
void UART_StateMachine (void);
	
	
#ifdef __cplusplus
}
#endif

#endif /* __UART_H */