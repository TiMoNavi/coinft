/* v52 stage J, test B: wire-format round-trip against every recorded frame.
 *
 * sim_verify_v52 has a selection bias by construction -- it only replays reads
 * that already matched a transmitted frame.  This test has none: it walks the
 * raw UART byte stream the device produced, and for every frame it finds it
 * decodes the payload, feeds those values into the simulator as record state,
 * re-encodes, and demands byte-identical output.
 *
 * A single disagreement anywhere in the capture fails the run, so passing means
 * the recovered frame encoding reproduces the device's own bytes exactly across
 * the whole capture, with nothing cherry-picked.
 *
 * Usage: sim_roundtrip_v52 <uart_capture.bin>
 */

#include "../../07_analysis/coinft_sim_v52.c"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    FILE *fp;
    long size;
    unsigned char *blob;
    long i;
    unsigned long frames = 0, roundtrip_ok = 0, resync = 0;
    coinft_sim_t sim;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <uart_capture.bin>\n", argv[0]);
        return 2;
    }
    fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        perror(argv[1]);
        return 2;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    blob = malloc((size_t)size);
    if (blob == NULL || fread(blob, 1, (size_t)size, fp) != (size_t)size) {
        fprintf(stderr, "read failed\n");
        free(blob);
        fclose(fp);
        return 2;
    }
    fclose(fp);

    coinft_sim_reset(&sim);
    coinft_sim_feed_byte(&sim, 's');

    i = 0;
    while (i + COINFT_SIM_FRAME_BYTES <= size) {
        const unsigned char *f = blob + i;
        uint16_t raw[COINFT_SIM_RECORDS];
        uint8_t produced[COINFT_SIM_FRAME_BYTES];
        unsigned c;

        if (f[0] != 0x02u || f[COINFT_SIM_FRAME_BYTES - 1u] != 0x03u) {
            ++resync;
            ++i;                     /* not a frame boundary; slide along */
            continue;
        }
        ++frames;

        for (c = 0u; c < COINFT_SIM_CHANNELS; ++c)
            raw[c] = (uint16_t)(f[1 + c * 2] | ((uint16_t)f[2 + c * 2] << 8));
        raw[12] = 2048u;             /* record 12 is never transmitted */

        coinft_sim_scan(&sim, raw, COINFT_SIM_RECORDS);
        if (coinft_sim_pack_frame(&sim, produced, sizeof produced) &&
            memcmp(produced, f, COINFT_SIM_FRAME_BYTES) == 0)
            ++roundtrip_ok;

        i += COINFT_SIM_FRAME_BYTES;
    }
    free(blob);

    printf("capture_bytes=%ld\n", size);
    printf("frames_found=%lu\n", frames);
    printf("resync_slides=%lu\n", resync);
    printf("roundtrip_identical=%lu/%lu\n", roundtrip_ok, frames);
    return (frames > 0 && roundtrip_ok == frames) ? 0 : 1;
}
