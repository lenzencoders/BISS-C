/*!
 * @file biss_c_master.h
 * @author Kirill Rostovskiy (kmrost@lenzencoders.com)
 * @brief BiSS C Master library
 * @version 0.1
 * @copyright Lenz Encoders (c) 2024
 *
 * BiSS C Master state machine CDM setting and CDS receiving timing diagram 
 *
 *      \|/ CDM(n) Master request                  CDM(n) setting up \|/             \|/ CDM(n+1) Master request     
 * MA  ¯¯¯¯¯|__|¯¯|__|¯¯|__|¯¯|__|¯¯|__|¯¯|__|¯¯|__|¯¯|__|¯xxxxxxx_|¯¯|xxxx nCDM xxxxx|¯¯¯¯|__|¯¯|_xxx_|¯¯|__|¯¯|_xxxxx
 *
 * SLO ¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯|__________ACK__________|¯ST¯¯|xCDSx|xx SCD xx|_xx_TimeOut_xx_|¯¯¯¯¯¯¯¯¯¯¯¯¯xxxx¯¯|xCDSx|x SCD xx
 *                             CDS(n-1) setting up /|\ 			  CDS(n-1)Master read  /|\ CDS(n) setting up /|\
 */

#ifndef __BISS_C_MASTER_H
#define __BISS_C_MASTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

typedef enum{
	BISS_REQ_OK = 0u,
	BISS_BUSY = 1u,
	BISS_WRITE_FINISHED,
	BISS_READ_FINISHED
}BiSSExternalState_t;

typedef union {
	uint32_t Angle_TOF;
	struct{
    uint32_t angle_data:24;
    uint32_t time_of_life_counter:8;
	};
} AngleData_t;

typedef enum{
	nCDM = 0,
	CDM = 1U,
}CDM_t;

typedef enum{
	nCDS = 0,
	CDS = 1U
}CDS_t;

CDM_t BiSS_C_Master_StateMachine(CDS_t CDS_in);
BiSSExternalState_t BiSSRequestRead(uint16_t addr, uint8_t cnt, uint8_t *ptr);
BiSSExternalState_t BiSSRequestWrite(uint16_t addr, uint8_t cnt, uint8_t *ptr);

BiSSExternalState_t IsBiSSReqBusy(void);
void BiSSResetExternalState(void);
	
static inline AngleData_t getAngle(void){
	extern volatile AngleData_t AngleData;
	return(AngleData);
}

#ifdef __cplusplus
}
#endif

#endif /* __BISS_C_MASTER_H */
