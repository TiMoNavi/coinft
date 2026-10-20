/**
 * UART 通信
 */

#ifndef UART_COMM_H
#define UART_COMM_H

#include "coinft_types.h"

/* 初始化 UART */
void uart_comm_init(void);

/* 发送 26 字节帧 */
void uart_send_frame(const record_t records[NUM_CHANNELS]);

/* 接收命令字节 */
bool uart_rx_available(void);
uint8_t uart_read_byte(void);

/* 发送单字节（用于 query 响应）*/
void uart_send_byte(uint8_t data);

/* 发送多字节（用于 device ID）*/
void uart_send_bytes(const uint8_t* data, uint8_t len);

#endif /* UART_COMM_H */
