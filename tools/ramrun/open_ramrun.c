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

static inline void spi_wait_idle(void) {
    while (SPI_DATA_FIFO_RO & 0x01);
    while (SPI_DATA_FIFO_RO & 0x01);
}

static unsigned int spi_read_status(void) {
    spi_wait_idle();
    SPI_FIFO_CTRL = 3; /* clear tx & rx */
    SPI_BLOCK_SIZE = (1 << 8);
    SPI_CMD_ADDR = (0x05 << 8) | 0x100;
    spi_wait_idle();
    return SPI_READ_BACK & 0xFF;
}

static void spi_wait_wip_done(void) {
    for (volatile int d = 0; d < 2000; d++);
    while (spi_read_status() & 0x01);
}

static void spi_write_enable(void) {
    spi_wait_idle();
    SPI_BLOCK_SIZE = 0;
    SPI_CMD_ADDR = (0x06 << 8);
    spi_wait_idle();
    volatile int timeout = 10000;
    while (((spi_read_status() & 0x02) == 0) && timeout--);
}

static unsigned int spi_read_id(void) {
    spi_wait_idle();
    SPI_FLASH_24 = 3;
    SPI_BLOCK_SIZE = (3 << 8); /* 3 bytes */
    spi_wait_idle();
    SPI_CMD_ADDR = (0x9F << 8) | 0x100;
    spi_wait_idle();
    return SPI_READ_BACK;
}

static void spi_unlock_flash(void) {
    spi_write_enable();
    spi_wait_idle();
    SPI_FIFO_CTRL = 2; /* clear tx fifo */
    SPI_BLOCK_SIZE = 0;
    SPI_DATA_FIFO_WO = 0;
    SPI_DATA_FIFO_WO = 0;
    SPI_CMD_ADDR = 0x0100; /* WRSR 0x01 */
    spi_wait_wip_done();
}

static void flash_erase(unsigned int addr, unsigned int size) {
    SPI_AHB_CTRL |= 0x10;
    unsigned int end = addr + size;
    for (unsigned int a = addr; a < end; a += 4096) {
        spi_write_enable();
        spi_wait_idle();
        SPI_FIFO_CTRL = 2; /* clear tx fifo */
        SPI_BLOCK_SIZE = 0;
        SPI_DATA_FIFO_WO = (a >> 16) & 0xFF;
        SPI_DATA_FIFO_WO = (a >> 8) & 0xFF;
        SPI_DATA_FIFO_WO = a & 0xFF;
        SPI_CMD_ADDR = (0x20 << 8);
        spi_wait_wip_done();
    }
    SPI_AHB_CTRL &= ~0x10;
}

static void flash_program(unsigned int flash_addr, const unsigned char *src, unsigned int size) {
    SPI_AHB_CTRL |= 0x10;
    unsigned int offset = 0;
    while (offset < size) {
        unsigned int page_rem = 256 - ((flash_addr + offset) & 0xFF);
        unsigned int chunk = size - offset;
        if (chunk > page_rem)
            chunk = page_rem;
        if (chunk > 16)
            chunk = 16;

        unsigned int a = flash_addr + offset;
        spi_write_enable();
        spi_wait_idle();
        SPI_FIFO_CTRL = 2; /* clear tx fifo */
        SPI_BLOCK_SIZE = 0;
        SPI_DATA_FIFO_WO = (a >> 16) & 0xFF;
        SPI_DATA_FIFO_WO = (a >> 8) & 0xFF;
        SPI_DATA_FIFO_WO = a & 0xFF;
        for (unsigned int i = 0; i < chunk; i++) {
            SPI_DATA_FIFO_WO = src[offset + i];
        }
        SPI_CMD_ADDR = (0x02 << 8);
        spi_wait_wip_done();

        offset += chunk;
    }
    SPI_AHB_CTRL &= ~0x10;
}

static inline void send_event(unsigned int evt) {
    while ((DEBUG_HOST_EVENT & 0x01) == 0);
    DEBUG_HOST_EVENT = evt;
}

void ramrun_main(void) {
    /* Flush/Invalidate I-cache */
    void (*inv_icache)(void) = (void (*)(void))0x81e00378;
    inv_icache();

    /* Unlock status register protection bits */
    spi_unlock_flash();

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
                for (volatile int d = 0; d < 200000; d++);
                /* Trigger hardware soft reset into flashed firmware */
                *(volatile unsigned int *)0xA1A00000 = 0x00A50001;
                *(volatile unsigned int *)0xA1A00004 = 0x80000000;
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
                for (volatile int d = 0; d < 200000; d++);
                /* Trigger hardware soft reset into flashed firmware */
                *(volatile unsigned int *)0xA1A00000 = 0x00A50001;
                *(volatile unsigned int *)0xA1A00004 = 0x80000000;
                while (1);
            }
            FASTPF_MAP.slot1.cmd = FPC_NONE;
            send_event(EVENT_FLASH_PROG_READY + 1);
        }
    }
}
