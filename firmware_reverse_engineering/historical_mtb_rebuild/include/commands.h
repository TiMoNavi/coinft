/**
 * 命令解析与执行
 */

#ifndef COMMANDS_H
#define COMMANDS_H

#include "coinft_types.h"

/* 命令处理 */
void cmd_handle_idle(void);
void cmd_handle_streaming(void);
void cmd_handle_tare(uint8_t index, uint16_t param);
void cmd_handle_query(void);
void cmd_handle_write_id(const uint8_t* data, uint8_t len);
void cmd_handle_read_id(void);

/* 命令解析器 */
void cmd_parser_init(void);
void cmd_parser_feed_byte(uint8_t byte);
bool cmd_parser_is_complete(void);
void cmd_parser_execute(system_state_t* state);

#endif /* COMMANDS_H */
