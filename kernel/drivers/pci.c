#include <bnux/types.h>
#include <bnux/io.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t address = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                        ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

extern void kprintf(const char *fmt, ...);

/* Обходит все bus/device/function и печатает класс/подкласс каждого
 * найденного устройства. Класс 0x01 = Mass Storage (0x01=IDE, 0x06=AHCI,
 * 0x08=NVMe). Класс 0x0C/0x03 = USB-контроллер (prog_if: 0x00=UHCI,
 * 0x10=OHCI, 0x20=EHCI, 0x30=xHCI) — для UHCI сразу печатаем I/O base,
 * для остальных MMIO base, эти адреса понадобятся когда дойдём до
 * настоящего драйвера. */
/* Ищет первый UHCI-контроллер (класс 0x0C, подкласс 0x03, prog_if 0x00),
 * возвращает I/O base через out_io_base. 1 = найден, 0 = нет. */
int pci_find_uhci(uint32_t *out_io_base) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint32_t id = pci_config_read32(bus, dev, func, 0x00);
                if ((id & 0xFFFF) == 0xFFFF) continue;

                uint32_t classreg = pci_config_read32(bus, dev, func, 0x08);
                uint8_t class_code = (classreg >> 24) & 0xFF;
                uint8_t subclass   = (classreg >> 16) & 0xFF;
                uint8_t prog_if    = (classreg >> 8) & 0xFF;

                if (class_code == 0x0C && subclass == 0x03 && prog_if == 0x00) {
                    uint32_t bar4 = pci_config_read32(bus, dev, func, 0x20);
                    if (bar4 & 0x01) {
                        *out_io_base = bar4 & ~0x3U;
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

void pci_scan_and_report(void) {
    int found_any = 0;
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint32_t id = pci_config_read32(bus, dev, func, 0x00);
                uint16_t vendor = id & 0xFFFF;
                if (vendor == 0xFFFF) continue; // нет устройства на этой позиции

                uint32_t classreg = pci_config_read32(bus, dev, func, 0x08);
                uint8_t class_code = (classreg >> 24) & 0xFF;
                uint8_t subclass   = (classreg >> 16) & 0xFF;

                if (class_code == 0x01) { // Mass Storage Controller
                    const char *kind = "UNKNOWN";
                    if (subclass == 0x01) kind = "IDE (LEGACY ATA — НАШ ata.c ЕГО УМЕЕТ)";
                    else if (subclass == 0x06) kind = "AHCI/SATA — ДРАЙВЕРА ПОКА НЕТ";
                    else if (subclass == 0x08) kind = "NVME — ДРАЙВЕРА ПОКА НЕТ";
                    kprintf("[pci] %d:%d.%d MASS STORAGE: %s\n", (int)bus, (int)dev, (int)func, kind);
                    found_any = 1;
                }

                if (class_code == 0x0C && subclass == 0x03) { // Serial Bus: USB controller
                    uint32_t progif_reg = pci_config_read32(bus, dev, func, 0x08);
                    uint8_t prog_if = (progif_reg >> 8) & 0xFF;
                    const char *kind = "UNKNOWN";
                    if (prog_if == 0x00) kind = "UHCI (USB 1.1) — цель для первого драйвера";
                    else if (prog_if == 0x10) kind = "OHCI (USB 1.1)";
                    else if (prog_if == 0x20) kind = "EHCI (USB 2.0)";
                    else if (prog_if == 0x30) kind = "XHCI (USB 3.x)";
                    kprintf("[pci] %d:%d.%d USB CONTROLLER: %s\n", (int)bus, (int)dev, (int)func, kind);

                    if (prog_if == 0x00) { // UHCI: I/O-based, BAR4
                        uint32_t bar4 = pci_config_read32(bus, dev, func, 0x20);
                        if (bar4 & 0x01) { // бит0=1 значит I/O space BAR
                            uint32_t io_base = bar4 & ~0x3U;
                            kprintf("[pci]   UHCI IO BASE = 0x%x\n", io_base);
                        }
                    } else { // EHCI/OHCI/XHCI: memory-mapped, BAR0
                        uint32_t bar0 = pci_config_read32(bus, dev, func, 0x10);
                        uint32_t mmio_base = bar0 & ~0xFU;
                        kprintf("[pci]   MMIO BASE = 0x%x\n", mmio_base);
                    }
                    found_any = 1;
                }
            }
        }
    }
    if (!found_any) {
        kprintf("[pci] контроллеров накопителей не найдено вообще\n");
    }
}
