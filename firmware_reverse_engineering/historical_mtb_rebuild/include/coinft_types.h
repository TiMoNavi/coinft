/**
 * CoinFT 固件重构 - 数据类型定义
 * 基于逆向工程结果 v54
 */

#ifndef COINFT_TYPES_H
#define COINFT_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* 系统状态 */
typedef enum {
    STATE_IDLE = 0,      /* 不扫描，不发送 */
    STATE_STREAMING,     /* 扫描并发送 UART */
    STATE_BLOCKED        /* 等待命令字节 */
} system_state_t;

/* record 结构（每条 10 字节）*/
typedef struct {
    uint16_t raw;           /* MSC 实时读数 */
    uint16_t baseline;      /* tare 时的快照 */
    uint16_t field2;        /* 预留，恒为 0 */
    uint16_t field3;        /* 预留，恒为 0 */
    uint8_t  diffcount;     /* raw > baseline 累加 */
    uint8_t  negdiffcount;  /* raw < baseline 累加 */
} __attribute__((packed)) record_t;

/* 配置常量 */
#define NUM_CHANNELS        12      /* UART 输出通道数 */
#define NUM_RECORDS         13      /* record 数组大小（最后一条不上线）*/
#define UART_FRAME_SIZE     26      /* UART 帧字节数 */
#define UART_BAUD           115200
#define DEVICE_ID_SIZE      12

/* UART 帧标记 */
#define FRAME_SOF           0x02    /* Start of Frame */
#define FRAME_EOF           0x03    /* End of Frame */

/* 命令字节 */
#define CMD_IDLE            'i'
#define CMD_STREAMING       's'
#define CMD_TARE            't'
#define CMD_QUERY           'q'
#define CMD_WRITE_ID        'w'
#define CMD_READ_ID         'n'

#define QUERY_RESPONSE      0x1A

#endif /* COINFT_TYPES_H */
