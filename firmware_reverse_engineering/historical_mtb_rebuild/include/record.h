/**
 * record 数组管理
 */

#ifndef RECORD_H
#define RECORD_H

#include "coinft_types.h"

/* 初始化 */
void record_init(void);

/* 更新 raw 值（扫描完成后调用）*/
void record_update_raw(uint8_t channel, uint16_t raw_value);

/* tare：baseline := raw（全部 13 条）*/
void record_tare_all(void);

/* 获取 record（用于 UART 编码）*/
const record_t* record_get(uint8_t channel);

/* 更新 diffcount/negdiffcount */
void record_update_diff_counters(void);

#endif /* RECORD_H */
