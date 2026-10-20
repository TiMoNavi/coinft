/* Host-only checks for the v50 emulated-EEPROM model. */

#include "../../07_analysis/coinft_firmware_pseudocode_v50.c"

#include <stdio.h>
#include <string.h>

static unsigned g_failures;

static void check(bool condition, const char *label)
{
    if (!condition) {
        ++g_failures;
        printf("FAIL: %s\n", label);
    }
}

static void test_identities(void)
{
    /* Pinned so the v36 "message object" label cannot come back. */
    check(COINFT_V50_EEPROM_WRITE == 0x00002E14,
          "FUN_00002E14 is the EEPROM write, not a UART message object");
    check(COINFT_V50_EEPROM_READ == 0x00002BA4, "read address");
    check(COINFT_V50_EEPROM_BAD_PARAM == 0x093E0001u, "BAD_PARAM code");
    check(COINFT_V50_EEPROM_WRITE_FAIL == 0x093E0004u, "WRITE_FAIL code");
}

static void test_crc8(void)
{
    /* Known-answer test for CRC-8 poly 0x31, init 0xFF, over bytes [1, len-4).
     * A four-byte input checksums zero bytes and must return the seed. */
    const uint8_t four[4] = {0x00, 0x11, 0x22, 0x33};
    uint8_t row[8];
    uint8_t crc;
    unsigned i;

    check(coinft_v50_eeprom_crc8(four, 4) == 0xFFu,
          "a four-byte row checksums no payload and returns the seed");
    check(coinft_v50_eeprom_crc8(NULL, 8) == 0xFFu, "null input returns seed");
    check(coinft_v50_eeprom_crc8(four, 2) == 0xFFu, "under-length returns seed");

    /* One payload byte: crc = CRC8(0xFF ^ payload). */
    memset(row, 0, sizeof row);
    row[1] = 0x00u;
    crc = coinft_v50_eeprom_crc8(row, 5u);
    /* 0xFF ^ 0x00 = 0xFF; eight shift-xor rounds with 0x31. */
    {
        uint8_t expect = 0xFFu;
        for (i = 0u; i < 8u; ++i)
            expect = (expect & 0x80u) ? (uint8_t)((expect << 1) ^ 0x31u)
                                      : (uint8_t)(expect << 1);
        check(crc == expect, "single-byte CRC matches the shift-xor definition");
    }

    /* The checksum must depend on the payload, and must skip byte 0. */
    memset(row, 0, sizeof row);
    row[1] = 0xA5u;
    crc = coinft_v50_eeprom_crc8(row, 8u);
    row[0] = 0x5Au;   /* byte 0 is outside the checksummed range */
    check(coinft_v50_eeprom_crc8(row, 8u) == crc,
          "byte 0 is excluded from the checksum");
    row[1] = 0xA6u;
    check(coinft_v50_eeprom_crc8(row, 8u) != crc,
          "payload changes alter the checksum");

    check(coinft_v50_row_valid(row, 8u,
                               coinft_v50_eeprom_crc8(row, 8u)),
          "a row agreeing with its stored CRC is valid");
    check(!coinft_v50_row_valid(row, 8u,
                                (uint8_t)(coinft_v50_eeprom_crc8(row, 8u) ^ 1u)),
          "a mismatching CRC invalidates the row");
}

static void test_wear_levelling(void)
{
    const uint32_t base = COINFT_V50_EEPROM_FLASH_BASE;
    const uint32_t unit = 128u;

    /* Factor 1 means no wear levelling: the pointer passes through. */
    check(coinft_v50_select_copy(base + 4u, base, unit, 1u) == base + 4u,
          "factor 1 leaves the pointer alone");
    check(coinft_v50_select_copy(base + 4u, base, unit, 0u) == base + 4u,
          "factor 0 leaves the pointer alone");

    /* The device uses factor 2. */
    check(coinft_v50_select_copy(base + 4u, base, unit,
                                 COINFT_V50_EEPROM_WEAR_LEVELLING)
          == base + 4u + unit,
          "inside the first unit, advance to the second copy");
    check(coinft_v50_select_copy(base + unit + 4u, base, unit,
                                 COINFT_V50_EEPROM_WEAR_LEVELLING)
          == base + 4u,
          "beyond the first unit, wrap back");
}

static void test_device_id(void)
{
    const uint8_t initialised[12] = {'N', 'F', 'T', '\r', 0, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t blank[12] = {0};
    const uint8_t no_terminator[12] = {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L'};

    check(COINFT_V50_DEVICE_ID_LEN == 12 && COINFT_V50_DEVICE_ID_ADDR == 0x40,
          "device ID is 12 bytes at logical address 0x40");

    check(coinft_v50_device_id_initialised(initialised, 12u),
          "the observed 'NFT\\r' buffer counts as initialised");
    check(!coinft_v50_device_id_initialised(blank, 12u),
          "a blank row triggers the default-template path");
    check(!coinft_v50_device_id_initialised(no_terminator, 12u),
          "a full row with no CR triggers the default-template path");
    check(!coinft_v50_device_id_initialised(NULL, 12u), "null rejected");

    /* The CR may sit anywhere in the row, including the last byte. */
    {
        uint8_t last[12];
        memset(last, 'x', sizeof last);
        last[11] = '\r';
        check(coinft_v50_device_id_initialised(last, 12u),
              "a CR in the final byte still counts");
    }
}

static void test_config(void)
{
    check(COINFT_V50_EEPROM_SIZE == 128, "EEPROM size 128");
    check(COINFT_V50_EEPROM_WEAR_LEVELLING == 2, "wear levelling factor 2");
    check(COINFT_V50_EEPROM_REDUNDANT_COPY == 0,
          "redundant copy is disabled (param_4[7] low byte is 0)");
    check(COINFT_V50_EEPROM_FLASH_BASE == 0x5000, "flash base 0x5000");
    check(COINFT_V50_DEVICE_ID_TEMPLATE == 0x4F78, "template at 0x4f78");
    /* The device ID must fit inside the emulated EEPROM. */
    check(COINFT_V50_DEVICE_ID_ADDR + COINFT_V50_DEVICE_ID_LEN
          <= COINFT_V50_EEPROM_SIZE,
          "the device ID lies within the EEPROM's size");
}

int main(void)
{
    test_identities();
    test_crc8();
    test_wear_levelling();
    test_device_id();
    test_config();

    if (g_failures != 0u) {
        printf("test_v50_eeprom: %u failure(s)\n", g_failures);
        return 1;
    }
    printf("test_v50_eeprom: passed\n");
    return 0;
}
