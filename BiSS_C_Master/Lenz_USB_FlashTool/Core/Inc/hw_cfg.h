/*!
 * @file hw_cfg.h
 * @author Kirill Rostovskiy (kmrost@lenzencoders.com)
 * @brief Hardware configurator file
 * @version 0.1
 * @copyright Lenz Encoders (c) 2024
 */

#ifndef _HW_CFG_H_
#define _HW_CFG_H_

#ifdef __cplusplus
extern "C" {
#endif


/* BISS C Config*/
static const enum {BISS_MODE_SPI, BISS_MODE_UART} BISS_MODE = BISS_MODE_UART;
static const enum {RS485_ADR1, RS485_ADR2} RS485_ADR = RS485_ADR1;
	
#define BISS_Task_TIM 					TIM7
#define BISS_Task_IRQHandler 		TIM7_IRQHandler
#define BISS_SPI								SPI1
#define BISS_UART								USART1
#define DMA_LPUART_RX 					DMA1, LL_DMA_CHANNEL_1
#define DMA_LPUART_TX 					DMA1, LL_DMA_CHANNEL_2

#define DMA_BISS_RX 						DMA1, LL_DMA_CHANNEL_3
#define DMA_BISS_TX 						DMA1, LL_DMA_CHANNEL_4
#define DMA_BISS_UART_RX				DMA1, LL_DMA_CHANNEL_5

#define BISS_MA_SPI_PIN      		GPIOA, LL_GPIO_PIN_5
#define BISS_SLO_DE_PIN					GPIOA, LL_GPIO_PIN_12 /* RS485 Slave Out (uart in_out) Driver enable pin */
#define BISS_MA_UART_PIN				GPIOA, LL_GPIO_PIN_9
#define BISS_SLO_UART_PIN				GPIOB, LL_GPIO_PIN_7
/* END BISS C Config*/
#define PWR_EN_PIN							GPIOB, LL_GPIO_PIN_0

#ifdef __cplusplus
}
#endif

#endif /* _HW_CFG_H_ */