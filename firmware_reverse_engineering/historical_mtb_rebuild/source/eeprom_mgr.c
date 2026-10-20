/**
 * EEPROM 管理 - 简化版
 *
 * 使用 Flash 直接读写（暂不依赖 Em_EEPROM 中间件）
 * 注：生产环境建议使用 Em_EEPROM 以实现磨损均衡
 */

#include "eeprom_mgr.h"
#include "cy_flash.h"
#include <string.h>

/* Flash 模拟 EEPROM 的存储区域（最后一个 Flash 行）*/
#define EEPROM_FLASH_ROW        ((CY_FLASH_NUMBER_ROWS) - 1)
#define EEPROM_FLASH_ADDR       (CY_FLASH_BASE + (EEPROM_FLASH_ROW * CY_FLASH_SIZEOF_ROW))

/* 设备 ID 在该 Flash 行中的偏移 */
#define DEVICE_ID_OFFSET        0x10

/* 设备 ID 在 EEPROM 中的偏移（从逆向结果）*/
#define DEVICE_ID_OFFSET    0x10

void eeprom_mgr_init(void)
{
    /* 简化版不需要初始化 */
    /* Flash 硬件已由系统启动代码初始化 */
}

void eeprom_read_device_id(uint8_t buffer[DEVICE_ID_SIZE])
{
    /* 从 Flash 读取设备 ID */
    const uint8_t* flash_addr = (const uint8_t*)(EEPROM_FLASH_ADDR + DEVICE_ID_OFFSET);

    /* 检查是否为未初始化状态（全 0xFF）*/
    if (flash_addr[0] == 0xFF) {
        /* 使用默认值 */
        const char default_id[] = "COINFT_V2_00";
        memcpy(buffer, default_id, DEVICE_ID_SIZE);
    } else {
        /* 直接从 Flash 读取 */
        memcpy(buffer, flash_addr, DEVICE_ID_SIZE);
    }
}

void eeprom_write_device_id(const uint8_t data[DEVICE_ID_SIZE])
{
    /* 写入设备 ID 到 Flash
     * 注：需要先擦除整个 Flash 行，然后写入
     * 这里简化处理，实际应用建议使用 Em_EEPROM 中间件
     */

    /* 准备写入缓冲区（整个 Flash 行）*/
    uint8_t row_buffer[CY_FLASH_SIZEOF_ROW];

    /* 1. 读取当前 Flash 行内容 */
    memcpy(row_buffer, (const void*)EEPROM_FLASH_ADDR, CY_FLASH_SIZEOF_ROW);

    /* 2. 更新设备 ID 部分 */
    memcpy(row_buffer + DEVICE_ID_OFFSET, data, DEVICE_ID_SIZE);

    /* 3. 写入整个 Flash 行 */
    cy_en_flashdrv_status_t status = Cy_Flash_WriteRow(EEPROM_FLASH_ADDR, (const uint32_t*)row_buffer);

    /* 忽略错误（简化处理）*/
    (void)status;
}
