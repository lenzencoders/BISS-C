/*!
 * @file biss_c_master.c
 * @author Kirill Rostovskiy (kmrost@lenzencoders.com)
 * @brief UART library
 * @version 0.1
 * @copyright Lenz Encoders (c) 2024
 */
#include "uart.h"
 
#include "stm32g4xx_ll_lpuart.h"
#include "stm32g4xx_ll_dma.h"
#include "stm32g4xx_ll_tim.h"
#include "stm32g4xx_ll_gpio.h"
#include "string.h"
#include "biss_c_master.h"

#define RX_BUFFER_SIZE 		256U
//#define UART_LINE_SIZE		133U
#define HEXLEN_ADR_CMD_CRC_LEN	5U // Length of data (1) + Address (2) + Cmd (1) + CRC (1) bytes
#define HEX_DATA_LEN	128U
#define UART_LINE_SIZE		HEXLEN_ADR_CMD_CRC_LEN + HEX_DATA_LEN
#define QUEUE_SIZE 	36U //FIFO
#define MAX_RETRY		3U
#define UART_ANGLE_LEN 	40U //60U --> Encoder / 40U --> Encoder + Renishaw
#define UART_ANGLE_BUF_SIZE 	(UART_ANGLE_LEN * 6U) // *4U --> Encoder / *(4U + 2U) --> Encoder + Renishaw
#define PAGE_ADDR       0x18U
#define BSEL_ADDR		    0x40U
#define FIRST_USER_BANK 0x05U

static void EncoderPowerEnable(void){
	LL_GPIO_SetOutputPin(PWR2_EN_PIN);
}

static void EncoderPowerDisable(void){
	LL_GPIO_ResetOutputPin(PWR2_EN_PIN);
}
/**
 * @brief Renishaw Encoder power enable
**/
static void RenishawEncoderPowerEnable(void){
	LL_GPIO_SetOutputPin(PWR1_EN_PIN);
}
/**
 * @brief Renishaw Encoder power disable
**/
static void RenishawEncoderPowerDisable(void){
	LL_GPIO_ResetOutputPin(PWR1_EN_PIN);
}

typedef enum {
	UART_STATE_IDLE,
	UART_STATE_RECEIVE,
	UART_STATE_SEND,
	UART_STATE_CHECKCRC,
	UART_STATE_RUNCMD,
	UART_STATE_ANGLE_READING,
	UART_STATE_ABORT,
} UART_State_t;

typedef enum {
	UART_ERROR_NONE = 0x00U,
	UART_ERROR_CRC = 0x01U,
	UART_ERROR_QUEUE_FULL = 0x02U,
	UART_ERROR_BISS_WRITE_FAULT = 0x03U,
} UART_Error_t;

volatile enum{
	CRC_OK,
	CRC_FAULT
} CRC_State = CRC_FAULT;

typedef enum{
	QUEUE_OK,
	QUEUE_FULL
}QUEUE_Status_t;

typedef struct {
	uint8_t len;
	uint16_t addr;
	UART_Command_t cmd;
	uint8_t data[HEX_DATA_LEN];

} CommandQueue_t;



volatile struct{
	AngleData_t AngleFIFO[256];
	uint16_t len;
	uint8_t ToL_cnt;
	uint8_t FIFO_start_ptr;
	uint8_t FIFO_current_ptr;
}ReadingStr;

// DEBUG BEGIN
// INSERT AngleDataRenishaw_t to ReadingStr
/*
volatile struct{
	AngleData_t AngleFIFO[256];
	AngleDataRenishaw_t AngleFIFO[256];
	uint16_t len;
	uint8_t ToL_cnt;
	uint8_t FIFO_start_ptr;
	uint8_t FIFO_current_ptr;
}ReadingStr;
*/
// DEBUG END
volatile struct{
	AngleDataRenishaw_t AngleFIFO[256];
	uint16_t len;
	uint8_t ToL_cnt;
	uint8_t FIFO_start_ptr;
	uint8_t FIFO_current_ptr;
}ReadingRenishawStr;

