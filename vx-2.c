/*
 * Interface to Yaesu VX-2R, VX-2E.
 *
 * Copyright (C) 2018 Serge Vakulenko, KK6ABQ
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. The name of the author may not be used to endorse or promote products
 *      derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include "radio.h"
#include "util.h"

#define NCHAN           1000
#define NBANKS          20
#define NPMS            50
#define MEMSZ           32594

#define OFFSET_BUSE1    0x005a  // 0xffff when banks unused
#define OFFSET_BUSE2    0x00da  // 0xffff when banks unused
#define OFFSET_BNCHAN   0x016a  // 20 banks, 2 bytes per bank: 0xffff when bank[i] unused
#define OFFSET_WX       0x0396  // 10 WX channel names
#define OFFSET_HOME     0x03d2  // 12 home channels
#define OFFSET_VFO      0x04e2  // 12 variable frequency channels
#define OFFSET_BANKS    0x05c2  // 20 banks, 100 channels, 2 bytes per channel
#define OFFSET_FLAGS    0x1562  // 500 bytes: four bits per channel
#define OFFSET_CHANNELS 0x17c2  // 1000 memory channels
#define OFFSET_PMS      0x5e12  // 50 channel pairs: programmable memory scan

static const char CHARSET[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-/[]";

#define NCHARS  42
#define SPACE   36

static const char *POWER_NAME[] = { "High", "Low", "High", "Low" };

static const char *SCAN_NAME[] = { "+", "-", "Only", "??" };

static const char *MOD_NAME[] = { "FM", "AM", "WFM", "Auto", "NFM" };

static const char *STEP_NAME[] = { "5", "10", "12.5", "15", "20", "25", "50", "100", "9" };

//
// Tuning frequency step, for VFO and Home channels.
//
enum {
    STEP_5 = 0,         // 5 kHz
    STEP_10,            // 10 kHz
    STEP_12_5,          // 12.5 kHz
    STEP_15,            // 15 kHz
    STEP_20,            // 20 kHz
    STEP_25,            // 25 kHz
    STEP_50,            // 50 kHz
    STEP_100,           // 100 kHz
    STEP_9,             // 9 kHz, for MW band
};

//
// Channels flags.
// Stored in a separate memory, 4 bits per channel, total 500 bytes.
//
enum {
    FLAG_UNMASKED = 1,  // Unmasked, see 'Masking Memories' in the Operating manual
    FLAG_VALID    = 2,  // Channel contains valid data
    FLAG_SKIP     = 4,  // Skip this channel during Memory Scan
    FLAG_PSKIP    = 8,  // Skip=Only, for Preferential Memory Scan
};

//
// Scan flags.
//
enum {
    SCAN_NORMAL = 0,    // Normal scan, Skip=Off
    SCAN_SKIP,          // Skip this channel
    SCAN_PREFERENTIAL,  // Preferential scan, Skip=Only
};

//
// Data structure for a memory channel.
//
typedef struct {
    uint8_t     _u1       : 4,
                clk       : 1,  // CPU clock shift
                isnarrow  : 1,  // Narrow FM modulation
                _u2       : 2;
    uint8_t     step      : 4,  // Tune frequency step, for Home channels
                duplex    : 2,  // Direction of repeater offset
#define D_SIMPLEX       0
#define D_NEG_OFFSET    1
#define D_POS_OFFSET    2
#define D_DUPLEX        3
                amfm      : 2;  // Modulation
#define MOD_FM          0
#define MOD_AM          1
#define MOD_WFM         2
#define MOD_AUTO        3
#define MOD_NFM         4       // Narrow FM (txnarrow=1)
    uint8_t     rxfreq[3];      // Receive frequency
    uint8_t     tmode     : 2,  // CTCSS/DCS mode
#define T_OFF           0
#define T_TONE          1
#define T_TSQL          2
#define T_DTCS          3
                _u3       : 4,
                power     : 2;  // Transmit power level
#define PWR_HIGH        0
#define PWR_LOW         3
    uint8_t     name[6];        // Channel name
    uint8_t     offset[3];      // Transmit frequency offset
    uint8_t     tone      : 6,  // CTCSS tone select
#define TONE_DEFAULT    12
                _u4       : 2;
    uint8_t     dcs       : 7,  // DCS code select
                _u5       : 1;
    uint8_t     _u6;            // 0x0d for unused channel?
} memory_channel_t;

//
// Example:
// 1: 05 02 44 38 75 c0 24 24 24 24 24 24 00 76 00 0c 00 00 - 443.875 MHz
// 2: 05 02 45 76 50 c0 ff ff ff ff ff ff 00 76 00 0c 00 0d - unused
// 3: 05 02 15 01 50 00 24 24 24 24 24 24 00 06 00 0c 00 00 - 150.150 MHz
// 4: 05 02 45 80 00 c0 ff ff ff ff ff ff 00 76 00 0c 00 0d - unused
//

//
// Home channels:
// 1:  02 48 00 05 40 c0 ff ff ff ff ff ff 00 00 00 0c 00 0d
// 2:  00 40 00 18 00 c0 ff ff ff ff ff ff 00 00 00 0c 00 0d
// 3:  00 00 03 00 00 c0 ff ff ff ff ff ff 00 00 00 0c 00 0d
// 4:  05 87 08 80 00 c0 ff ff ff ff ff ff 00 00 00 0c 00 0d
// 5:  05 86 17 40 00 c0 ff ff ff ff ff ff 00 00 00 0c 00 0d
// 6:  05 45 10 80 00 c0 ff ff ff ff ff ff 00 00 00 0c 00 0d
// 7:  05 02 14 40 00 c0 ff ff ff ff ff ff 00 06 00 0c 00 0d
// 8:  05 86 17 40 00 c0 ff ff ff ff ff ff 00 00 00 0c 00 0d
// 9:  05 02 23 00 00 c0 ff ff ff ff ff ff 00 00 00 0c 00 0d
// 10: 05 05 43 00 00 c0 ff ff ff ff ff ff 00 76 00 0c 00 0d
// 11: 05 86 47 00 00 c0 ff ff ff ff ff ff 00 00 00 0c 00 0d
// 12: 05 02 86 00 00 c0 ff ff ff ff ff ff 00 00 00 0c 00 0d
//

//
// Print a generic information about the device.
//
static void vx2_print_version(FILE *out)
{
    // Nothing to print.
}

//
// Read block of data, up to 64 bytes.
// When start==0, return non-zero on success or 0 when empty.
// When start!=0, halt the program on any error.
//
static int read_block(int fd, int start, unsigned char *data, int datalen)
{
    unsigned char reply;
    int len, nbytes;
    int need_ack = (datalen <= 16);

again:
    // Read chunk of data.
    nbytes = (datalen < 64) ? datalen : 64;
    len = serial_read(fd, data, nbytes);
    if (len != nbytes) {
        if (start == 0)
            return 0;
        fprintf(stderr, "Reading block 0x%04x: got only %d bytes.\n", start, len);
        exit(-1);
    }

    if (need_ack) {
        // Send acknowledge.
        serial_write(fd, "\x06", 1);
        if (serial_read(fd, &reply, 1) != 1) {
            fprintf(stderr, "No acknowledge after block 0x%04x.\n", start);
            exit(-1);
        }
        if (reply != 0x06) {
            fprintf(stderr, "Bad acknowledge after block 0x%04x: %02x\n", start, reply);
            exit(-1);
        }
    }

    if (serial_verbose) {
        printf("# Read 0x%04x: ", start);
        print_hex(data, nbytes);
        printf("\n");
    } else {
        ++radio_progress;
        if (radio_progress % 16 == 0) {
            fprintf(stderr, "#");
            fflush(stderr);
        }
    }

    if (nbytes < datalen) {
        // Next chunk.
        start += nbytes;
        data += nbytes;
        datalen -= nbytes;
        goto again;
    }
    return 1;
}

//
// Write block of data, up to 64 bytes.
// Halt the program on any error.
// Return 0 on error.
//
static int write_block(int fd, int start, const unsigned char *data, int datalen)
{
    unsigned char reply[64];
    int len, nbytes;
    int need_ack = (datalen <= 16);

again:
    // Write chunk of data.
    nbytes = (datalen < 64) ? datalen : 64;
    serial_write(fd, data, nbytes);

    // Get echo.
    len = serial_read(fd, reply, nbytes);
    if (len != nbytes) {
        fprintf(stderr, "! Echo for block 0x%04x: got only %d bytes.\n", start, len);
        return 0;
    }

    if (need_ack) {
        // Get acknowledge.
        if (serial_read(fd, reply, 1) != 1) {
            fprintf(stderr, "! No acknowledge after block 0x%04x.\n", start);
            return 0;
        }
        if (reply[0] != 0x06) {
            fprintf(stderr, "! Bad acknowledge after block 0x%04x: %02x\n", start, reply[0]);
            return 0;
        }
    }

    if (serial_verbose) {
        printf("# Write 0x%04x: ", start);
        print_hex(data, nbytes);
        printf("\n");
    } else {
        ++radio_progress;
        if (radio_progress % 16 == 0) {
            fprintf(stderr, "#");
            fflush(stderr);
        }
    }

    if (nbytes < datalen) {
        // Next chunk.
        start += nbytes;
        data += nbytes;
        datalen -= nbytes;
        usleep(60000);
        goto again;
    }
    return 1;
}

//
// Read memory image from the device.
//
static void vx2_download()
{
    int addr, sum;

    if (serial_verbose)
        fprintf(stderr, "\nPlease follow the procedure:\n");
    else
        fprintf(stderr, "please follow the procedure.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "1. Power Off the VX-2.\n");
    fprintf(stderr, "2. Hold down the F/W key and Power On the VX-2. \n");
    fprintf(stderr, "   CLONE wil appear on the display.\n");
    fprintf(stderr, "3. Press the BAND key until the radio starts to send.\n");
    fprintf(stderr, "-- Or enter ^C to abort the memory read.\n");
again:
    fprintf(stderr, "\n");
    fprintf(stderr, "Waiting for data... ");
    fflush(stderr);

    // Wait for the first 10 bytes.
    while (read_block(radio_port, 0, &radio_mem[0], 10) == 0)
        continue;

    // Wait for the next 8 bytes.
    while (read_block(radio_port, 10, &radio_mem[10], 8) == 0)
        continue;

    // Get the rest of data, and checksum.
    read_block(radio_port, 18, &radio_mem[18], MEMSZ - 18 + 1);

    // Verify the checksum.
    sum = 0;
    for (addr=0; addr<MEMSZ; addr++)
        sum += radio_mem[addr];
    sum &= 0xff;
    if (sum != radio_mem[MEMSZ]) {
        if (serial_verbose) {
            printf("Bad checksum = %02x, expected %02x\n", sum, radio_mem[MEMSZ]);
            fprintf(stderr, "BAD CHECKSUM!\n");
        } else
            fprintf(stderr, "[BAD CHECKSUM]\n");
        fprintf(stderr, "Please, repeat the procedure:\n");
        fprintf(stderr, "Press and hold the PTT switch until the radio starts to send.\n");
        fprintf(stderr, "Or enter ^C to abort the memory read.\n");
        goto again;
    }
    if (serial_verbose)
        printf("Checksum = %02x (OK)\n", radio_mem[MEMSZ]);
}

//
// Write memory image to the device.
//
static void vx2_upload(int cont_flag)
{
    int addr, sum;
    char buf[80];

    if (serial_verbose)
        fprintf(stderr, "\nPlease follow the procedure:\n");
    else
        fprintf(stderr, "please follow the procedure.\n");
    fprintf(stderr, "\n");
    if (cont_flag) {
        fprintf(stderr, "1. Press the V/M key until the radio starts to receive.\n");
        fprintf(stderr, "   WAIT will appear on the display.\n");
        fprintf(stderr, "2. Press <Enter> to continue.\n");
    } else {
        fprintf(stderr, "1. Power Off the VX-2.\n");
        fprintf(stderr, "2. Hold down the F/W key and Power On the VX-2. \n");
        fprintf(stderr, "   CLONE will appear on the display.\n");
        fprintf(stderr, "3. Press the V/M key until the radio starts to receive.\n");
        fprintf(stderr, "4. Press <Enter> to continue.\n");
    }
    fprintf(stderr, "-- Or enter ^C to abort the memory write.\n");
again:
    fprintf(stderr, "\n");
    fprintf(stderr, "Press <Enter> to continue: ");
    fflush(stderr);
    serial_flush(radio_port);
    if (! fgets(buf, sizeof(buf), stdin))
	/*ignore*/;
    fprintf(stderr, "Sending data... ");
    serial_flush(radio_port);
    fflush(stderr);

    if (! write_block(radio_port, 0, &radio_mem[0], 10)) {
error:  fprintf(stderr, "\nPlease, repeat the procedure:\n");
        fprintf(stderr, "1. Press the V/M key until the radio starts to receive.\n");
        fprintf(stderr, "2. Press <Enter> to continue.\n");
        fprintf(stderr, "-- Or enter ^C to abort the memory write.\n");
        goto again;
    }
    usleep(500000);
    if (! write_block(radio_port, 10, &radio_mem[10], 8))
        goto error;

    // Compute the checksum.
    sum = 0;
    for (addr=0; addr<MEMSZ; addr++)
        sum += radio_mem[addr];
    radio_mem[MEMSZ] = sum;

    usleep(500000);
    if (! write_block(radio_port, 18, &radio_mem[18], MEMSZ - 18 + 1))
        goto error;

    usleep(200000);
}

