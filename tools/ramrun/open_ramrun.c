/*
 * 100% Open-Source C RAM-Run Flash Programmer for RDA8955 (MIPS XCPU)
 * Implements FastPF Protocol & Direct SPI NOR Flash Hardware Control.
 */

#define REG_SPI_BASE         0xA7FFF000
#define SPI_CMD_ADDR         (*(volatile unsigned int *)(REG_SPI_BASE + 0x00))
#define SPI_BLOCK_SIZE       (*(volatile unsigned int *)(REG_SPI_BASE + 0x04))
#define SPI_DATA_FIFO_WO     (*(volatile unsigned int *)(REG_SPI_BASE + 0x08))
#define SPI_DATA_FIFO_RO     (*(volatile unsigned int *)(REG_SPI_BASE + 0x0C))
#define SPI_READ_BACK        (*(volatile unsigned int *)(REG_SPI_BASE + 0x10))
#define SPI_CONFIG           (*(volatile unsigned int *)(REG_SPI_BASE + 0x14))
#define SPI_FIFO_CTRL        (*(volatile unsigned int *)(REG_SPI_BASE + 0x18))
#define SPI_AHB_CTRL         (*(volatile unsigned int *)(REG_SPI_BASE + 0x1C))
#define SPI_FLASH_24         (*(volatile unsigned int *)(REG_SPI_BASE + 0x24))

#define DEBUG_HOST_EVENT     (*(volatile unsigned int *)0xA1A3F008)

typedef struct {
    volatile unsigned int cmd;
    volatile unsigned int flashAddr;
    volatile unsigned int ramAddr;
    volatile unsigned int size;
    volatile unsigned int fcs;
} FpcCommand;

typedef struct {
    volatile unsigned int version;
    FpcCommand slot0;
    FpcCommand slot1;
    volatile unsigned int buf_a;
    volatile unsigned int buf_b;
    volatile unsigned int buf_c;
    volatile unsigned int fpc_size;
} FastPfMap;

#define FASTPF_MAP (*(volatile FastPfMap *)0xA1C05F30)

#define FPC_NONE        0
#define FPC_PROGRAM     1
#define FPC_END         4
#define FPC_CHECK_FCS   5
#define FPC_RESTART     7
#define FPC_ERASE       11

#define EVENT_FLASH_PROG_READY 0xF0

/*
 * SPI flash controller access, following the vendor hal_SpiFlash* driver:
 * - commands: address/data bytes into DATA_FIFO, then CMD_ADDR = cmd << 8
 * - reads: CMD_ADDR = cmd << 8 | 0x10000, rx length in BLOCK_SIZE[16:8] and
 *   CONFIG[18:17] = len - 1; the result is MSB-aligned in READ_BACK
 * BLOCK_SIZE/CONFIG are only ever read-modify-written.
 */
#define SPI_BLOCK_RX_MASK  0xFFFE00FFu
#define SPI_CONFIG_RX_MASK 0xFFF9FFFFu

static inline void send_event(unsigned int evt);

static inline void spi_wait_idle(void) {
    while (SPI_DATA_FIFO_RO & 0x01);
    while (SPI_DATA_FIFO_RO & 0x01);
}

static void spi_cmd(unsigned int cmd, const unsigned char *tx, unsigned int len) {
    spi_wait_idle();
    SPI_FIFO_CTRL = 2; /* clear tx fifo */
    spi_wait_idle();
    for (unsigned int i = 0; i < len; i++)
        SPI_DATA_FIFO_WO = tx[i];
    SPI_CMD_ADDR = cmd << 8;
    spi_wait_idle();
}

static unsigned int spi_read(unsigned int cmd, unsigned int rxlen) {
    spi_wait_idle();
    SPI_FIFO_CTRL = 3; /* clear tx & rx fifos */
    spi_wait_idle();
    SPI_BLOCK_SIZE = (SPI_BLOCK_SIZE & SPI_BLOCK_RX_MASK) | (rxlen << 8);
    SPI_CONFIG = (SPI_CONFIG & SPI_CONFIG_RX_MASK) | ((rxlen - 1) << 17);
    spi_wait_idle();
    SPI_CMD_ADDR = (cmd << 8) | 0x10000;
    spi_wait_idle();
    unsigned int v = SPI_READ_BACK;
    SPI_CONFIG &= SPI_CONFIG_RX_MASK;
    SPI_BLOCK_SIZE = (SPI_BLOCK_SIZE & SPI_BLOCK_RX_MASK) | (1 << 8);
    return v >> ((4 - rxlen) * 8);
}

