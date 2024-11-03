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
#include "uart.h"

#define SPI_CR1_BISS_CDM    SPI_CR1_SSI | SPI_CR1_SPE | SPI_CR1_MSTR | SPI_CR1_SSM | ((0x5U << SPI_CR1_BR_Pos) & SPI_CR1_BR_Msk)
#define SPI_CR1_BISS_nCDM   SPI_CR1_CPOL | SPI_CR1_CPHA | SPI_CR1_BISS_CDM

#define SPI_CR2_BISS_CFG		SPI_CR2_RXDMAEN |SPI_CR2_TXDMAEN | SPI_CR2_FRXTH | (0x7U << SPI_CR2_DS_Pos)

typedef union{
	volatile uint8_t buf[8];
	struct{
		uint32_t reserv1:24;
		volatile uint32_t CDS:1;
		uint32_t reserv2:7;
		volatile uint32_t revSCD;
	};
}SPI_rx_t;

SPI_rx_t SPI_rx;

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
    LL_GPIO_SetPinMode(BISS_CLK_PIN, LL_GPIO_MODE_OUTPUT);
	LL_DMA_DisableChannel(DMA_BISS_RX);
	LL_DMA_DisableChannel(DMA_BISS_TX);
	LL_SPI_DeInit(BISS_SPI);
	BISS_SPI->CR1 = SPI_CR1_BISS_CDM;
	BISS_SPI->CR2 = SPI_CR2_BISS_CFG;
	LL_DMA_SetDataLength(DMA_BISS_TX, 5U); // TODO try 1U via define
	LL_DMA_SetDataLength(DMA_BISS_RX, 5U); // TODO try 1U via define
	LL_DMA_EnableChannel(DMA_BISS_TX);	    
  LL_GPIO_SetPinMode(BISS_CLK_PIN, LL_GPIO_MODE_ALTERNATE);
	LL_DMA_EnableChannel(DMA_BISS_RX);	
}

void BissRequest_nCDM(void){
	LL_DMA_DisableChannel(DMA_BISS_RX);
	LL_DMA_DisableChannel(DMA_BISS_TX);
	LL_SPI_DeInit(BISS_SPI);
	BISS_SPI->CR1 = SPI_CR1_BISS_nCDM;
	BISS_SPI->CR2 = SPI_CR2_BISS_CFG;
	LL_DMA_SetDataLength(DMA_BISS_TX, 5U); // TODO try 1U via define
	LL_DMA_SetDataLength(DMA_BISS_RX, 5U); // TODO try 1U via define
	LL_DMA_EnableChannel(DMA_BISS_TX);	 
	LL_DMA_EnableChannel(DMA_BISS_RX);	   
}

void BISS_Task_IRQHandler(void) {
	LL_TIM_ClearFlag_UPDATE(BISS_Task_TIM);
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
	UART_StateMachine();
}

void BiSS_C_Master_HAL_Init(void){
	LL_GPIO_SetOutputPin(BISS_CLK_PIN);
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
}