//
// Check whether the memory image is compatible with this device.
//
static int vx2_is_compatible()
{
    return strncmp("AH015$", (char*)&radio_mem[0], 6) == 0;
}

//
// Round double value to integer.
//
static int iround(double x)
{
    if (x >= 0)
        return (int)(x + 0.5);

    return -(int)(-x + 0.5);
}

//
// Convert squelch string to CTCSS tone index.
// Return -1 on error.
// Format: nnn.n
//
static int encode_tone(char *str)
{
    unsigned val;

    // CTCSS tone
    float hz;
    if (sscanf(str, "%f", &hz) != 1)
        return -1;

    // Round to integer.
    val = iround(hz * 10.0);
    if (val < 0x0258)
        return -1;

    // Find a valid index in CTCSS table.
    int i;
    for (i=0; i<NCTCSS; i++)
        if (CTCSS_TONES[i] == val)
            return i;
    return -1;
}

//
// Convert squelch string to DCS code index.
// Return -1 on error.
// Format: Dnnn
//
static int encode_dcs(char *str)
{
    unsigned val;

    // DCS tone
    if (sscanf(++str, "%u", &val) != 1)
        return -1;

    // Find a valid index in DCS table.
    int i;
    for (i=0; i<NDCS; i++)
        if (DCS_CODES[i] == val)
            return i;
    return -1;
}

