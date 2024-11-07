/*!
 * @file biss_c_master_hal.c
 * @author Kirill Rostovskiy (kmrost@lenzencoders.com)
 * @brief BiSS C Master Hardware abstraction layer driver
 * @version 0.1
 * @copyright Lenz Encoders (c) 2024
 */

#include "hw_cfg.h"
#include "stm32g4xx.h"
#include "stm32g4xx_it.h"
#include "biss_c_master.h"
#include "biss_c_master_hal.h"
#include "stm32g4xx_ll_tim.h"
#include "stm32g4xx_ll_dma.h"
#include "stm32g4xx_ll_system.h"
#include "stm32g4xx_ll_exti.h"
#include "stm32g4xx_ll_spi.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_usart.h"
#include "uart.h"

#define SPI_CR1_BISS_CDM    SPI_CR1_SSI | SPI_CR1_SPE | SPI_CR1_MSTR | SPI_CR1_SSM | ((0x5U << SPI_CR1_BR_Pos) & SPI_CR1_BR_Msk)
#define SPI_CR1_BISS_nCDM   SPI_CR1_CPOL | SPI_CR1_CPHA | SPI_CR1_BISS_CDM

#define SPI_CR2_BISS_CFG		SPI_CR2_RXDMAEN |SPI_CR2_TXDMAEN | SPI_CR2_FRXTH | (0x7U << SPI_CR2_DS_Pos)

typedef enum{
	RS485_CDM_ADR1_REQ = 0x81U,
	RS485_CDM_ADR2_REQ = 0x22U,
	RS485_CDM_ADR3_REQ = 0x43U,
	RS485_CDM_ADR4_REQ = 0x04U,
	RS485_CDM_ADR5_REQ = 0x65U,
	RS485_CDM_ADR6_REQ = 0xC6U,
	RS485_CDM_ADR7_REQ = 0xA7U,
	RS485_nCDM_ADR1_REQ = 0xB1U,
	RS485_nCDM_ADR2_REQ = 0x12U,
	RS485_nCDM_ADR3_REQ = 0x73U,
	RS485_nCDM_ADR4_REQ = 0x34U,
	RS485_nCDM_ADR5_REQ = 0x55U,
	RS485_nCDM_ADR6_REQ = 0xF6U,
	RS485_nCDM_ADR7_REQ = 0x97U,
}RS485_Req_t;

typedef union{
	volatile uint8_t buf[8];
	struct{
		uint32_t reserv1:24;
		volatile uint32_t CDS:1;
		uint32_t reserv2:7;
		volatile uint32_t revSCD;
	};
}SPI_rx_t;

typedef union{
	volatile uint32_t u32;
	struct{
		uint32_t adr:3;
		uint32_t frame_format:1;
		uint32_t CDS:1;
		uint32_t nW:1;
		uint32_t nE:1;
		uint32_t Pos:19;
		uint32_t _CRC6:6;
	};	
	struct{
		uint32_t Data4CRC:26;
		uint32_t CRC6:6;
	};
}USART_rx_t;

SPI_rx_t SPI_rx;
USART_rx_t USART_rx;
volatile CDS_t USART_CDS_last = CDS;

