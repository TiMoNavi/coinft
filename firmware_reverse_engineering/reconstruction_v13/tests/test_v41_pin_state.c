/* Host-only regression test for the recovered CSD pin/shield reset model. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../../07_analysis/coinft_firmware_pseudocode_v41.c"

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static void put_record(uint8_t *flash, size_t offset, uint32_t port,
                       uint8_t pin, uint8_t hsiom, uint8_t flags)
{
    put_u32(flash + offset, port);
    flash[offset + 4u] = pin;
    flash[offset + 5u] = hsiom;
    flash[offset + 6u] = flags;
    flash[offset + 7u] = 0u;
}

static void test_family_mapping_and_decode(void)
{
    uint8_t flash[0x98u] = {0u};
    coinft_v41_pin_record_t record;

    assert(coinft_v41_port_number((uintptr_t)0x40040000u) == 0);
    assert(coinft_v41_port_number((uintptr_t)0x40040300u) == 3);
    assert(coinft_v41_port_number((uintptr_t)0x40040100u) == -1);

    put_record(flash, 0u, 0x40040300u, 5u, 4u, 0u);
    put_record(flash, 0x90u, 0x40040300u, 6u, 5u, 0u);
    assert(coinft_v41_decode_pin_record_at(
               flash, sizeof(flash), COINFT_V41_PIN_TABLE_BASE, &record));
    assert(record.port_base == (uintptr_t)0x40040300u);
    assert(record.pin == 5u && record.hsiom == 4u);
    assert(coinft_v41_decode_pin_record_at(
               flash, sizeof(flash), COINFT_V41_SHIELD_TABLE_BASE, &record));
    assert(record.pin == 6u && record.hsiom == 5u);
}

static void test_packed_pin_fields_and_reset_order(void)
{
    coinft_v41_port_shadow_t port;
    coinft_v41_pin_record_t record = {
        (uintptr_t)0x40040300u, 7u, 6u, 0u
    };
    memset(&port, 0, sizeof(port));
    port.port_base = record.port_base;

    assert(coinft_v41_set_hsiom(&port, 7u, 6u));
    assert(port.hsiom == UINT32_C(0x60000000));
    assert(coinft_v41_set_drive_mode(&port, 7u, 9u));
    assert((port.pc & (7u << 21u)) == (1u << 21u));
    assert((port.pc2 & (1u << 7u)) != 0u);

    memset(&port, 0, sizeof(port));
    port.port_base = record.port_base;
    assert(coinft_v41_reset_pin(&port, &record));
    assert((port.hsiom & (0xfu << 28u)) == 0u);
    assert((port.pc & (7u << 21u)) == (1u << 21u));
    assert((port.pc2 & (1u << 7u)) != 0u);
    assert((port.dr & (1u << 7u)) == 0u);
    assert(port.dr_clear_mask == (1u << 7u));
}

static void test_public_and_shield_tables(void)
{
    uint8_t flash[0x98u] = {0u};
    coinft_v41_port_shadow_t ports[2];
    memset(ports, 0, sizeof(ports));
    ports[0].port_base = (uintptr_t)0x40040000u;
    ports[1].port_base = (uintptr_t)0x40040300u;

    put_record(flash, 0u, 0x40040300u, 5u, 4u, 0u);
    put_record(flash, 8u, 0x40040000u, 2u, 6u, 0u);
    put_record(flash, 0x90u, 0x40040300u, 6u, 5u, 0u);
    assert(coinft_v41_reset_public_and_shield_tables(
               flash, sizeof(flash), ports, 2u, 2u, 1u) == 3u);
    assert(ports[0].dr_clear_mask == (1u << 2u));
    assert(ports[1].dr_clear_mask == ((1u << 5u) | (1u << 6u)));
}

int main(void)
{
    test_family_mapping_and_decode();
    test_packed_pin_fields_and_reset_order();
    test_public_and_shield_tables();
    return 0;
}