//
// Convert squelch strings to tmode value, tone index and dcs index.
//
static int encode_squelch(char *rx, char *tx, int *tone, int *dcs)
{
    int rx_tone = -1, tx_tone = -1, tx_dcs = -1;

    if (*tx == 'D' || *tx == 'd') {             // Transmit DCS code
        tx_dcs = encode_dcs(tx);
    } else if (*tx >= '0' && *tx <= '9') {      // Transmit CTCSS tone
        tx_tone = encode_tone(tx);
    }

    if (*rx >= '0' && *rx <= '9') {             // Receive CTCSS tone
        rx_tone = encode_tone(rx);
    }

    // Encode tmode.
    *tone = TONE_DEFAULT;
    *dcs = 0;
    if (tx_dcs >= 0) {
        *dcs = tx_dcs;
        return T_DTCS;
    }
    if (tx_tone >= 0) {
        *tone = tx_tone;
        if (rx_tone < 0)
            return T_TONE;
        return T_TSQL;
    }
    return T_OFF;
}

//
// Convert a 3-byte frequency value from binary coded decimal
// to integer format (in Hertz).
//
static int freq_to_hz(uint8_t *bcd)
{
    int a  = bcd[0] >> 4;
    int b  = bcd[0] & 15;
    int c  = bcd[1] >> 4;
    int d  = bcd[1] & 15;
    int e  = bcd[2] >> 4;
    int f  = bcd[2] & 15;
    int hz = (((((a*10 + b) * 10 + c) * 10 + d) * 10 + e) * 10 + f) * 1000;

    if (f == 2 || f == 7)
        hz += 500;
    return hz;
}

//
// Convert an integet frequency value (in Hertz)
// to a 3-byte binary coded decimal format.
//
static void hz_to_freq(int hz, uint8_t *bcd)
{
    if (hz == 0) {
        bcd[0] = bcd[1] = bcd[2] = 0xff;
        return;
    }

    bcd[0] = (hz / 100000000 % 10) << 4 |
             (hz / 10000000  % 10);
    bcd[1] = (hz / 1000000   % 10) << 4 |
             (hz / 100000    % 10);
    bcd[2] = (hz / 10000     % 10) << 4 |
             (hz / 1000      % 10);
}

//
// Convert 16-bit word to/from big endian.
//
static inline uint16_t big_endian_16(uint16_t x)
{
#if __BIG_ENDIAN__ == 1 || __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return x;
#elif __LITTLE_ENDIAN__ == 1 || __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return (x >> 8) | (x << 8);
#else
#   error Byte order undefined!
#endif
}

//
// Print one line of Banks table.
//
static void print_bank(FILE *out, int i)
{
    int       nchan = big_endian_16(*(uint16_t*) &radio_mem[OFFSET_BNCHAN + i*2]);
    uint16_t *data  = (uint16_t*) &radio_mem[OFFSET_BANKS + i * 200];
    int       last  = -1;
    int       range = 0;
    int       n;

    if (nchan < 100) {
        fprintf(out, "%4d    ", i + 1);
        for (n=0; n<=nchan; n++) {
            int cnum = 1 + big_endian_16(data[n]);

            if (cnum == last+1) {
                range = 1;
            } else {
                if (range) {
                    fprintf(out, "-%d", last);
                    range = 0;
                }
                if (n > 0)
                    fprintf(out, ",");
                fprintf(out, "%d", cnum);
            }
            last = cnum;
        }
        if (range)
            fprintf(out, "-%d", last);
        fprintf(out, "\n");
    }
}

//
// Set the bitmask of banks for a given channel.
// Return 0 on failure.
//
static void setup_bank(int bank_index, int chan_index)
{
    uint16_t *data = (uint16_t*) &radio_mem[OFFSET_BANKS + bank_index * 200];
    int n;

    // Find first empty slot.
    for (n=0; n<100; n++) {
        if (data[n] == 0xffff) {
            data[n] = big_endian_16(chan_index);
            return;
        }
    }
    fprintf(stderr, "Bank %d: too many channels.\n", bank_index + 1);
    exit(-1);
}

//
// Extract channel name.
//
static void decode_name(uint8_t *internal, char *name)
{
    if ((internal[0] & 0x7f) < NCHARS) {
        int n, c;
        for (n=0; n<6; n++) {
            c = internal[n] & 0x7f;
            name[n] = (c < NCHARS) ? CHARSET[c] : ' ';

            // Replace spaces by underscore.
            if (name[n] == ' ')
                name[n] = '_';
        }
        // Strip trailing spaces.
        for (n=5; n>=0 && name[n]=='_'; n--)
            name[n] = 0;
        name[6] = 0;
    }
}

//
// Encode a character from ASCII to internal index.
// Replace underscores by spaces.
// Make all letters uppercase.
//
static int encode_char(int c)
{
    int i;

    // Replace underscore by space.
    if (c == '_')
        c = ' ';
    if (c >= 'a' && c <= 'z')
        c += 'A' - 'a';
    for (i=0; i<NCHARS; i++)
        if (c == CHARSET[i])
            return i;
    return SPACE;
}

//
// Set a name for the channel.
//
static void encode_name(uint8_t *internal, char *name)
{
    int n;

    if (!name || !*name || *name == '-')
        name = "      ";

    // Setup channel name.
    for (n=0; n<6 && name[n]; n++) {
        internal[n] = encode_char(name[n]);
    }
    for (; n<6; n++) {
        internal[n] = SPACE;
    }

    if (internal[0] != SPACE) {
        // Display name.
        internal[0] |= 0x80;
    }
}

//
// Get channel flags.
//
static int get_flags(int i)
{
    int flags = radio_mem[OFFSET_FLAGS + i/2];

    if (i & 1)
        flags >>= 4;
    return flags & 0xf;
}

//
// Set channel flags.
//
static void set_flags(int i, int flags)
{
    unsigned char *ptr = &radio_mem[OFFSET_FLAGS + i/2];
    int shift = (i & 1) * 4;

    *ptr &= ~(0xf << shift);
    *ptr |= flags << shift;
}