static unsigned int spi_read_status(void) {
    return spi_read(0x05, 1);
}

/* SR2 (0x35) << 8 | SR1 (0x05), the same pair the vendor hal reads. */
static unsigned int spi_read_status16(void) {
    return (spi_read(0x35, 1) << 8) | spi_read(0x05, 1);
}

static unsigned int spi_read_id(void) {
    return spi_read(0x9F, 3);
}

/* Busy-wait until WIP clears; the vendor polls SR1 bit 0 after ~20 us. */
static void spi_wait_wip_done(void) {
    for (volatile int d = 0; d < 100; d++);
    while (spi_read_status() & 0x01);
}

/* Returns non-zero once the write-enable latch is confirmed set. */
static int spi_write_enable(void) {
    for (int i = 0; i < 1000; i++) {
        spi_cmd(0x06, 0, 0);
        if (spi_read_status() & 0x02)
            return 1;
    }
    return 0;
}

/* Clear all block-protect bits (SR1 BP/SRP and SR2 CMP) with a 2-byte WRSR. */
static void spi_unlock_flash(void) {
    unsigned char zero[2] = {0, 0};
    spi_wait_wip_done();
    spi_write_enable();
    spi_cmd(0x01, zero, 2);
    spi_wait_wip_done();
}

static inline void spi_manual_mode(int on) {
    if (on)
        SPI_AHB_CTRL |= 0x10;
    else
        SPI_AHB_CTRL &= ~0x11u;
}