UartTxStr_t UART_TX;
UartTxStr_t UART_RENISHAW_TX;
UART_Error_t UART_Error = UART_ERROR_NONE;

volatile uint8_t usb_rx_buffer[RX_BUFFER_SIZE] = {0};
uint8_t usb_tx_buffer[TX_BUFFER_SIZE] = {0};
uint8_t hex_line_buffer[UART_LINE_SIZE] = {0};
UART_State_t UART_State = UART_STATE_IDLE;

uint32_t dma_rx_cnt = 0; 
volatile uint32_t uart_expected_length = 0; 

volatile uint8_t uart_length = 0;
volatile uint32_t new_cnt = 0;
 
volatile UART_Command_t UART_Command = 0; 
CommandQueue_t CommandQueue[QUEUE_SIZE];

// DEBUG BEGIN
// uint16_t current_readingStr_len = 0;
uint32_t remaining_data = 0;
// DEBUG END
uint8_t queue_read_cnt = 0;
uint8_t queue_write_cnt = 0;
uint8_t queue_cnt = 0;
uint8_t retry_cnt = 0;

QUEUE_Status_t EnqueueCommand(UART_Command_t cmd, uint16_t addr, uint8_t len,	uint8_t *data) {
	if (queue_cnt < QUEUE_SIZE){
		CommandQueue[queue_write_cnt].cmd = cmd;
		CommandQueue[queue_write_cnt].addr = addr;
		CommandQueue[queue_write_cnt].len = len;
		memcpy(CommandQueue[queue_write_cnt].data, data, len);
		queue_write_cnt = (queue_write_cnt + 1U) % QUEUE_SIZE;
		queue_cnt++;
		return QUEUE_OK;
	}
	return QUEUE_FULL;
}

QUEUE_Status_t EnqueueCommandToBegining(UART_Command_t cmd, uint16_t addr, uint8_t len,	uint8_t *data) {
    if (queue_cnt < QUEUE_SIZE) {
			queue_read_cnt = (queue_read_cnt + QUEUE_SIZE - 1U) % QUEUE_SIZE;
			CommandQueue[queue_read_cnt].cmd = cmd;
			CommandQueue[queue_read_cnt].addr = addr;
			CommandQueue[queue_read_cnt].len = len;
			memcpy(CommandQueue[queue_read_cnt].data, data, len);
			queue_cnt++;
			return QUEUE_OK;
    }
    return QUEUE_FULL;
}



void InitUart(void){
	LL_TIM_EnableIT_UPDATE(BISS_Task_TIM);
	LL_TIM_EnableCounter(BISS_Task_TIM);
	LL_DMA_SetMemoryAddress(DMA_LPUART_RX, (uint32_t)usb_rx_buffer);
	LL_DMA_SetPeriphAddress(DMA_LPUART_RX, (uint32_t)&LPUART1->RDR);
	LL_DMA_SetMemoryAddress(DMA_LPUART_TX, (uint32_t)usb_tx_buffer);
	LL_DMA_SetPeriphAddress(DMA_LPUART_TX, (uint32_t)&LPUART1->TDR);
	LL_DMA_SetDataLength(DMA_LPUART_RX, RX_BUFFER_SIZE);
	LL_LPUART_EnableDMAReq_RX(LPUART1);
	LL_LPUART_EnableDMAReq_TX(LPUART1);
	LL_DMA_EnableChannel(DMA_LPUART_RX);
}
/**
 * @brief Initialization of Renishaw Encoder by TIM_RENISHAW TIM3 --> hw_cdg.h
**/
void InitRenishaw(void){
	LL_GPIO_ResetOutputPin(DE1_PIN); // Need to think;  GPIOA, LL_GPIO_PIN_10 to low state
	LL_GPIO_SetOutputPin(PWR1_EN_PIN); // Need to think; GPIOB, LL_GPIO_PIN_0 to high state
	LL_TIM_EnableCounter(TIM_RENISHAW);
}