//
// Get all parameters for a given channel.
// Seek selects the type of channel:
//  OFFSET_VFO      - VFO channel, 0..4
//  OFFSET_HOME     - home channel, 0..4
//  OFFSET_CHANNELS - memory channel, 0..999
//  OFFSET_PMS      - programmable memory scan, i=0..99
//
static void decode_channel(int i, int seek, char *name,
    int *rx_hz, int *tx_hz, int *rx_ctcs, int *tx_ctcs,
    int *rx_dcs, int *tx_dcs, int *power,
    int *scan, int *amfm, int *step)
{
    memory_channel_t *ch = i + (memory_channel_t*) &radio_mem[seek];
    int flags = get_flags(i + (seek == OFFSET_PMS ? NCHAN : 0));

    *rx_hz = *tx_hz = *rx_ctcs = *tx_ctcs = *rx_dcs = *tx_dcs = 0;
    *power = *scan = *amfm = *step = 0;
    if (name)
        *name = 0;
    if (seek == OFFSET_CHANNELS || seek == OFFSET_PMS) {
        // Check flags.
        if (! (flags & FLAG_VALID))
            return;
    }

    // Extract channel name.
    if (name && seek == OFFSET_CHANNELS)
        decode_name(ch->name, name);

    // Decode channel frequencies.
    *rx_hz = freq_to_hz(ch->rxfreq);

    *tx_hz = *rx_hz;
    switch (ch->duplex) {
    case D_NEG_OFFSET:
        *tx_hz -= freq_to_hz(ch->offset);
        break;
    case D_POS_OFFSET:
        *tx_hz += freq_to_hz(ch->offset);
        break;
    case D_DUPLEX:
        *tx_hz = freq_to_hz(ch->offset);
        break;
    }

    // Decode squelch modes.
    switch (ch->tmode) {
    case T_TONE:
        *tx_ctcs = CTCSS_TONES[ch->tone];
        break;
    case T_TSQL:
        *tx_ctcs = CTCSS_TONES[ch->tone];
        *rx_ctcs = CTCSS_TONES[ch->tone];
        break;
    case T_DTCS:
        *tx_dcs = DCS_CODES[ch->dcs];
        *rx_dcs = DCS_CODES[ch->dcs];
        break;
    }

    // Other parameters.
    *power = ch->power;
    *scan = (flags & FLAG_PSKIP) ? SCAN_PREFERENTIAL :
            (flags & FLAG_SKIP)  ? SCAN_SKIP :
                                   SCAN_NORMAL;
    *amfm = ch->isnarrow ? MOD_NFM : ch->amfm;
    *step = ch->step;
}

//
// Set the parameters for a given memory channel.
//
static void setup_channel(int i, char *name, double rx_mhz, double tx_mhz,
    int tmode, int tone, int dcs, int power, int scan, int amfm)
{
    memory_channel_t *ch = i + (memory_channel_t*) &radio_mem[OFFSET_CHANNELS];
    int flags = FLAG_VALID | FLAG_UNMASKED;

    hz_to_freq(iround(rx_mhz * 1000000.0), ch->rxfreq);

    int offset_khz = iround((tx_mhz - rx_mhz) * 1000.0);
    ch->offset[0] = ch->offset[1] = ch->offset[2] = 0;
    if (offset_khz == 0) {
        ch->duplex = D_SIMPLEX;
    } else if (offset_khz > 0 && offset_khz < 100000) {
        ch->duplex = D_POS_OFFSET;
        hz_to_freq(offset_khz * 1000, ch->offset);
    } else if (offset_khz < 0 && -offset_khz < 100000) {
        ch->duplex = D_NEG_OFFSET;
        hz_to_freq(-offset_khz * 1000, ch->offset);
    } else {
        ch->duplex = D_DUPLEX;
        hz_to_freq(iround(tx_mhz * 1000000.0), ch->offset);
    }
    ch->tmode = tmode;
    ch->tone = tone;
    ch->dcs = dcs;
    ch->power = power;
    ch->isnarrow = (amfm == MOD_NFM);
    ch->amfm = amfm;
    ch->step = STEP_12_5;
    ch->clk = 0;
    ch->_u1 = (rx_mhz < 1.8) ? 2 :
              (rx_mhz < 88)  ? 0 : 5;
    ch->_u2 = 0;
    ch->_u3 = 0;
    ch->_u4 = 0;
    ch->_u5 = 0;
    ch->_u6 = 0;
    encode_name(ch->name, name);

    switch (scan) {
    case SCAN_SKIP:         flags |= FLAG_SKIP;  break;
    case SCAN_PREFERENTIAL: flags |= FLAG_PSKIP; break;
    }
    set_flags(i, flags);
}

//
// Set the parameters for a given home channel.
// Band selects the channel:
//      band  = 1 2 3 4 5 6 7 8 9 10 11
//      index = 0 1 2 3 5 6 7 8 9 10 11
// Channel index 4 is not used.
//
static void setup_home(int band, double rx_mhz, double tx_mhz,
    int tmode, int tone, int dcs, int power, int amfm, int step)
{
    // Skip home channel index #4.
    int index = (band <= 4) ? band-1 : band;
    memory_channel_t *ch = index + (memory_channel_t*) &radio_mem[OFFSET_HOME];

    hz_to_freq(iround(rx_mhz * 1000000.0), ch->rxfreq);

    int offset_khz = iround((tx_mhz - rx_mhz) * 1000.0);
    ch->offset[0] = ch->offset[1] = ch->offset[2] = 0;
    if (offset_khz == 0) {
        ch->duplex = D_SIMPLEX;
    } else if (offset_khz > 0 && offset_khz < 100000) {
        ch->duplex = D_POS_OFFSET;
        hz_to_freq(offset_khz * 1000, ch->offset);
    } else if (offset_khz < 0 && -offset_khz < 100000) {
        ch->duplex = D_NEG_OFFSET;
        hz_to_freq(-offset_khz * 1000, ch->offset);
    } else {
        ch->duplex = D_DUPLEX;
        hz_to_freq(iround(tx_mhz * 1000000.0), ch->offset);
    }
    ch->tmode = tmode;
    ch->tone = tone;
    ch->dcs = dcs;
    ch->power = power;
    ch->isnarrow = (amfm == MOD_NFM);
    ch->amfm = amfm;
    ch->step = step;
    ch->clk = 0;
    ch->_u1 = (rx_mhz < 1.8) ? 2 :
              (rx_mhz < 88)  ? 0 : 5;
    ch->_u2 = 0;
    ch->_u3 = 0;
    ch->_u4 = 0;
    ch->_u5 = 0;
    ch->_u6 = 0;
    encode_name(ch->name, "      ");
}