static const uint8_t CRC6_LUT[256U] = {
	0x00U, 0x0CU, 0x18U, 0x14U, 0x30U, 0x3CU, 0x28U, 0x24U, 0x60U, 0x6CU, 0x78U, 0x74U, 0x50U, 0x5CU, 0x48U, 0x44U, 
	0xC0U, 0xCCU, 0xD8U, 0xD4U, 0xF0U, 0xFCU, 0xE8U, 0xE4U, 0xA0U, 0xACU, 0xB8U, 0xB4U, 0x90U, 0x9CU, 0x88U, 0x84U, 
	0x8CU, 0x80U, 0x94U, 0x98U, 0xBCU, 0xB0U, 0xA4U, 0xA8U, 0xECU, 0xE0U, 0xF4U, 0xF8U, 0xDCU, 0xD0U, 0xC4U, 0xC8U, 
	0x4CU, 0x40U, 0x54U, 0x58U, 0x7CU, 0x70U, 0x64U, 0x68U, 0x2CU, 0x20U, 0x34U, 0x38U, 0x1CU, 0x10U, 0x04U, 0x08U, 
	0x14U, 0x18U, 0x0CU, 0x00U, 0x24U, 0x28U, 0x3CU, 0x30U, 0x74U, 0x78U, 0x6CU, 0x60U, 0x44U, 0x48U, 0x5CU, 0x50U, 
	0xD4U, 0xD8U, 0xCCU, 0xC0U, 0xE4U, 0xE8U, 0xFCU, 0xF0U, 0xB4U, 0xB8U, 0xACU, 0xA0U, 0x84U, 0x88U, 0x9CU, 0x90U, 
	0x98U, 0x94U, 0x80U, 0x8CU, 0xA8U, 0xA4U, 0xB0U, 0xBCU, 0xF8U, 0xF4U, 0xE0U, 0xECU, 0xC8U, 0xC4U, 0xD0U, 0xDCU, 
	0x58U, 0x54U, 0x40U, 0x4CU, 0x68U, 0x64U, 0x70U, 0x7CU, 0x38U, 0x34U, 0x20U, 0x2CU, 0x08U, 0x04U, 0x10U, 0x1CU, 
	0x28U, 0x24U, 0x30U, 0x3CU, 0x18U, 0x14U, 0x00U, 0x0CU, 0x48U, 0x44U, 0x50U, 0x5CU, 0x78U, 0x74U, 0x60U, 0x6CU, 
	0xE8U, 0xE4U, 0xF0U, 0xFCU, 0xD8U, 0xD4U, 0xC0U, 0xCCU, 0x88U, 0x84U, 0x90U, 0x9CU, 0xB8U, 0xB4U, 0xA0U, 0xACU, 
	0xA4U, 0xA8U, 0xBCU, 0xB0U, 0x94U, 0x98U, 0x8CU, 0x80U, 0xC4U, 0xC8U, 0xDCU, 0xD0U, 0xF4U, 0xF8U, 0xECU, 0xE0U, 
	0x64U, 0x68U, 0x7CU, 0x70U, 0x54U, 0x58U, 0x4CU, 0x40U, 0x04U, 0x08U, 0x1CU, 0x10U, 0x34U, 0x38U, 0x2CU, 0x20U, 
	0x3CU, 0x30U, 0x24U, 0x28U, 0x0CU, 0x00U, 0x14U, 0x18U, 0x5CU, 0x50U, 0x44U, 0x48U, 0x6CU, 0x60U, 0x74U, 0x78U, 
	0xFCU, 0xF0U, 0xE4U, 0xE8U, 0xCCU, 0xC0U, 0xD4U, 0xD8U, 0x9CU, 0x90U, 0x84U, 0x88U, 0xACU, 0xA0U, 0xB4U, 0xB8U, 
	0xB0U, 0xBCU, 0xA8U, 0xA4U, 0x80U, 0x8CU, 0x98U, 0x94U, 0xD0U, 0xDCU, 0xC8U, 0xC4U, 0xE0U, 0xECU, 0xF8U, 0xF4U, 
	0x70U, 0x7CU, 0x68U, 0x64U, 0x40U, 0x4CU, 0x58U, 0x54U, 0x10U, 0x1CU, 0x08U, 0x04U, 0x20U, 0x2CU, 0x38U, 0x34U
};

/*!
 * @brief CRC6 calculation function
 * @param data including position data, error and warning bits
 * @return uint8_t CRC6, poly 0x43, inverted
 */
__STATIC_INLINE uint8_t BISS_CRC6_Calc(uint32_t data){	
	uint8_t crc = CRC6_LUT[(data >> 24U) & 0x3U];
	crc = CRC6_LUT[((data >> 16U) & 0xFFU) ^ crc];
	crc = CRC6_LUT[((data >> 8U) & 0xFFU) ^ crc];
	crc = CRC6_LUT[(data & 0xFFU) ^ crc];
	crc = ((crc ^ 0xFFU) >> 2U) & 0x3FU;
	return(crc);
}


