#include <bnux/types.h>
#include <bnux/io.h>

#define ATA_PRIMARY_IO   0x1F0
#define ATA_PRIMARY_CTRL 0x3F6

#define ATA_REG_DATA    0
#define ATA_REG_ERROR   1
#define ATA_REG_SECCNT  2
#define ATA_REG_LBA0    3
#define ATA_REG_LBA1    4
#define ATA_REG_LBA2    5
#define ATA_REG_DRIVE   6
#define ATA_REG_STATUS  7
#define ATA_REG_COMMAND 7

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH   0xE7

static int ata_wait_bsy(void) {
    for (int i = 0; i < 2000000; i++) {
        if (!(inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_STATUS_BSY)) return 0;
    }
    return -1; // таймаут — скорее всего на этом канале физически нет ATA-диска
}

static int ata_wait_drq(void) {
    uint8_t status;
    for (;;) {
        status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status & ATA_STATUS_ERR) return -1;
        if (status & ATA_STATUS_DRQ) return 0;
    }
}

/* Читает `count` секторов (по 512 байт) начиная с LBA в буфер `buf`.
 * Только master-диск на первичном канале — этого хватит для QEMU
 * с -drive if=ide (обычно именно так и подключается). */
int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if (ata_wait_bsy() != 0) return -1;

    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + ATA_REG_SECCNT, count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (uint8_t)(lba));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

    uint16_t *ptr = (uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_bsy() != 0) return -1;
        if (ata_wait_drq() != 0) return -1;
        for (int i = 0; i < 256; i++) {
            ptr[s * 256 + i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
        }
    }
    return 0;
}

int ata_probe(void) {
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xE0);
    io_wait();
    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    return status != 0xFF && status != 0x00;
}
