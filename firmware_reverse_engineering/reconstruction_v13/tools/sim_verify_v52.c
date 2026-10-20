/* v52 stage J: replay recorded hardware captures through the simulator.
 *
 * Reads a flat binary produced by sim_export_v52.py -- a sequence of records
 * holding the raw counts read over SWD and the UART frame the device actually
 * transmitted closest to that read -- and checks that the simulator, fed those
 * raw counts, emits byte-identical frames.
 *
 * Usage: sim_verify_v52 <cases.bin>
 * Record layout (little endian): 13 x uint16 raw, then 26 bytes of UART frame.
 */

#include "../../07_analysis/coinft_sim_v52.c"

#include <stdio.h>
#include <stdlib.h>

enum {
    CASE_RAW_BYTES = COINFT_SIM_RECORDS * 2,
    CASE_BYTES = CASE_RAW_BYTES + COINFT_SIM_FRAME_BYTES
};

int main(int argc, char **argv)
{
    FILE *fp;
    unsigned char buf[CASE_BYTES];
    unsigned long total = 0, frame_ok = 0, payload_ok = 0, header_ok = 0;
    coinft_sim_t sim;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <cases.bin>\n", argv[0]);
        return 2;
    }
    fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        perror(argv[1]);
        return 2;
    }

    coinft_sim_reset(&sim);
    coinft_sim_feed_byte(&sim, 's');

    while (fread(buf, 1, sizeof buf, fp) == sizeof buf) {
        uint16_t raw[COINFT_SIM_RECORDS];
        uint8_t produced[COINFT_SIM_FRAME_BYTES];
        const unsigned char *expect = buf + CASE_RAW_BYTES;
        unsigned i;

        ++total;
        for (i = 0u; i < COINFT_SIM_RECORDS; ++i)
            raw[i] = (uint16_t)(buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8));

        coinft_sim_scan(&sim, raw, COINFT_SIM_RECORDS);
        if (!coinft_sim_pack_frame(&sim, produced, sizeof produced))
            continue;

        /* The recorded frame must be a well-formed frame at all. */
        if (expect[0] == 0x02u && expect[COINFT_SIM_FRAME_BYTES - 1u] == 0x03u)
            ++header_ok;
        /* Framing bytes the simulator emits must match. */
        if (produced[0] == expect[0] &&
            produced[COINFT_SIM_FRAME_BYTES - 1u] ==
            expect[COINFT_SIM_FRAME_BYTES - 1u])
            ++frame_ok;
        /* And the 24 payload bytes must be identical. */
        if (memcmp(produced + 1, expect + 1, 24) == 0)
            ++payload_ok;
    }
    fclose(fp);

    printf("cases=%lu\n", total);
    printf("recorded_frames_wellformed=%lu/%lu\n", header_ok, total);
    printf("framing_bytes_match=%lu/%lu\n", frame_ok, total);
    printf("payload_bytes_match=%lu/%lu\n", payload_ok, total);
    if (total == 0)
        return 1;
    return (payload_ok == total && frame_ok == total && header_ok == total)
        ? 0 : 1;
}