volatile uint32_t BISS_SCD;
volatile AngleData_t AngleData;
volatile enum{CRC6_OK,CRC6_FAULT} CRC6_State = CRC6_FAULT;

void BissRequest_CDM(void){
	switch(BISS_MODE){
		case BISS_MODE_SPI:		
			LL_GPIO_SetPinMode(BISS_MA_SPI_PIN, LL_GPIO_MODE_OUTPUT);
			LL_DMA_DisableChannel(DMA_BISS_RX);
			LL_DMA_DisableChannel(DMA_BISS_TX);
			LL_SPI_DeInit(BISS_SPI);
			BISS_SPI->CR1 = SPI_CR1_BISS_CDM;
			BISS_SPI->CR2 = SPI_CR2_BISS_CFG;
			LL_DMA_SetDataLength(DMA_BISS_TX, 5U); // TODO try 1U via define
			LL_DMA_SetDataLength(DMA_BISS_RX, 5U); // TODO try 1U via define
			LL_DMA_EnableChannel(DMA_BISS_TX);	    
			LL_GPIO_SetPinMode(BISS_MA_SPI_PIN, LL_GPIO_MODE_ALTERNATE);
			LL_DMA_EnableChannel(DMA_BISS_RX);	
			break;	
		case BISS_MODE_UART:
			LL_DMA_DisableChannel(DMA_BISS_UART_RX);
			LL_DMA_SetDataLength(DMA_BISS_UART_RX, 4U);
			LL_DMA_EnableChannel(DMA_BISS_UART_RX);	 
			switch(RS485_ADR){
				case RS485_ADR1:
					LL_USART_TransmitData8(BISS_UART, RS485_CDM_ADR1_REQ);
					break;
				case RS485_ADR2:
					LL_USART_TransmitData8(BISS_UART, RS485_CDM_ADR2_REQ);
					break;
			}
			break;		
	}		
}

void BissRequest_nCDM(void){
	switch(BISS_MODE){
		case BISS_MODE_SPI:		
			LL_DMA_DisableChannel(DMA_BISS_RX);
			LL_DMA_DisableChannel(DMA_BISS_TX);
			LL_SPI_DeInit(BISS_SPI);
			BISS_SPI->CR1 = SPI_CR1_BISS_nCDM;
			BISS_SPI->CR2 = SPI_CR2_BISS_CFG;
			LL_DMA_SetDataLength(DMA_BISS_TX, 5U); // TODO try 1U via define
			LL_DMA_SetDataLength(DMA_BISS_RX, 5U); // TODO try 1U via define
			LL_DMA_EnableChannel(DMA_BISS_TX);	 
			LL_DMA_EnableChannel(DMA_BISS_RX);	
			break;	
		case BISS_MODE_UART:
			LL_DMA_DisableChannel(DMA_BISS_UART_RX);
			LL_DMA_SetDataLength(DMA_BISS_UART_RX, 4U);
			LL_DMA_EnableChannel(DMA_BISS_UART_RX);	 
			switch(RS485_ADR){
				case RS485_ADR1:
					LL_USART_TransmitData8(BISS_UART, RS485_nCDM_ADR1_REQ);
					break;
				case RS485_ADR2:
					LL_USART_TransmitData8(BISS_UART, RS485_nCDM_ADR2_REQ);
					break;
			}
			break;		
	}		
}

volatile CDM_t last_CDM = CDM;

