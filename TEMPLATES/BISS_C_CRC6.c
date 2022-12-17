/*!
 * @file BISS_C_CRC6.c
 * @author Kirill Rostovskiy (kmrost@kb-rs.com)
 * @brief BISS C CRC6 calculation template
 * @version 0.1
 * @copyright KBRS LLC (c) 2022
 */
#include <stdint.h>

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
uint8_t BISS_CRC6_Calc(uint32_t data){
	uint8_t crc = CRC6_LUT[(data >> 24U) & 0xFFU];
	crc = CRC6_LUT[((data >> 16U) & 0xFFU) ^ crc];
	crc = CRC6_LUT[((data >> 8U) & 0xFFU) ^ crc];
	crc = CRC6_LUT[(data & 0xFFU) ^ crc];
	crc = ((~crc) >> 2U) & 0x3FU;
	return(crc);
}
