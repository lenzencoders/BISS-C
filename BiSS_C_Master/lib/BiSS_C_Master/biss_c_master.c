/*!
 * @file biss_c_master.c
 * @author Kirill Rostovskiy (kmrost@lenzencoders.com)
 * @brief BiSS C Master library
 * @version 0.1
 * @copyright Lenz Encoders (c) 2024
 */

#include "biss_c_master.h"
#include "biss_c_master_hal.h"

#define BISS_ADRESS_Mask					0x7FU	/* BiSS C register address mask (7 bits) */
#define BISS_CD_CRC4_Pos					2U 		/* Control Data CRC4 position */
#define BISS_CD_Pos								6U		/* Control Data position */
#define BISS_W_CDF_DATA_Pos				5U		/* Data from Write Control Data Frame  position */
#define BISS_W_CDF_CRC4_Pos				1U		/* CRC4 from Write Control Data Frame  position */
#define BISS_R_CDF_DATA_Pos				4U		/* Data from Read Control Data Frame  position */
#define BISS_R_CDF_CRC4_Pos				0		/* CRC4 from Read Control Data Frame  position */
#define BISS_R_CDF_CRC4_Mask			0xFU		/* Data from Read Control Data Frame  position */
#define BISS_CDM_CTS_Pos     			10U   /* Control bit CTS position */
#define BISS_CDM_S_Pos       			11U   /* Start bit position */
#define BISS_DATA_CRC_LEN					13U	 	/* Data(8) CRC(4) P(1) */
#define BISS_ABORT_CYCLES 				14U		/* Cycles to abort control data frame */
#define BISS_IDL_ADR_RW_LEN 			18U
#define	MSB_BISS_IDL_ADR_RW_LEN 	(BISS_IDL_ADR_RW_LEN - 2U)
#define IDL_0											0x20000U


static const uint8_t  tableCRC4[16] = {0x0,0x3,0x6,0x5,0xC,0xF,0xA,0x9,0xB,0x8,0xD,0xE,0x7,0x4,0x1,0x2};

typedef enum {
	BISS_STATE_IDLE,
	BISS_STATE_START_READ,
	BISS_STATE_START_WRITE,
	BISS_STATE_CDF,
	BISS_STATE_DATA_READ,
	BISS_STATE_DATA_WRITE,
	BISS_STATE_ABORT,
	BISS_STATE_STOP
}BISS_State_t;
typedef enum{
	nRW = 1u, 
	RnW = 2u
}RW_t;

struct{
	volatile uint32_t ShiftCDM;
	volatile uint32_t ShiftCDS;
	volatile uint8_t RegAdr;	
	volatile uint8_t RegLen;
	uint8_t RegCnt;
	uint8_t CDMCnt;
	uint8_t StopCnt;
	volatile uint8_t end_cnt;
	volatile uint8_t * BufPtr;
	RW_t RW;
	enum{WRITE_FIRST, WRITE_CONT, WRITE_END}WriteStopBitCheck;
	enum{WRITE_STATE_START, WRITE_STATE_START_BIT_CHECK, WRITE_STATE_SHIFT, WRITE_STATE_STOP}WriteState;
	volatile BISS_State_t State;
	volatile BiSSExternalState_t ExternalState;
	volatile uint32_t Bit_Mask;
}BiSS;

BiSSExternalState_t IsBiSSReqBusy(void){
	return(BiSS.ExternalState);
}
static inline uint32_t CRC4_CTS_ADR(uint32_t adr){
	return(0xF - tableCRC4[(adr & 0xF) 	^ tableCRC4[((adr >> 4) & 0xF) ^ 0xC]]);
}

static inline uint32_t CRC4_DATA(uint32_t data){
	return(0xF - tableCRC4[(data & 0xF) ^ tableCRC4[(data >> 4) & 0xF]]);
}

static inline CDM_t Int2CDM(uint32_t In){
	return(((In & 0x1U) == 0x1U)? CDM : nCDM); 
}

static inline uint32_t Gen_CDF_Start_Read(uint8_t Address) {
    uint16_t Control_Data = (1U << BISS_CDM_CTS_Pos) |
                            (Address & BISS_ADRESS_Mask);

    uint8_t CRC4_Control_Data = CRC4_CTS_ADR((1U << BISS_CDM_S_Pos) | Control_Data); 

    uint32_t CDM_Frame = (Control_Data << BISS_CD_Pos) |
                         (CRC4_Control_Data << BISS_CD_CRC4_Pos) |
                         (RW_t) RnW;

    return CDM_Frame; // 17-bit
}

static inline uint32_t Gen_CDF_Start_Write(uint16_t Address) {
    uint16_t Control_Data = (1U << BISS_CDM_CTS_Pos) |	// 0b1
                            (Address & BISS_ADRESS_Mask);			// 0b0000000

    uint8_t CRC4_Control_Data = CRC4_CTS_ADR(Control_Data);

    uint32_t CDM_Frame = (Control_Data << BISS_CD_Pos) |
                         (CRC4_Control_Data << BISS_CD_CRC4_Pos) |
                         (RW_t) nRW;
    return CDM_Frame; //19-bit
}

