/**
 * CapSense 扫描引擎
 */

#ifndef CAPSENSE_SCAN_H
#define CAPSENSE_SCAN_H

#include "coinft_types.h"

/* 初始化 CapSense */
void capsense_scan_init(void);

/* 启动扫描 */
void capsense_start(void);

/* 停止扫描 */
void capsense_stop(void);

/* 检查扫描是否完成 */
bool capsense_is_scan_complete(void);

/* 读取 12 路 raw 结果 */
void capsense_read_results(uint16_t raw[NUM_CHANNELS]);

/* 清除扫描完成标志 */
void capsense_clear_scan_flag(void);

#endif /* CAPSENSE_SCAN_H */
