/**
 * record 数组管理
 */

#include "record.h"
#include <string.h>

static record_t g_records[NUM_RECORDS];

void record_init(void)
{
    memset(g_records, 0, sizeof(g_records));
    
    /* 初始化 baseline 为默认值 */
    for (int i = 0; i < NUM_RECORDS; i++) {
        g_records[i].baseline = 2048;  /* 典型中间值 */
    }
}

void record_update_raw(uint8_t channel, uint16_t raw_value)
{
    if (channel < NUM_RECORDS) {
        g_records[channel].raw = raw_value;
    }
}

void record_tare_all(void)
{
    /* 关键：无条件 tare 全部 13 条（包括 record 12）*/
    for (int ch = 0; ch < NUM_RECORDS; ch++) {
        g_records[ch].baseline = g_records[ch].raw;
    }
}

const record_t* record_get(uint8_t channel)
{
    if (channel < NUM_RECORDS) {
        return &g_records[channel];
    }
    return NULL;
}

void record_update_diff_counters(void)
{
    for (int ch = 0; ch < NUM_RECORDS; ch++) {
        int16_t diff = (int16_t)g_records[ch].raw - (int16_t)g_records[ch].baseline;
        
        if (diff > 0) {
            if (g_records[ch].diffcount < 255) {
                g_records[ch].diffcount++;
            }
        }
        else if (diff < 0) {
            if (g_records[ch].negdiffcount < 255) {
                g_records[ch].negdiffcount++;
            }
        }
    }
}