void BISS_Task_IRQHandler(void) {
	LL_TIM_ClearFlag_UPDATE(BISS_Task_TIM);
	switch(BISS_MODE){
		case BISS_MODE_SPI:		
			BISS_SCD = __REV(SPI_rx.revSCD);
			if(BISS_CRC6_Calc(BISS_SCD >> 6) == (BISS_SCD & 0x3FU)){
				CRC6_State = CRC6_OK;
				AngleData.angle_data = BISS_SCD >> 8;
				AngleData.time_of_life_counter++;
			}
			else{
				CRC6_State = CRC6_FAULT;
			}				
			if (BiSS_C_Master_StateMachine(SPI_rx.CDS) == CDM) {
				BissRequest_CDM();
			}
			else {
				BissRequest_nCDM();
			}	
			break;
		case BISS_MODE_UART:		
			if(LL_DMA_GetDataLength(DMA_BISS_UART_RX) == 0){
				if(BISS_CRC6_Calc(USART_rx.Data4CRC) == USART_rx.CRC6){
					CRC6_State = CRC6_OK;
					AngleData.angle_data = USART_rx.Pos;
					AngleData.time_of_life_counter++;
				}
				else{
					CRC6_State = CRC6_FAULT;
				}	
				if (BiSS_C_Master_StateMachine(USART_CDS_last) == CDM) {
					BissRequest_CDM();
					last_CDM = CDM;
				}
				else {
					BissRequest_nCDM();
					last_CDM = nCDM;
				}	
				USART_CDS_last = (CDS_t)USART_rx.CDS;
			}
			else{
				if(last_CDM == CDM){
					BissRequest_CDM();
				}
				else {
					BissRequest_nCDM();
				}						
			}
			break;
	}
	UART_StateMachine();
}

void BiSS_C_Master_HAL_Init(void){
	switch(BISS_MODE){
		case BISS_MODE_SPI:			
			LL_GPIO_SetPinMode(BISS_MA_UART_PIN, LL_GPIO_MODE_INPUT);
			LL_GPIO_SetPinMode(BISS_MA_SPI_PIN, LL_GPIO_MODE_ALTERNATE);
			LL_GPIO_SetOutputPin(BISS_MA_SPI_PIN);
			LL_GPIO_SetOutputPin(PWR_EN_PIN);
			LL_GPIO_ResetOutputPin(BISS_SLO_DE_PIN);
			LL_DMA_SetPeriphAddress(DMA_BISS_RX, (uint32_t) &BISS_SPI->DR);
			LL_DMA_SetMemoryAddress(DMA_BISS_RX, (uint32_t) &SPI_rx.buf[3]);
			LL_DMA_SetDataLength(DMA_BISS_RX, 5);	
			LL_DMA_SetPeriphAddress(DMA_BISS_TX, (uint32_t) &BISS_SPI->DR);
			LL_DMA_SetDataLength(DMA_BISS_TX, 5);	
			LL_SPI_EnableDMAReq_TX(BISS_SPI);
			LL_SPI_EnableDMAReq_RX(BISS_SPI);
			LL_SPI_Enable(BISS_SPI);
			LL_DMA_EnableChannel(DMA_BISS_RX);
			break;
		case BISS_MODE_UART:
			LL_GPIO_SetOutputPin(PWR_EN_PIN);
			LL_USART_Disable(BISS_UART);		
			LL_GPIO_SetPinMode(BISS_MA_SPI_PIN, LL_GPIO_MODE_INPUT);
			LL_GPIO_SetPinMode(BISS_MA_UART_PIN, LL_GPIO_MODE_ALTERNATE);
			LL_DMA_SetPeriphAddress(DMA_BISS_UART_RX, (uint32_t) &BISS_UART->RDR);
			LL_DMA_SetMemoryAddress(DMA_BISS_UART_RX, (uint32_t) &USART_rx.u32);
			LL_DMA_SetDataLength(DMA_BISS_UART_RX, 4);	
			LL_USART_EnableDMAReq_RX(BISS_UART);
			LL_USART_SetDEAssertionTime(BISS_UART, 16U);
			LL_USART_SetDEDeassertionTime(BISS_UART, 16U);
			LL_USART_Enable(BISS_UART);		
			LL_DMA_EnableChannel(DMA_BISS_UART_RX);
			break;		
	}
}