uint8_t CalculateCRC(uint8_t *data, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < length; i++) {
        sum += data[i];
    }
    uint8_t lsb = sum & 0xFF;
    return (uint8_t)(~lsb + 1);
}

uint8_t CalculateCRCCircularBuffer(uint8_t *buffer, uint16_t buffer_size, uint8_t start_index, uint8_t length) {
     uint8_t sum = 0;
    for (uint8_t i = 0; i < length; i++) {
        uint8_t index = (start_index + i) % buffer_size;
        sum += buffer[index];
    }
    uint8_t lsb = sum & 0xFF;
    return (uint8_t)(~lsb + 1);
}

void UART_Transmit(UartTxStr_t *TxStr) { //*ptr to struct
	uint8_t size = TxStr->len;
	if (size > TX_BUFFER_SIZE) {
		size = TX_BUFFER_SIZE; // handle error
	}
	LL_DMA_DisableChannel(DMA_LPUART_TX);
	LL_DMA_SetDataLength(DMA_LPUART_TX, size + 5U); //1U for CRC additional byte
	//len, addr_h, addr_l, cmd
	memcpy(usb_tx_buffer, TxStr, size + 4U);
	usb_tx_buffer[3] += 0x10U;
	uint8_t crc = CalculateCRC(usb_tx_buffer, size + 4U);
	usb_tx_buffer[size + 4U] = crc;
	LL_DMA_EnableChannel(DMA_LPUART_TX);
}

