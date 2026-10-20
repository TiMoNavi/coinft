/**
 * Em_EEPROM 管理
 */

#ifndef EEPROM_MGR_H
#define EEPROM_MGR_H

#include "coinft_types.h"

/* 初始化 */
void eeprom_mgr_init(void);

/* 读取设备 ID */
void eeprom_read_device_id(uint8_t buffer[DEVICE_ID_SIZE]);

/* 写入设备 ID */
void eeprom_write_device_id(const uint8_t data[DEVICE_ID_SIZE]);

#endif /* EEPROM_MGR_H */