static inline uint32_t Gen_CDF_Data_Write(uint8_t Data) {
		uint8_t CRC4 = CRC4_DATA(Data);		
		return ((Data << BISS_W_CDF_DATA_Pos) | (CRC4 << BISS_W_CDF_CRC4_Pos)); // 13-bit
}

BiSSExternalState_t BiSSRequestRead(uint16_t addr, uint8_t len, uint8_t *ptr){
	BiSSExternalState_t ret = BISS_REQ_OK;

	if(BiSS.State != BISS_STATE_IDLE){
		ret = BISS_BUSY;		
	}
	else{
		BiSS.ExternalState = BISS_BUSY;
		BiSS.State = BISS_STATE_START_READ;
		BiSS.RegAdr = addr;
		BiSS.RegLen = len;
		BiSS.BufPtr = ptr;		
	}
	return(ret);
}

BiSSExternalState_t BiSSRequestWrite(uint16_t addr, uint8_t len, uint8_t *ptr){
	BiSSExternalState_t ret = BISS_REQ_OK;

	if(BiSS.State != BISS_STATE_IDLE){
		ret = BISS_BUSY;		
	}
	else{
		BiSS.ExternalState = BISS_BUSY;
		BiSS.State = BISS_STATE_START_WRITE;
		BiSS.RegAdr = addr;
		BiSS.RegLen = len;
		BiSS.BufPtr = ptr;	
	}
	return(ret);
}

