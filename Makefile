CC := gcc
LD := ld
AS := gcc

CFLAGS := -std=gnu11 -ffreestanding -fno-stack-protector -fno-stack-check \
          -fno-pic -fno-pie -m64 -march=x86-64 -mno-red-zone -mno-mmx \
          -mno-sse -mno-sse2 -mcmodel=kernel -Wall -Wextra \
          -Ikernel/include -O2

ASFLAGS := -m64

# Fedora называет утилиту grub2-mkrescue (не grub-mkrescue как в Debian/Ubuntu) —
# определяем автоматически, что реально есть в системе.
GRUB_MKRESCUE := $(shell command -v grub2-mkrescue 2>/dev/null || command -v grub-mkrescue 2>/dev/null)

# --- Общие исходники для ОБОИХ путей загрузки ---
COMMON_C_SRCS := $(shell find kernel -name '*.c' \
                    ! -name 'kernel_limine.c' ! -name 'kernel_mb2.c' ! -path 'kernel/boot/*')
COMMON_S_SRCS := $(shell find kernel -name '*.S' ! -path 'kernel/boot/*')
COMMON_OBJS   := $(COMMON_C_SRCS:.c=.o) $(COMMON_S_SRCS:.S=.o)

# --- Limine-путь ---
LIMINE_KERNEL := build/bnux.elf
LIMINE_OBJS   := $(COMMON_OBJS) kernel/kernel_limine.o
LIMINE_LDFLAGS := -nostdlib -static -no-pie -z max-page-size=0x1000 -T kernel/linker.ld

# --- GRUB-путь: ELF называется так же, bnux.elf, но лежит в build/grub/,
# чтобы не сталкиваться с Limine-версией при сборке обоих путей подряд ---
GRUB_KERNEL := build/grub/bnux.elf
GRUB_OBJS   := $(COMMON_OBJS) kernel/kernel_mb2.o kernel/boot/multiboot2.o kernel/boot/mb2_parse.o
GRUB_LDFLAGS := -nostdlib -static -no-pie -z max-page-size=0x1000 -T kernel/linker_grub.ld

.PHONY: all clean iso run iso-grub run-grub

all: $(LIMINE_KERNEL)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) -c $< -o $@

$(LIMINE_KERNEL): $(LIMINE_OBJS)
	mkdir -p build
	$(LD) $(LIMINE_LDFLAGS) -o $(LIMINE_KERNEL) $(LIMINE_OBJS)

$(GRUB_KERNEL): $(GRUB_OBJS)
	mkdir -p build/grub
	$(LD) $(GRUB_LDFLAGS) -o $(GRUB_KERNEL) $(GRUB_OBJS)

clean:
	rm -f $(COMMON_OBJS) kernel/kernel_limine.o kernel/kernel_mb2.o kernel/boot/multiboot2.o kernel/boot/mb2_parse.o
	rm -rf build

# --- ISO через Limine (нужен limine-bootloader склонированный рядом) ---
# git clone https://github.com/limine-bootloader/limine.git --branch=v8.x-binary --depth=1
iso: $(LIMINE_KERNEL)
	mkdir -p build/iso_root/boot/limine
	cp $(LIMINE_KERNEL) build/iso_root/boot/bnux.elf
	cp limine.conf build/iso_root/boot/limine/
	cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin build/iso_root/boot/limine/
	mkdir -p build/iso_root/EFI/BOOT
	cp limine/BOOTX64.EFI build/iso_root/EFI/BOOT/
	xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		build/iso_root -o build/bnux.iso
	./limine/limine bios-install build/bnux.iso

run: iso
	@if [ ! -f build/disk.img ]; then \
		dd if=/dev/zero of=build/disk.img bs=1M count=64; \
	fi
	qemu-system-x86_64 -machine pc -cdrom build/bnux.iso -drive file=build/disk.img,format=raw,if=ide \
		-device piix3-usb-uhci -device usb-mouse \
		-serial stdio -m 256M

# --- ISO через GRUB ---
# Fedora:         sudo dnf install grub2-tools grub2-tools-extra grub2-pc grub2-efi-x64 xorriso mtools
# Debian/Ubuntu:  sudo apt install grub-pc-bin grub-efi-amd64-bin xorriso mtools
iso-grub: $(GRUB_KERNEL)
	@if [ -z "$(GRUB_MKRESCUE)" ]; then \
		echo "ОШИБКА: не найден ни grub2-mkrescue, ни grub-mkrescue."; \
		echo "Fedora:  sudo dnf install grub2-tools grub2-tools-extra xorriso mtools"; \
		echo "Debian:  sudo apt install grub-pc-bin grub-efi-amd64-bin xorriso mtools"; \
		exit 1; \
	fi
	mkdir -p build/iso_grub_root/boot/grub
	cp $(GRUB_KERNEL) build/iso_grub_root/boot/bnux.elf
	cp grub.cfg build/iso_grub_root/boot/grub/
	$(GRUB_MKRESCUE) -o build/bnux-grub.iso build/iso_grub_root

run-grub: iso-grub
	@if [ ! -f build/disk.img ]; then \
		dd if=/dev/zero of=build/disk.img bs=1M count=64; \
	fi
	qemu-system-x86_64 -machine pc -cdrom build/bnux-grub.iso -drive file=build/disk.img,format=raw,if=ide \
		-device piix3-usb-uhci -device usb-mouse \
		-serial stdio -m 256M