static void setup_vfo(int band, double rx_mhz, double tx_mhz,
    int tmode, int tone, int dcs, int power, int amfm, int step)
{
    // Skip home channel index #4.
    int index = (band <= 4) ? band-1 : band;
    memory_channel_t *ch = index + (memory_channel_t*) &radio_mem[OFFSET_VFO];

    hz_to_freq(iround(rx_mhz * 1000000.0), ch->rxfreq);

    int offset_khz = iround((tx_mhz - rx_mhz) * 1000.0);
    ch->offset[0] = ch->offset[1] = ch->offset[2] = 0;
    if (offset_khz == 0) {
        ch->duplex = D_SIMPLEX;
    } else if (offset_khz > 0 && offset_khz < 100000) {
        ch->duplex = D_POS_OFFSET;
        hz_to_freq(offset_khz * 1000, ch->offset);
    } else if (offset_khz < 0 && -offset_khz < 100000) {
        ch->duplex = D_NEG_OFFSET;
        hz_to_freq(-offset_khz * 1000, ch->offset);
    } else {
        ch->duplex = D_DUPLEX;
        hz_to_freq(iround(tx_mhz * 1000000.0), ch->offset);
    }
    ch->tmode = tmode;
    ch->tone = tone;
    ch->dcs = dcs;
    ch->power = power;
    ch->isnarrow = (amfm == MOD_NFM);
    ch->amfm = amfm;
    ch->step = step;
    ch->clk = 0;
    ch->_u1 = (rx_mhz < 1.8) ? 2 :
              (rx_mhz < 88)  ? 0 : 5;
    ch->_u2 = 0;
    ch->_u3 = 0;
    ch->_u4 = 0;
    ch->_u5 = 0;
    ch->_u6 = 0;
    encode_name(ch->name, "      ");
}

//
// Set the parameters for a given PMS pair.
//
static void setup_pms(int i, double mhz)
{
    memory_channel_t *ch = i + (memory_channel_t*) &radio_mem[OFFSET_PMS];

    hz_to_freq(iround(mhz * 1000000.0), ch->rxfreq);

    ch->offset[0] = ch->offset[1] = ch->offset[2] = 0;
    ch->duplex = D_SIMPLEX;
    ch->tmode = 0;
    ch->tone = 0;
    ch->dcs = 0;
    ch->power = 0;
    ch->isnarrow = 0;
    ch->amfm = 0;
    ch->step = STEP_12_5;
    ch->clk = 0;
    ch->_u1 = 5;
    ch->_u2 = 0;
    ch->_u3 = 0;
    ch->_u4 = 0;
    ch->_u5 = 0;
    ch->_u6 = 0;
    encode_name(ch->name, "      ");

    set_flags(NCHAN + i, FLAG_VALID | FLAG_UNMASKED);
}

//
// Print the transmit offset or frequency.
//
static void print_offset(FILE *out, int rx_hz, int tx_hz)
{
    int delta = tx_hz - rx_hz;
    int can_transmit = (rx_hz >= 137000000 && rx_hz < 174000000) ||
                       (rx_hz >= 420000000 && rx_hz < 470000000);

    if (!can_transmit) {
        fprintf(out, " -      ");
    } else if (delta == 0) {
        fprintf(out, "+0      ");
    } else if (delta > 0 && delta/50000 <= 255) {
        if (delta % 1000000 == 0)
            fprintf(out, "+%-7u", delta / 1000000);
        else
            fprintf(out, "+%-7.3f", delta / 1000000.0);
    } else if (delta < 0 && -delta/50000 <= 255) {
        delta = - delta;
        if (delta % 1000000 == 0)
            fprintf(out, "-%-7u", delta / 1000000);
        else
            fprintf(out, "-%-7.3f", delta / 1000000.0);
    } else {
        // Cross band mode.
        fprintf(out, " %-7.4f", tx_hz / 1000000.0);
    }
}

//
// Print the squelch value: CTCSS or DCS.
//
static void print_squelch(FILE *out, int ctcs, int dcs)
{
    if      (ctcs)    fprintf(out, "%5.1f", ctcs / 10.0);
    else if (dcs > 0) fprintf(out, "D%03d", dcs);
    else              fprintf(out, "   - ");
}