CDM_t BiSS_C_Master_StateMachine(CDS_t CDS_in) {
	CDM_t ret = nCDM;
	switch (BiSS.State) {
		
		case BISS_STATE_IDLE:
			ret = nCDM;            
			break;						
		
		case BISS_STATE_START_READ: 
			ret = CDM; //Start bit
			BiSS.ShiftCDM = Gen_CDF_Start_Read(BiSS.RegAdr);
			BiSS.RW = RnW;
			BiSS.CDMCnt = 0;
			BiSS.ShiftCDS = 0;
			BiSS.RegCnt = 0;
			BiSS.State = BISS_STATE_CDF;			
			BiSS.Bit_Mask = 1U << MSB_BISS_IDL_ADR_RW_LEN;	
			break;
		
		case BISS_STATE_START_WRITE:
			ret = CDM; //Start bit
			BiSS.ShiftCDM = Gen_CDF_Start_Write(BiSS.RegAdr);
			BiSS.RW = nRW;
			BiSS.RegCnt = 0;
			BiSS.CDMCnt = 0;
			BiSS.ShiftCDS = 0;
			BiSS.WriteState = WRITE_STATE_START;
			BiSS.State = BISS_STATE_CDF;			
			BiSS.WriteStopBitCheck = WRITE_FIRST;
			BiSS.Bit_Mask = 1U << MSB_BISS_IDL_ADR_RW_LEN;				
			break;
				
		case BISS_STATE_CDF:
			BiSS.CDMCnt++;				
			BiSS.ShiftCDS |= (uint32_t)CDS_in << (BISS_IDL_ADR_RW_LEN - BiSS.CDMCnt); 				
			if (BiSS.CDMCnt < BISS_IDL_ADR_RW_LEN){
					if (BiSS.ShiftCDM & BiSS.Bit_Mask) {
							ret = CDM;
					} else {
							ret = nCDM;
					}
					BiSS.Bit_Mask >>= 1U;					
			}
			else {
				if (BiSS.ShiftCDS == ((IDL_0 | BiSS.RW) >> 1U)){
					BiSS.CDMCnt = 0;
					if(BiSS.RW == RnW){
						BiSS.State = BISS_STATE_DATA_READ;
					}
					else{
						BiSS.State = BISS_STATE_DATA_WRITE;
					}
					BiSS.ShiftCDS = 0;
					ret = CDM; //Start bit
				}
				else{
					ret = nCDM;
					BiSS.State = BISS_STATE_ABORT;
				}	
			}
			break;
			
		case BISS_STATE_DATA_READ:
			if(BiSS.CDMCnt >= BISS_DATA_CRC_LEN){
				BiSS.ShiftCDS |= (uint32_t)CDS_in << (BISS_DATA_CRC_LEN - BiSS.CDMCnt); 
				if (CRC4_DATA(BiSS.ShiftCDS >> BISS_R_CDF_DATA_Pos) == (BiSS.ShiftCDS & BISS_R_CDF_CRC4_Mask)) {// TODO explain 20 - BISS_ID_ADR_RWS_LEN
					BiSS.BufPtr[BiSS.RegCnt] = BiSS.ShiftCDS >> BISS_R_CDF_DATA_Pos;
					BiSS.RegCnt++;
					if(BiSS.RegCnt < BiSS.RegLen){
							BiSS.ShiftCDS = 0;
							BiSS.CDMCnt = 0;
							ret = CDM;//Start
					}
					else{					
						ret = nCDM;
						BiSS.ExternalState = BISS_READ_FINISHED;
						BiSS.State = BISS_STATE_STOP;
						
						BiSS.StopCnt = 14;
					}
				}
				else{
					ret = nCDM;					
					BiSS.State = BISS_STATE_ABORT;
				}
			}
			else{
				BiSS.ShiftCDS |= (uint32_t)CDS_in << (BISS_DATA_CRC_LEN - BiSS.CDMCnt); 	
				BiSS.CDMCnt++;
				ret = nCDM;
			}
			break;
			
		case BISS_STATE_DATA_WRITE:
			switch(BiSS.WriteState){
				case WRITE_STATE_START:		
					BiSS.WriteState = WRITE_STATE_START_BIT_CHECK;
					switch(BiSS.WriteStopBitCheck){
						case WRITE_FIRST:
							BiSS.WriteStopBitCheck = WRITE_CONT;
							if(CDS_in == nCDS){// Write bit check
								BiSS.State = BISS_STATE_ABORT;
								ret = nCDM;							
							}
							else{							
								BiSS.ShiftCDM = Gen_CDF_Data_Write(BiSS.BufPtr[BiSS.RegCnt]);
								ret = Int2CDM(BiSS.ShiftCDM >> (BISS_DATA_CRC_LEN - 1U));
							}
							break;					
						case WRITE_CONT:
							if(CDS_in == CDS){// Stop bit check
								BiSS.State = BISS_STATE_ABORT;
								ret = nCDM;
							}
							else{							
								BiSS.ShiftCDM = Gen_CDF_Data_Write(BiSS.BufPtr[BiSS.RegCnt]);
								ret = Int2CDM(BiSS.ShiftCDM >> (BISS_DATA_CRC_LEN - 1U));
							}
							break;
						case WRITE_END:
							if(CDS_in == nCDS){// Stop bit check
								BiSS.State = BISS_STATE_STOP;
								//FOR SALEAE Analizer only
								BiSS.StopCnt = 14;
							}
							else{		
								BiSS.State = BISS_STATE_ABORT;
							}
							ret = nCDM;
							break;
						default:
							BiSS.State = BISS_STATE_ABORT;
							ret = nCDM;
					}
					break;
				
				case WRITE_STATE_START_BIT_CHECK:			
					BiSS.WriteState = WRITE_STATE_SHIFT;			
					BiSS.CDMCnt = 2U;	
					BiSS.ShiftCDS = 0;
					if(CDS_in == nCDS){//Start bit check
						BiSS.State = BISS_STATE_ABORT;
						ret = nCDM;
					}
					else{
						ret = Int2CDM(BiSS.ShiftCDM >> (BISS_DATA_CRC_LEN - 2U));
					}
					break;
				
				case WRITE_STATE_SHIFT:
					BiSS.ShiftCDS |= (uint32_t)CDS_in << (BISS_DATA_CRC_LEN - BiSS.CDMCnt); 	
					BiSS.CDMCnt++;
					ret = Int2CDM(BiSS.ShiftCDM >> (BISS_DATA_CRC_LEN - BiSS.CDMCnt));
					if(BiSS.CDMCnt == BISS_DATA_CRC_LEN){
						BiSS.WriteState = WRITE_STATE_STOP;	
					}
					break;
				
				case WRITE_STATE_STOP:
					BiSS.WriteState = WRITE_STATE_START;		
					BiSS.ShiftCDS |= (uint32_t)CDS_in << (BISS_DATA_CRC_LEN - BiSS.CDMCnt); 	
					if(BiSS.ShiftCDS == (BiSS.ShiftCDM >> 1U)){
						BiSS.RegCnt++;
						if(BiSS.RegCnt < BiSS.RegLen){
							ret = CDM;// Start bit
						}
						else{
							ret = nCDM;
							BiSS.WriteStopBitCheck = WRITE_END;
							BiSS.ExternalState = BISS_WRITE_FINISHED;
						}
					}
					else{
						ret = nCDM;
						BiSS.State = BISS_STATE_ABORT;					
					}
					break;
				
				default:
					BiSS.State = BISS_STATE_ABORT;
			} 
			break;
			
		case BISS_STATE_ABORT:
				ret = nCDM;
				BiSS.end_cnt++;
				if (BiSS.end_cnt == BISS_ABORT_CYCLES){
					BiSS.end_cnt = 0;
					BiSS.State = BISS_STATE_IDLE;
				}
				break;
				
		case BISS_STATE_STOP:
			if(BiSS.StopCnt > 0){
				BiSS.StopCnt--;
			}
			else{
				BiSS.State = BISS_STATE_IDLE;
				
			}
			ret = nCDM;
			break;
			
		default:
				ret = nCDM;
				BiSS.State = BISS_STATE_IDLE;
				break;
    }
		return ret;
}

void BiSSResetExternalState(void){	
	BiSS.ExternalState = BISS_REQ_OK;
}