static void flash_erase(unsigned int addr, unsigned int size) {
    spi_manual_mode(1);
    unsigned int end = addr + size;
    for (unsigned int a = addr; a < end; a += 4096) {
        unsigned char tx[3] = {(a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF};
        spi_wait_wip_done();
        if (!spi_write_enable())
            send_event(0xEE000000 | (a >> 12)); /* WEL never set */
        spi_cmd(0x20, tx, 3);
        spi_wait_wip_done();
    }
    spi_manual_mode(0);
}

static void flash_program(unsigned int flash_addr, const unsigned char *src, unsigned int size) {
    spi_manual_mode(1);
    unsigned int offset = 0;
    while (offset < size) {
        /* One page program per 256-byte page, as the vendor hal_SpiFlashWrite does:
         * data bytes go into the FIFO, the address rides in CMD_ADDR[31:8]. */
        unsigned int a = flash_addr + offset;
        unsigned int chunk = 256 - (a & 0xFF);
        if (chunk > size - offset)
            chunk = size - offset;

        spi_wait_wip_done();
        if (!spi_write_enable())
            send_event(0xEF000000 | (a & 0xFFFFFF)); /* WEL never set */
        spi_wait_idle();
        SPI_FIFO_CTRL = 2; /* clear tx fifo */
        spi_wait_idle();
        for (unsigned int i = 0; i < chunk; i++)
            SPI_DATA_FIFO_WO = src[offset + i];
        SPI_CMD_ADDR = (a << 8) | 0x02;
        spi_wait_idle();
        spi_wait_wip_done();

        offset += chunk;
    }
    spi_manual_mode(0);
}

static inline void send_event(unsigned int evt) {
    while ((DEBUG_HOST_EVENT & 0x01) == 0);
    DEBUG_HOST_EVENT = evt;
}

void ramrun_main(void) {
    /* Flush/Invalidate I-cache */
    void (*inv_icache)(void) = (void (*)(void))0x81e00378;
    inv_icache();

    /* Report status registers before and after the unlock (0xE1/0xE2 events) */
    send_event(0xE1000000 | spi_read_status16());
    spi_unlock_flash();
    send_event(0xE2000000 | spi_read_status16());

    /* Read flash ID */
    unsigned int flash_id = spi_read_id();
    send_event(flash_id);

    /* Initialize FastPF map structure at 0xA1C05F30 */
    FASTPF_MAP.slot0.cmd = FPC_NONE;
    FASTPF_MAP.slot0.flashAddr = 0;
    FASTPF_MAP.slot0.ramAddr = 0;
    FASTPF_MAP.slot0.size = 0;
    FASTPF_MAP.slot0.fcs = 0;

    FASTPF_MAP.slot1.cmd = FPC_NONE;
    FASTPF_MAP.slot1.flashAddr = 0;
    FASTPF_MAP.slot1.ramAddr = 0;
    FASTPF_MAP.slot1.size = 0;
    FASTPF_MAP.slot1.fcs = 0;

    FASTPF_MAP.buf_a = 0xA1C06000;
    FASTPF_MAP.buf_b = 0xA1C07000;
    FASTPF_MAP.buf_c = 0xA1C08000;
    FASTPF_MAP.fpc_size = 4096;

    /* Signal FastPF protocol version ready */
    FASTPF_MAP.version = 0xFB05FA01;

    /* Command loop */
    while (1) {
        if (FASTPF_MAP.slot0.cmd != FPC_NONE) {
            unsigned int cmd = FASTPF_MAP.slot0.cmd;
            if (cmd == FPC_ERASE) {
                flash_erase(FASTPF_MAP.slot0.flashAddr, FASTPF_MAP.slot0.size);
            } else if (cmd == FPC_PROGRAM) {
                flash_program(FASTPF_MAP.slot0.flashAddr, (const unsigned char *)FASTPF_MAP.slot0.ramAddr, FASTPF_MAP.slot0.size);
            } else if (cmd == FPC_RESTART) {
                FASTPF_MAP.slot0.cmd = FPC_NONE;
                send_event(EVENT_FLASH_PROG_READY + 0);
                for (volatile int d = 0; d < 100000; d++);
                /* Clear boot mode in Reset_Cause so future boots boot from flash */
                *(volatile unsigned int *)0xA1A000A0 = 0;
                /* Restore SPI Flash controller to AHB read mode via ROM boot_IspiOpen */
                void (*ispi_open)(int) = (void (*)(int))0x81e01791;
                ispi_open(0);
                /* Invalidate I-cache */
                void (*inv_icache)(void) = (void (*)(void))0x81e00378;
                inv_icache();
                /* Jump directly to flash entry point (0x88000010) */
                typedef void (*entry_func_t)(void);
                entry_func_t entry = (entry_func_t)0x88000010;
                entry();
                while (1);
            }
            FASTPF_MAP.slot0.cmd = FPC_NONE;
            send_event(EVENT_FLASH_PROG_READY + 0);
        }

        if (FASTPF_MAP.slot1.cmd != FPC_NONE) {
            unsigned int cmd = FASTPF_MAP.slot1.cmd;
            if (cmd == FPC_ERASE) {
                flash_erase(FASTPF_MAP.slot1.flashAddr, FASTPF_MAP.slot1.size);
            } else if (cmd == FPC_PROGRAM) {
                flash_program(FASTPF_MAP.slot1.flashAddr, (const unsigned char *)FASTPF_MAP.slot1.ramAddr, FASTPF_MAP.slot1.size);
            } else if (cmd == FPC_RESTART) {
                FASTPF_MAP.slot1.cmd = FPC_NONE;
                send_event(EVENT_FLASH_PROG_READY + 1);
                for (volatile int d = 0; d < 100000; d++);
                /* Clear boot mode in Reset_Cause so future boots boot from flash */
                *(volatile unsigned int *)0xA1A000A0 = 0;
                /* Restore SPI Flash controller to AHB read mode via ROM boot_IspiOpen */
                void (*ispi_open)(int) = (void (*)(int))0x81e01791;
                ispi_open(0);
                /* Invalidate I-cache */
                void (*inv_icache)(void) = (void (*)(void))0x81e00378;
                inv_icache();
                /* Jump directly to flash entry point (0x88000010) */
                typedef void (*entry_func_t)(void);
                entry_func_t entry = (entry_func_t)0x88000010;
                entry();
                while (1);
            }
            FASTPF_MAP.slot1.cmd = FPC_NONE;
            send_event(EVENT_FLASH_PROG_READY + 1);
        }
    }
}