//
// Print full information about the device configuration.
//
static void vx2_print_config(FILE *out, int verbose)
{
    int i;

    //
    // Radio identification and hardware options.
    //
    fprintf(out, "Radio: Yaesu VX-2\n");
    fprintf(out, "Virtual Jumpers: %02x %02x %02x %02x\n",
        radio_mem[6], radio_mem[7], radio_mem[8], radio_mem[13]);

    //
    // Memory channels.
    //
    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Table of preprogrammed channels.\n");
        fprintf(out, "# 1) Channel number: 1-%d\n", NCHAN);
        fprintf(out, "# 2) Name: up to 6 characters, no spaces\n");
        fprintf(out, "# 3) Receive frequency in MHz\n");
        fprintf(out, "# 4) Transmit frequency or +/- offset in MHz\n");
        fprintf(out, "# 5) Squelch tone for receive, or '-' to disable\n");
        fprintf(out, "# 6) Squelch tone for transmit, or '-' to disable\n");
        fprintf(out, "# 7) Transmit power: High, Low\n");
        fprintf(out, "# 8) Modulation: FM, AM, WFM, NFM, Auto\n");
        fprintf(out, "# 9) Scan mode: +, -, Only\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Channel Name    Receive  Transmit R-Squel T-Squel Power Modulation Scan\n");
    for (i=0; i<NCHAN; i++) {
        int rx_hz, tx_hz, rx_ctcs, tx_ctcs, rx_dcs, tx_dcs;
        int power, scan, amfm, step;
        char name[17];

        decode_channel(i, OFFSET_CHANNELS, name, &rx_hz, &tx_hz, &rx_ctcs, &tx_ctcs,
            &rx_dcs, &tx_dcs, &power, &scan, &amfm, &step);
        if (rx_hz == 0) {
            // Channel is disabled
            continue;
        }

        fprintf(out, "%5d   %-7s %7.3f  ", i+1, name[0] ? name : "-", rx_hz / 1000000.0);
        print_offset(out, rx_hz, tx_hz);
        fprintf(out, " ");
        print_squelch(out, rx_ctcs, rx_dcs);
        fprintf(out, "   ");
        print_squelch(out, tx_ctcs, tx_dcs);

        fprintf(out, "   %-4s  %-10s %s\n", POWER_NAME[power],
            MOD_NAME[amfm], SCAN_NAME[scan]);
    }
    if (verbose)
        print_squelch_tones(out, 1);

    //
    // Banks.
    //
    uint16_t *bank_use1 = (uint16_t*) &radio_mem[OFFSET_BUSE1];
    uint16_t *bank_use2 = (uint16_t*) &radio_mem[OFFSET_BUSE2];

    if (*bank_use1 != 0xffff || *bank_use2 != 0xffff) {
        fprintf(out, "\n");
        if (verbose) {
            fprintf(out, "# Table of channel banks.\n");
            fprintf(out, "# 1) Bank number: 1-20\n");
            fprintf(out, "# 2) List of channels: numbers and ranges (N-M) separated by comma\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Bank    Channels\n");
        for (i=0; i<NBANKS; i++) {
            print_bank(out, i);
        }
    }

    //
    // VFO channels.
    //
    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Table of VFO mode frequencies.\n");
        fprintf(out, "# 1) Band number: 1-11\n");
        fprintf(out, "# 2) Receive frequency in MHz\n");
        fprintf(out, "# 3) Transmit frequency or +/- offset in MHz\n");
        fprintf(out, "# 4) Squelch tone for receive, or '-' to disable\n");
        fprintf(out, "# 5) Squelch tone for transmit, or '-' to disable\n");
        fprintf(out, "# 6) Dial step in KHz: 5, 9, 10, 12.5, 15, 20, 25, 50, 100\n");
        fprintf(out, "# 7) Transmit power: High, Low\n");
        fprintf(out, "# 8) Modulation: FM, AM, WFM, NFM, Auto\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "VFO     Receive  Transmit R-Squel T-Squel Step  Power Modulation\n");
    for (i=0; i<12; i++) {
        int rx_hz, tx_hz, rx_ctcs, tx_ctcs, rx_dcs, tx_dcs;
        int power, scan, amfm, step;
        int can_transmit = (i == 6) || (i == 9);
        int band = (i < 4) ? i+1 : i;

        if (i == 4) {
            // Skip home channel index #4.
            continue;
        }
        decode_channel(i, OFFSET_VFO, 0, &rx_hz, &tx_hz, &rx_ctcs, &tx_ctcs,
            &rx_dcs, &tx_dcs, &power, &scan, &amfm, &step);

        fprintf(out, "%4d   %8.3f  ", band, rx_hz / 1000000.0);
        print_offset(out, rx_hz, tx_hz);
        fprintf(out, " ");
        print_squelch(out, rx_ctcs, rx_dcs);
        fprintf(out, "   ");
        print_squelch(out, tx_ctcs, tx_dcs);

        fprintf(out, "   %-5s %-4s  %s\n", STEP_NAME[step],
            can_transmit ? POWER_NAME[power] : "-", MOD_NAME[amfm]);
    }

    //
    // Home channels.
    //
    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Table of home frequencies.\n");
        fprintf(out, "# 1) Band number: 1-11\n");
        fprintf(out, "# 2) Receive frequency in MHz\n");
        fprintf(out, "# 3) Transmit frequency or +/- offset in MHz\n");
        fprintf(out, "# 4) Squelch tone for receive, or '-' to disable\n");
        fprintf(out, "# 5) Squelch tone for transmit, or '-' to disable\n");
        fprintf(out, "# 6) Dial step in KHz: 5, 9, 10, 12.5, 15, 20, 25, 50, 100\n");
        fprintf(out, "# 7) Transmit power: High, Low\n");
        fprintf(out, "# 8) Modulation: FM, AM, WFM, NFM, Auto\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Home    Receive  Transmit R-Squel T-Squel Step  Power Modulation\n");
    for (i=0; i<12; i++) {
        int rx_hz, tx_hz, rx_ctcs, tx_ctcs, rx_dcs, tx_dcs;
        int power, scan, amfm, step;
        int can_transmit = (i == 6) || (i == 9);
        int band = (i < 4) ? i+1 : i;

        if (i == 4) {
            // Skip home channel index #4.
            continue;
        }
        decode_channel(i, OFFSET_HOME, 0, &rx_hz, &tx_hz, &rx_ctcs, &tx_ctcs,
            &rx_dcs, &tx_dcs, &power, &scan, &amfm, &step);

        fprintf(out, "%4d   %8.3f  ", band, rx_hz / 1000000.0);
        print_offset(out, rx_hz, tx_hz);
        fprintf(out, " ");
        print_squelch(out, rx_ctcs, rx_dcs);
        fprintf(out, "   ");
        print_squelch(out, tx_ctcs, tx_dcs);

        fprintf(out, "   %-5s %-4s  %s\n", STEP_NAME[step],
            can_transmit ? POWER_NAME[power] : "-", MOD_NAME[amfm]);
    }

    //
    // Programmable memory scan.
    //
    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Programmable memory scan: list of sub-band limits.\n");
        fprintf(out, "# 1) PMS pair number: 1-50\n");
        fprintf(out, "# 2) Lower frequency in MHz\n");
        fprintf(out, "# 3) Upper frequency in MHz\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "PMS     Lower    Upper\n");
    for (i=0; i<NPMS; i++) {
        int lower_hz, upper_hz, tx_hz, rx_ctcs, tx_ctcs, rx_dcs, tx_dcs;
        int power, scan, amfm, step;

        decode_channel(i*2, OFFSET_PMS, 0, &lower_hz, &tx_hz, &rx_ctcs, &tx_ctcs,
            &rx_dcs, &tx_dcs, &power, &scan, &amfm, &step);
        decode_channel(i*2+1, OFFSET_PMS, 0, &upper_hz, &tx_hz, &rx_ctcs, &tx_ctcs,
            &rx_dcs, &tx_dcs, &power, &scan, &amfm, &step);
        if (lower_hz == 0 && upper_hz == 0)
            continue;

        fprintf(out, "%5d   ", i+1);
        if (lower_hz == 0)
            fprintf(out, "-       ");
        else
            fprintf(out, "%8.4f", lower_hz / 1000000.0);
        if (upper_hz == 0)
            fprintf(out, " -\n");
        else
            fprintf(out, " %8.4f\n", upper_hz / 1000000.0);
    }
}

//
// Read memory image from the binary file.
//
static void vx2_read_image(FILE *img)
{
    if (fread(&radio_mem[0], 1, MEMSZ+1, img) != MEMSZ+1) {
        fprintf(stderr, "Error reading image data.\n");
        exit(-1);
    }
}

//
// Save memory image to the binary file.
// File format is compatible with VX2 Commander.
//
static void vx2_save_image(FILE *img)
{
    fwrite(&radio_mem[0], 1, MEMSZ+1, img);
}

//
// Parse the scalar parameter.
//
static void vx2_parse_parameter(char *param, char *value)
{
    if (strcasecmp("Radio", param) == 0) {
        if (strcasecmp("Yaesu VX-2", value) != 0) {
            fprintf(stderr, "Bad value for %s: %s\n", param, value);
            exit(-1);
        }
        return;
    }
    if (strcasecmp("Virtual Jumpers", param) == 0) {
        int a, b, c, d;
        if (sscanf(value, "%x %x %x %x", &a, &b, &c, &d) != 4) {
            fprintf(stderr, "Wrong value: %s = %s\n", param, value);
            return;
        }
        radio_mem[10] = a;
        radio_mem[11] = b;
        radio_mem[12] = c;
        radio_mem[13] = d;
        return;
    }

    fprintf(stderr, "Unknown parameter: %s = %s\n", param, value);
    exit(-1);
}

//
// Check that the radio does support this frequency.
//
static int is_valid_frequency(double mhz)
{
    if (mhz >= 0.5 && mhz <= 999)
        return 1;
    return 0;
}

//
// Parse one line of memory channel table.
// Start_flag is 1 for the first table row.
// Return 0 on failure.
//
static int parse_channel(int first_row, char *line)
{
    char num_str[256], name_str[256], rxfreq_str[256], offset_str[256];
    char rq_str[256], tq_str[256], power_str[256], mod_str[256];
    char scan_str[256];
    int num, tmode, tone, dcs, power, scan, amfm;
    double rx_mhz, tx_mhz;

    if (sscanf(line, "%s %s %s %s %s %s %s %s %s",
        num_str, name_str, rxfreq_str, offset_str, rq_str, tq_str,
        power_str, mod_str, scan_str) != 9) {
        fprintf(stderr, "Wrong number of fields.\n");
        return 0;
    }

    num = atoi(num_str);
    if (num < 1 || num > NCHAN) {
        fprintf(stderr, "Bad channel number.\n");
        return 0;
    }

    if (sscanf(rxfreq_str, "%lf", &rx_mhz) != 1 ||
        ! is_valid_frequency(rx_mhz)) {
        fprintf(stderr, "Bad receive frequency.\n");
        return 0;
    }
    if (offset_str[0] == '-' && offset_str[1] == 0) {
        tx_mhz = rx_mhz;
    } else {
        if (sscanf(offset_str, "%lf", &tx_mhz) != 1) {
badtx:      fprintf(stderr, "Bad transmit frequency.\n");
            return 0;
        }
        if (offset_str[0] == '-' || offset_str[0] == '+')
            tx_mhz += rx_mhz;
        if (! is_valid_frequency(tx_mhz))
            goto badtx;
    }
    tmode = encode_squelch(rq_str, tq_str, &tone, &dcs);

    if (strcasecmp("High", power_str) == 0) {
        power = PWR_HIGH;
    } else if (strcasecmp("Low", power_str) == 0 ||
               strcasecmp("-", power_str) == 0) {
        power = PWR_LOW;
    } else {
        fprintf(stderr, "Bad power level.\n");
        return 0;
    }

    if (strcasecmp("FM", mod_str) == 0) {
        amfm = MOD_FM;
    } else if (strcasecmp("AM", mod_str) == 0) {
        amfm = MOD_AM;
    } else if (strcasecmp("WFM", mod_str) == 0) {
        amfm = MOD_WFM;
    } else if (strcasecmp("NFM", mod_str) == 0) {
        amfm = MOD_NFM;
    } else if (strcasecmp("Auto", mod_str) == 0) {
        amfm = MOD_AUTO;
    } else {
        fprintf(stderr, "Bad modulation.\n");
        return 0;
    }

    if (*scan_str == '+') {
        scan = SCAN_NORMAL;
    } else if (*scan_str == '-') {
        scan = SCAN_SKIP;
    } else if (strcasecmp("Only", scan_str) == 0) {
        scan = SCAN_PREFERENTIAL;
    } else {
        fprintf(stderr, "Bad scan flag.\n");
        return 0;
    }

    if (first_row) {
        // On first entry, erase the channel table.
        memset(&radio_mem[OFFSET_CHANNELS], 0xff, NCHAN * sizeof(memory_channel_t));
        memset(&radio_mem[OFFSET_FLAGS], 0, NCHAN/2);
    }

    setup_channel(num-1, name_str, rx_mhz, tx_mhz,
        tmode, tone, dcs, power, scan, amfm);
    return 1;
}

//
// Parse one line of home channel table.
// Return 0 on failure.
//
static int parse_home(int first_row, char *line)
{
    char band_str[256], rxfreq_str[256], offset_str[256], rq_str[256];
    char tq_str[256], power_str[256], mod_str[256], step_str[256];
    int band, tmode, tone, dcs, power, amfm, step;
    double rx_mhz, tx_mhz;

    if (sscanf(line, "%s %s %s %s %s %s %s %s",
        band_str, rxfreq_str, offset_str, rq_str, tq_str,
        step_str, power_str, mod_str) != 8)
        return 0;

    band = atoi(band_str);
    if (band < 1 || band > 11) {
        fprintf(stderr, "Incorrect band.\n");
        return 0;
    }

    if (sscanf(rxfreq_str, "%lf", &rx_mhz) != 1 ||
        ! is_valid_frequency(rx_mhz)) {
        fprintf(stderr, "Bad receive frequency.\n");
        return 0;
    }
    if (offset_str[0] == '-' && offset_str[1] == 0) {
        tx_mhz = rx_mhz;
    } else {
        if (sscanf(offset_str, "%lf", &tx_mhz) != 1) {
badtx:      fprintf(stderr, "Bad transmit frequency.\n");
            return 0;
        }
        if (offset_str[0] == '-' || offset_str[0] == '+')
            tx_mhz += rx_mhz;
        if (! is_valid_frequency(tx_mhz))
            goto badtx;
    }
    tmode = encode_squelch(rq_str, tq_str, &tone, &dcs);

    if (strcasecmp("High", power_str) == 0) {
        power = PWR_HIGH;
    } else if (strcasecmp("Low", power_str) == 0 ||
               strcasecmp("-", power_str) == 0) {
        power = PWR_LOW;
    } else {
        fprintf(stderr, "Bad power level.\n");
        return 0;
    }

    if (strcasecmp("FM", mod_str) == 0) {
        amfm = MOD_FM;
    } else if (strcasecmp("AM", mod_str) == 0) {
        amfm = MOD_AM;
    } else if (strcasecmp("WFM", mod_str) == 0) {
        amfm = MOD_WFM;
    } else if (strcasecmp("NFM", mod_str) == 0) {
        amfm = MOD_NFM;
    } else if (strcasecmp("Auto", mod_str) == 0) {
        amfm = MOD_AUTO;
    } else {
        fprintf(stderr, "Bad modulation.\n");
        return 0;
    }

    if (strcmp("5", step_str) == 0) {
        step = STEP_5;
    } else if (strcmp("9", step_str) == 0) {
        step = STEP_9;
    } else if (strcmp("10", step_str) == 0) {
        step = STEP_10;
    } else if (strcmp("12.5", step_str) == 0) {
        step = STEP_12_5;
    } else if (strcmp("15", step_str) == 0) {
        step = STEP_15;
    } else if (strcmp("20", step_str) == 0) {
        step = STEP_20;
    } else if (strcmp("25", step_str) == 0) {
        step = STEP_25;
    } else if (strcmp("50", step_str) == 0) {
        step = STEP_50;
    } else if (strcmp("100", step_str) == 0) {
        step = STEP_100;
    } else {
        fprintf(stderr, "Bad frequency step.\n");
        return 0;
    }

    setup_home(band, rx_mhz, tx_mhz, tmode, tone, dcs, power, amfm, step);
    return 1;
}

//
// Parse one line of VFO channel table.
// Return 0 on failure.
//
static int parse_vfo(int first_row, char *line)
{
    char band_str[256], rxfreq_str[256], offset_str[256], rq_str[256];
    char tq_str[256], power_str[256], mod_str[256], step_str[256];
    int band, tmode, tone, dcs, power, amfm, step;
    double rx_mhz, tx_mhz;

    if (sscanf(line, "%s %s %s %s %s %s %s %s",
        band_str, rxfreq_str, offset_str, rq_str, tq_str,
        step_str, power_str, mod_str) != 8)
        return 0;

    band = atoi(band_str);
    if (band < 1 || band > 11) {
        fprintf(stderr, "Incorrect band.\n");
        return 0;
    }

    if (sscanf(rxfreq_str, "%lf", &rx_mhz) != 1 ||
        ! is_valid_frequency(rx_mhz)) {
        fprintf(stderr, "Bad receive frequency.\n");
        return 0;
    }
    if (offset_str[0] == '-' && offset_str[1] == 0) {
        tx_mhz = rx_mhz;
    } else {
        if (sscanf(offset_str, "%lf", &tx_mhz) != 1) {
badtx:      fprintf(stderr, "Bad transmit frequency.\n");
            return 0;
        }
        if (offset_str[0] == '-' || offset_str[0] == '+')
            tx_mhz += rx_mhz;
        if (! is_valid_frequency(tx_mhz))
            goto badtx;
    }
    tmode = encode_squelch(rq_str, tq_str, &tone, &dcs);

    if (strcasecmp("High", power_str) == 0) {
        power = PWR_HIGH;
    } else if (strcasecmp("Low", power_str) == 0 ||
               strcasecmp("-", power_str) == 0) {
        power = PWR_LOW;
    } else {
        fprintf(stderr, "Bad power level.\n");
        return 0;
    }

    if (strcasecmp("FM", mod_str) == 0) {
        amfm = MOD_FM;
    } else if (strcasecmp("AM", mod_str) == 0) {
        amfm = MOD_AM;
    } else if (strcasecmp("WFM", mod_str) == 0) {
        amfm = MOD_WFM;
    } else if (strcasecmp("NFM", mod_str) == 0) {
        amfm = MOD_NFM;
    } else if (strcasecmp("Auto", mod_str) == 0) {
        amfm = MOD_AUTO;
    } else {
        fprintf(stderr, "Bad modulation.\n");
        return 0;
    }

    if (strcmp("5", step_str) == 0) {
        step = STEP_5;
    } else if (strcmp("9", step_str) == 0) {
        step = STEP_9;
    } else if (strcmp("10", step_str) == 0) {
        step = STEP_10;
    } else if (strcmp("12.5", step_str) == 0) {
        step = STEP_12_5;
    } else if (strcmp("15", step_str) == 0) {
        step = STEP_15;
    } else if (strcmp("20", step_str) == 0) {
        step = STEP_20;
    } else if (strcmp("25", step_str) == 0) {
        step = STEP_25;
    } else if (strcmp("50", step_str) == 0) {
        step = STEP_50;
    } else if (strcmp("100", step_str) == 0) {
        step = STEP_100;
    } else {
        fprintf(stderr, "Bad frequency step.\n");
        return 0;
    }

    setup_vfo(band, rx_mhz, tx_mhz, tmode, tone, dcs, power, amfm, step);
    return 1;
}

//
// Parse one line of PMS table.
// Return 0 on failure.
//
static int parse_pms(int first_row, char *line)
{
    char num_str[256], lower_str[256], upper_str[256];
    int num;
    double lower_mhz, upper_mhz;

    if (sscanf(line, "%s %s %s", num_str, lower_str, upper_str) != 3)
        return 0;

    num = atoi(num_str);
    if (num < 1 || num > NPMS) {
        fprintf(stderr, "Bad PMS number.\n");
        return 0;
    }
    if (sscanf(lower_str, "%lf", &lower_mhz) != 1 ||
        ! is_valid_frequency(lower_mhz)) {
        fprintf(stderr, "Bad lower frequency.\n");
        return 0;
    }
    if (sscanf(upper_str, "%lf", &upper_mhz) != 1 ||
        ! is_valid_frequency(upper_mhz)) {
        fprintf(stderr, "Bad upper frequency.\n");
        return 0;
    }

    if (first_row) {
        // On first entry, erase the PMS table.
        memset(&radio_mem[OFFSET_PMS], 0xff, NPMS * 2 * sizeof(memory_channel_t));
        memset(&radio_mem[OFFSET_FLAGS] + NCHAN/2, 0, NPMS);
    }
    setup_pms(num*2 - 2, lower_mhz);
    setup_pms(num*2 - 1, upper_mhz);
    return 1;
}

//
// Parse one line of Banks table.
// Return 0 on failure.
//
static int parse_banks(int first_row, char *line)
{
    char num_str[256], chan_str[256];
    int bnum;

    if (sscanf(line, "%s %s", num_str, chan_str) != 2)
        return 0;

    bnum = atoi(num_str);
    if (bnum < 1 || bnum > NBANKS) {
        fprintf(stderr, "Bad bank number.\n");
        return 0;
    }

    if (first_row) {
        // On first entry, erase the Banks table.
        memset(&radio_mem[OFFSET_BANKS], 0xff, NBANKS * 200);
        memset(&radio_mem[OFFSET_BNCHAN], 0xff, NBANKS * 2);
        memset(&radio_mem[OFFSET_BUSE1], 0xff, 2);
        memset(&radio_mem[OFFSET_BUSE2], 0xff, 2);
    }

    if (*chan_str == '-')
        return 1;

    char *str   = chan_str;
    int   nchan = 0;
    int   range = 0;
    int   last  = 0;

    // Parse channel list.
    for (;;) {
        char *eptr;
        int cnum = strtoul(str, &eptr, 10);

        if (eptr == str) {
            fprintf(stderr, "Bank %d: wrong channel list '%s'.\n", bnum, str);
            return 0;
        }
        if (cnum < 1 || cnum > NCHAN) {
            fprintf(stderr, "Bank %d: wrong channel number %d.\n", bnum, cnum);
            return 0;
        }

        if (range) {
            // Add range.
            int c;
            for (c=last; c<cnum; c++) {
                setup_bank(bnum-1, c);
                nchan++;
            }
        } else {
            // Add single channel.
            setup_bank(bnum-1, cnum-1);
            nchan++;
        }

        if (*eptr == 0)
            break;

        if (*eptr != ',' && *eptr != '-') {
            fprintf(stderr, "Bank %d: wrong channel list '%s'.\n", bnum, eptr);
            return 0;
        }
        range = (*eptr == '-');
        last = cnum;
        str = eptr + 1;
    }

    // Set number of channels in the bank.
    *(uint16_t*) &radio_mem[OFFSET_BNCHAN + (bnum-1)*2] = big_endian_16(nchan-1);

    // Clear unused flag.
    if (nchan > 0) {
        memset(&radio_mem[OFFSET_BUSE1], 0, 2);
        memset(&radio_mem[OFFSET_BUSE2], 0, 2);
    }
    return 1;
}

//
// Parse table header.
// Return table id, or 0 in case of error.
//
static int vx2_parse_header(char *line)
{
    if (strncasecmp(line, "Channel", 7) == 0)
        return 'C';
    if (strncasecmp(line, "Home", 4) == 0)
        return 'H';
    if (strncasecmp(line, "VFO", 3) == 0)
        return 'V';
    if (strncasecmp(line, "PMS", 3) == 0)
        return 'P';
    if (strncasecmp(line, "Bank", 4) == 0)
        return 'B';
    return 0;
}

//
// Parse one line of table data.
// Return 0 on failure.
//
static int vx2_parse_row(int table_id, int first_row, char *line)
{
    switch (table_id) {
    case 'C': return parse_channel(first_row, line);
    case 'H': return parse_home(first_row, line);
    case 'V': return parse_vfo(first_row, line);
    case 'P': return parse_pms(first_row, line);
    case 'B': return parse_banks(first_row, line);
    }
    return 0;
}

//
// Yaesu VX-2R, VX-2E
//
radio_device_t radio_vx2 = {
    "Yaesu VX-2",
    19200,
    vx2_download,
    vx2_upload,
    vx2_is_compatible,
    vx2_read_image,
    vx2_save_image,
    vx2_print_version,
    vx2_print_config,
    vx2_parse_parameter,
    vx2_parse_header,
    vx2_parse_row,
};