void UART_StateMachine(void) {
    uint8_t crc;
		uint8_t calculated_crc;
    uint32_t new_cnt;
		if(IsBiSSReqBusy() ==	BISS_READ_FINISHED) {
				UART_Transmit(&UART_TX);
				BiSSResetExternalState();
		}


    switch (UART_State) {
				uint8_t bytes_received;
        case UART_STATE_IDLE:
//						if (IsBiSSReqBusy() != BISS_READ_FINISHED){
//							UART_Transmit(read_buf, RX_BUFFER_SIZE);
//						}
           new_cnt = RX_BUFFER_SIZE - LL_DMA_GetDataLength(DMA_LPUART_RX);
					 // DEBUG BEGIN
					 remaining_data = LL_DMA_GetDataLength(DMA_LPUART_RX);
				   // DEBUG END
           if (dma_rx_cnt != new_cnt) {
								
								bytes_received = (new_cnt - dma_rx_cnt + RX_BUFFER_SIZE) % RX_BUFFER_SIZE;
								uart_length = usb_rx_buffer[dma_rx_cnt];
								uart_expected_length = uart_length + HEXLEN_ADR_CMD_CRC_LEN;
								if (bytes_received >= uart_expected_length) {
									UART_State = UART_STATE_RECEIVE;

//                dma_rx_cnt %= RX_BUFFER_SIZE;
//                uart_length = usb_rx_buffer[dma_rx_cnt];
//                uart_expected_length = uart_length + HEXLEN_ADR_CMD_CRC_LEN;
//                UART_State = UART_STATE_RECEIVE;
//            //change
								}
						}
						else {
							if (queue_cnt > 0){
								UART_State = UART_STATE_RUNCMD;
							}
						}
						break;

        case UART_STATE_RECEIVE:
						memset(hex_line_buffer, 0, UART_LINE_SIZE);
						if (uart_length > 0) {		
							uint8_t crc_position = (dma_rx_cnt + uart_expected_length - 1U) % RX_BUFFER_SIZE;
							crc = usb_rx_buffer[crc_position];
							calculated_crc = CalculateCRCCircularBuffer((uint8_t *)usb_rx_buffer, RX_BUFFER_SIZE, dma_rx_cnt, uart_expected_length - 1U);
            
							if (crc == calculated_crc) {
                if (dma_rx_cnt + uart_expected_length <= RX_BUFFER_SIZE) {
                    memcpy(hex_line_buffer, (uint8_t *)&usb_rx_buffer[dma_rx_cnt], uart_expected_length);
                } else {
                    uint32_t part_size = RX_BUFFER_SIZE - dma_rx_cnt;
                    memcpy(hex_line_buffer, (uint8_t *)&usb_rx_buffer[dma_rx_cnt], part_size);
                    memcpy(hex_line_buffer + part_size, (uint8_t *)usb_rx_buffer, uart_expected_length - part_size);
                }

								uint8_t cmd_data_len = hex_line_buffer[0];
								uint16_t cmd_addr = (hex_line_buffer[1] << 8) | hex_line_buffer[2];
								UART_Command_t command = hex_line_buffer[3];
								uint8_t *cmd_data = &hex_line_buffer[4];	
										
								if (EnqueueCommand(command, cmd_addr, cmd_data_len, cmd_data) == QUEUE_OK){
									UART_State = UART_STATE_RUNCMD;
								}
								else {
									UART_Error = UART_ERROR_QUEUE_FULL;
									UART_State = UART_STATE_ABORT;  
								}
							} 
							else {
									UART_Error = UART_ERROR_CRC;
									UART_State = UART_STATE_CHECKCRC;
							}

                dma_rx_cnt = (dma_rx_cnt + uart_expected_length) % RX_BUFFER_SIZE;
            }
						else { //??
							// DEBUG BEGIN 
							// reset the length, reconfigure the number of data transfers:
							// LL_DMA_SetDataLength(DMA_LPUART_RX, RX_BUFFER_SIZE);
							// DEBUG END
							UART_State = UART_STATE_ABORT;
						} 
            break;
//						memset(hex_line_buffer, 0, UART_LINE_SIZE);
//            if (uart_length > 0) {
//                if (dma_rx_cnt + uart_expected_length <= RX_BUFFER_SIZE) {
//                    memcpy(hex_line_buffer, (uint8_t *)&usb_rx_buffer[dma_rx_cnt], uart_expected_length);
//                } else {
//                    uint32_t part_size = RX_BUFFER_SIZE - dma_rx_cnt;
//                    memcpy(hex_line_buffer, (uint8_t *)&usb_rx_buffer[dma_rx_cnt], part_size);
//                    memcpy(hex_line_buffer + part_size, (uint8_t *)usb_rx_buffer, uart_expected_length - part_size);
//                }

//                crc = hex_line_buffer[uart_expected_length - 1U];
//                calculated_crc = CalculateCRC(hex_line_buffer, uart_expected_length - 1U);
//								// TODO implelemnt CRC calculation before memcpy
//                if (crc == calculated_crc) {
//										uint8_t cmd_data_len = hex_line_buffer[0];
//										uint16_t cmd_addr = (hex_line_buffer[1] << 8) | hex_line_buffer[2];
//										UART_Command_t command = hex_line_buffer[3];
//										uint8_t *cmd_data = &hex_line_buffer[4];	
//										
//										if (EnqueueCommand(command, cmd_addr, cmd_data_len, cmd_data) == QUEUE_OK){
//											UART_State = UART_STATE_RUNCMD;
//										}
//										else {
//											UART_Error = UART_ERROR_QUEUE_FULL;
//											UART_State = UART_STATE_ABORT;  
//										}
//                } else {
//                    UART_State = UART_STATE_CHECKCRC;
//                }

//                dma_rx_cnt = (dma_rx_cnt + uart_expected_length) % RX_BUFFER_SIZE;
//            }
//						else { //??
//							UART_State = UART_STATE_ABORT;
//						} 
//            break;
        case UART_STATE_CHECKCRC:
						UART_Error = UART_ERROR_CRC;
            UART_State = UART_STATE_ABORT;  // TODO  handle CRC error
            break;

        case UART_STATE_RUNCMD:
						if (queue_cnt > 0){
							UART_Command_t command = CommandQueue[queue_read_cnt].cmd;
							uint8_t cmd_data_len = CommandQueue[queue_read_cnt].len;
							uint16_t cmd_addr = CommandQueue[queue_read_cnt].addr;
							uint8_t *cmd_data = CommandQueue[queue_read_cnt].data;	

							switch (command) {
								// add command 00 
								//
								// cmd 04 -> set page and set bank 5
								//								A0 
								// A0 -> 5 (1010 0000 > 0000 0101)
								// A1 -> 6 (1010 0000 > 0000 0110)
								//
								// cmd 03 -> send crc and run command load2k
								case UART_COMMAND_WRITE_BANK:
									if (IsBiSSReqBusy() != BISS_BUSY) {
										if (cmd_data_len == 0x40){
											cmd_data_len +=1;
											cmd_data[cmd_data_len] = ((cmd_addr %(0x00A0U & 0xFFU)) % 0x20U) + 6;
										}
										if (BiSSRequestWrite(cmd_addr, cmd_data_len, cmd_data) == BISS_REQ_OK) {
											UART_State = UART_STATE_IDLE;
											queue_read_cnt = (queue_read_cnt + 1U) % QUEUE_SIZE;
											queue_cnt--;
											retry_cnt = 0;
										} 
										else {
											retry_cnt++;
											if (retry_cnt >= MAX_RETRY){
												UART_Error = UART_ERROR_BISS_WRITE_FAULT;
												UART_State = UART_STATE_ABORT;
												retry_cnt = 0;
											}

										}
										
									}

									break;
								case UART_COMMAND_PAGE:
									if (IsBiSSReqBusy() != BISS_BUSY) {
										uint8_t cmd_data_page = cmd_data[0];
										if (BiSSRequestWrite(PAGE_ADDR, 1U, &cmd_data_page) == BISS_REQ_OK) {
											UART_State = UART_STATE_IDLE;
											queue_read_cnt = (queue_read_cnt + 1U) % QUEUE_SIZE;
											queue_cnt--;
											retry_cnt = 0;
//											uint8_t add_data = 0x05;
											if (EnqueueCommandToBegining(UART_COMMAND_WRITE_REG, BSEL_ADDR, 1U, (uint8_t *)FIRST_USER_BANK) != QUEUE_OK){
												UART_Error = UART_ERROR_QUEUE_FULL;
												UART_State = UART_STATE_ABORT; 
											}
										} 
										else {
											retry_cnt++;
											if (retry_cnt >= MAX_RETRY){
												UART_Error = UART_ERROR_BISS_WRITE_FAULT;
												UART_State = UART_STATE_ABORT;
												retry_cnt = 0;
											}

										}
										
									}

									break;
								case UART_COMMAND_WRITE_REG:
									if (IsBiSSReqBusy() != BISS_BUSY) {
										if (BiSSRequestWrite(cmd_addr, cmd_data_len, cmd_data) == BISS_REQ_OK) {
											UART_State = UART_STATE_IDLE;
											queue_read_cnt = (queue_read_cnt + 1U) % QUEUE_SIZE;
											queue_cnt--;
											retry_cnt = 0;
										} 
										else {
											retry_cnt++;
											if (retry_cnt >= MAX_RETRY){
												UART_Error = UART_ERROR_BISS_WRITE_FAULT;
												UART_State = UART_STATE_ABORT;
												retry_cnt = 0;
											}

										}
										
									}

									break;

								case UART_COMMAND_READ_REG:
										if (IsBiSSReqBusy() != BISS_BUSY){ 
											UART_TX.cmd = command;
											UART_TX.len = cmd_data_len;
											UART_TX.adr_h = (cmd_addr >> 8U) & 0xFFU;
											UART_TX.adr_l = cmd_addr & 0xFFU;
										
											// TODO Renishaw TX struct 
											// Test Renishaw BEGIN
											/*
											UART_RENISHAW_TX.cmd = command;
											UART_RENISHAW_TX.len = cmd_data_len;
											UART_RENISHAW_TX.adr_h = (cmd_addr >> 8U) & 0xFFU;
											UART_RENISHAW_TX.adr_l = cmd_addr & 0xFFU;
											*/
											// Test Renishaw END
											
											if (BiSSRequestRead(cmd_addr, cmd_data_len, UART_TX.Buf) == BISS_REQ_OK) {
												UART_State = UART_STATE_IDLE;
												
												queue_read_cnt = (queue_read_cnt + 1U) % QUEUE_SIZE;
												queue_cnt--;

												//TODO add retry
											} 
											else {
												UART_State = UART_STATE_ABORT;
											}
										}
										break;

								case UART_COMMAND_READ_ANGLE:
									if (IsBiSSReqBusy() != BISS_BUSY){
										UART_TX.cmd = command;
										UART_TX.len = UART_ANGLE_BUF_SIZE;
										ReadingStr.len = cmd_addr + 1;// Address = buf_size * 63
										ReadingStr.FIFO_current_ptr = 0;
										ReadingStr.FIFO_start_ptr = 0;
										ReadingStr.ToL_cnt = 0;
										// DEBUG BEGIN	
										// current_readingStr_len = ReadingStr.len;
										// DEBUG END
								
										// TODO Renishaw TX
										// Test Renishaw BEGIN
										UART_RENISHAW_TX.cmd = command;
										UART_RENISHAW_TX.len = UART_ANGLE_BUF_SIZE;
										ReadingRenishawStr.len = cmd_addr + 1;// Address = buf_size * 63
										ReadingRenishawStr.FIFO_current_ptr = 0;
										ReadingRenishawStr.FIFO_start_ptr = 0;
										ReadingRenishawStr.ToL_cnt = 0;
										// Test Renishaw END
								
										queue_read_cnt = (queue_read_cnt + 1U) % QUEUE_SIZE;
										queue_cnt--;

										UART_State = UART_STATE_ANGLE_READING;
									}
									break;
									
								case UART_COMMAND_POWER_OFF:
										EncoderPowerDisable();
										RenishawEncoderPowerDisable(); // ???
										if (LL_GPIO_IsOutputPinSet(LED1_RED)){ // Set LED1 to Red light
											LL_GPIO_ResetOutputPin(LED1_RED); 
										}
										UART_State = UART_STATE_IDLE;
										queue_read_cnt = (queue_read_cnt + 1U) % QUEUE_SIZE;
										queue_cnt--;										
										break;
								
								case UART_COMMAND_POWER_ON:
										EncoderPowerEnable();
										RenishawEncoderPowerEnable(); // ???
										if (!LL_GPIO_IsOutputPinSet(LED1_RED)){ // Set LED1 to Green light
											LL_GPIO_SetOutputPin(LED1_RED);
										}
										UART_State = UART_STATE_IDLE;
										queue_read_cnt = (queue_read_cnt + 1U) % QUEUE_SIZE;
										queue_cnt--;
										break;								
								
								default:
										UART_State = UART_STATE_ABORT;
										break;
							}
						}
            break;
				case UART_STATE_ANGLE_READING:
					if(ReadingStr.len > 0){
						AngleData_t angle_data = getAngle();
						AngleDataRenishaw_t angle_data_renishaw = getAngleRenishaw();
						if(angle_data.time_of_life_counter != ReadingStr.ToL_cnt){
							ReadingStr.ToL_cnt = angle_data.time_of_life_counter;
							ReadingStr.AngleFIFO[ReadingStr.FIFO_current_ptr] = angle_data;
							ReadingStr.FIFO_current_ptr++;
							// DEBUG BEGIN
							ReadingRenishawStr.ToL_cnt = angle_data.time_of_life_counter; // do i need this ????
							ReadingRenishawStr.AngleFIFO[ReadingRenishawStr.FIFO_current_ptr] = angle_data_renishaw;
							ReadingRenishawStr.FIFO_current_ptr++; // do i need this ????
							// DEBUG END
							if((((uint16_t)ReadingStr.FIFO_current_ptr + 256 - ReadingStr.FIFO_start_ptr) & 0xFFU) >= UART_ANGLE_LEN){
								uint8_t TxBufCnt = 0;
								while(ReadingStr.FIFO_start_ptr != ReadingStr.FIFO_current_ptr){
									*((AngleData_t*)&UART_TX.Buf[TxBufCnt]) = ReadingStr.AngleFIFO[ReadingStr.FIFO_start_ptr];
									// DEBUG BEGIN 
									TxBufCnt += 4; // 4 --> Encoder
									// Renishaw struct insert into UART_TX buffer
									*((AngleDataRenishaw_t*)&UART_TX.Buf[TxBufCnt]) = ReadingRenishawStr.AngleFIFO[ReadingRenishawStr.FIFO_start_ptr];
									// DEBUG END
									TxBufCnt += 2; // 2 --> Renishaw
									ReadingStr.FIFO_start_ptr++;
									// DEBUG BEGIN 
									ReadingRenishawStr.FIFO_start_ptr++; // do i need this ????
									// DEBUG END 
								}
								ReadingStr.len--;
								UART_TX.adr_h = (ReadingStr.len >> 8U) & 0xFFU;
								UART_TX.adr_l = ReadingStr.len & 0xFFU;
								UART_Transmit(&UART_TX);
							}
						}
						/*
						// Test Renishaw Begin
									
						ReadingRenishawStr.ToL_cnt = angle_data.time_of_life_counter;
						ReadingRenishawStr.AngleFIFO[ReadingRenishawStr.FIFO_current_ptr] = angle_data_renishaw;
						ReadingRenishawStr.FIFO_current_ptr++;
						if((((uint16_t)ReadingRenishawStr.FIFO_current_ptr + RX_BUFFER_SIZE - ReadingRenishawStr.FIFO_start_ptr) & 0xFFU) >= UART_ANGLE_LEN){
							uint8_t TxBufCnt = 0;
							while(ReadingRenishawStr.FIFO_start_ptr != ReadingRenishawStr.FIFO_current_ptr){
								*((AngleDataRenishaw_t*)&UART_RENISHAW_TX.Buf[TxBufCnt]) = ReadingRenishawStr.AngleFIFO[ReadingRenishawStr.FIFO_start_ptr];
								TxBufCnt += 4;
								ReadingRenishawStr.FIFO_start_ptr++;
							}
							//ReadingRenishawStr.len--;
							ReadingStr.len--;
							UART_RENISHAW_TX.adr_h = (ReadingStr.len >> 8U) & 0xFFU;
							UART_RENISHAW_TX.adr_l = ReadingStr.len & 0xFFU;
							UART_Transmit(&UART_RENISHAW_TX);
						}
						
						// Test Renishaw End
						angle_renishaw_data = LL_TIM_GetCounter(TIM_RENISHAW);
						AngleDataRenishaw_t angle_renishaw_data;
						angle_renishaw_data.angle_data = LL_TIM_GetCounter(TIM_RENISHAW);
						*/
					}
					else{
						UART_State = UART_STATE_IDLE;
					}
					break;
//				case UART_STATE_SENDING:
//					
//					break;
        case UART_STATE_ABORT:
					switch (UART_Error) {
						case UART_ERROR_QUEUE_FULL:					
							__NOP();
							__NOP();
							__NOP();
							break;
						case UART_ERROR_BISS_WRITE_FAULT:
							__NOP();
							__NOP();
							__NOP();
							break;
						case UART_ERROR_CRC:
							__NOP();
							__NOP();
							__NOP();
							break;
						case UART_ERROR_NONE:
							__NOP();
							__NOP();
							__NOP();
							// DEBUG BEGIN
							// dma_rx_cnt = (dma_rx_cnt + uart_expected_length) % RX_BUFFER_SIZE;
							// DEBUG END
							UART_State = UART_STATE_IDLE;
							break;
					}
					break;
						
					/*
					if(UART_Error == UART_ERROR_QUEUE_FULL){
						// 
					} else{
						__NOP();
						__NOP();
						__NOP();
						// DEBUG BEGIN
						// IF uart_length = 0 --> need to fix
						dma_rx_cnt = (dma_rx_cnt + uart_expected_length) % RX_BUFFER_SIZE;
						// DEBUG END
						UART_State = UART_STATE_IDLE;
					}
          break;
					*/
        default:
          UART_State = UART_STATE_IDLE;
          break;
    }
}
