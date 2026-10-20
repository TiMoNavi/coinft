/**
 * 命令解析与执行
 */

#include "commands.h"
#include "record.h"
#include "uart_comm.h"
#include "eeprom_mgr.h"
#include <string.h>

/* 命令缓冲区 */
static uint8_t g_cmd_buffer[16];
static uint8_t g_cmd_index = 0;
static uint8_t g_cmd_expected = 0;
static uint8_t g_cmd_type = 0;

void cmd_parser_init(void)
{
    g_cmd_index = 0;
    g_cmd_expected = 0;
}

void cmd_parser_feed_byte(uint8_t byte)
{
    if (g_cmd_index == 0) {
        /* 第一个字节：命令类型 */
        g_cmd_type = byte;
        g_cmd_buffer[g_cmd_index++] = byte;
        
        if (byte == CMD_TARE) {
            g_cmd_expected = 4;  /* t + index + param[2] */
        }
        else if (byte == CMD_WRITE_ID) {
            g_cmd_expected = 13; /* w + 12 bytes（或遇到 CR）*/
        }
    }
    else {
        g_cmd_buffer[g_cmd_index++] = byte;
        
        /* 特殊处理：'w' 命令遇到 CR 结束 */
        if (g_cmd_type == CMD_WRITE_ID && byte == '\r') {
            g_cmd_expected = g_cmd_index;
        }
    }
}

bool cmd_parser_is_complete(void)
{
    return (g_cmd_index >= g_cmd_expected);
}

void cmd_parser_execute(system_state_t* state)
{
    if (g_cmd_type == CMD_TARE) {
        uint8_t index = g_cmd_buffer[1];
        uint16_t param = (g_cmd_buffer[3] << 8) | g_cmd_buffer[2];
        cmd_handle_tare(index, param);
        
        /* 返回 STREAMING */
        *state = STATE_STREAMING;
    }
    else if (g_cmd_type == CMD_WRITE_ID) {
        uint8_t len = g_cmd_index - 1;  /* 去掉命令字节 */
        cmd_handle_write_id(&g_cmd_buffer[1], len);
        
        /* 直落 'n' 命令 */
        cmd_handle_read_id();
        
        /* 保持 BLOCKED 状态 */
    }
    
    /* 重置解析器 */
    cmd_parser_init();
}

void cmd_handle_tare(uint8_t index, uint16_t param)
{
    /* TODO: 处理 index 和 param（写入扫描参数）*/
    /* 当前简化版：忽略参数校验 */
    (void)index;
    (void)param;
    
    /* 关键：无条件 tare */
    record_tare_all();
}

void cmd_handle_query(void)
{
    uart_send_byte(QUERY_RESPONSE);
}

void cmd_handle_write_id(const uint8_t* data, uint8_t len)
{
    uint8_t device_id[DEVICE_ID_SIZE];
    
    /* 复制数据（最多 12 字节）*/
    memset(device_id, 0, DEVICE_ID_SIZE);
    uint8_t copy_len = (len < DEVICE_ID_SIZE) ? len : DEVICE_ID_SIZE;
    memcpy(device_id, data, copy_len);
    
    eeprom_write_device_id(device_id);
}

void cmd_handle_read_id(void)
{
    uint8_t device_id[DEVICE_ID_SIZE];
    eeprom_read_device_id(device_id);
    uart_send_bytes(device_id, DEVICE_ID_SIZE);
}
