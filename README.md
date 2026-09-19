# Mulix OS

**Developed by ArcXinch**

A modern experimental operating system featuring a unique hybrid GUI inspired by the aesthetics of macOS and Windows 11. Mulix focuses on delivering a sleek, intuitive user experience with a custom-built graphical interface from the ground up.

![License](https://img.shields.io/badge/license-GPL--3.0-blue.svg)
![Status](https://img.shields.io/badge/status-experimental-orange.svg)
![Platform](https://img.shields.io/badge/platform-x86_64-lightgrey.svg)

## ✨ Features

- **Hybrid GUI**: A fresh interface design blending the elegance of macOS with the functionality of Windows 11.
  - Top Menu Bar with system tray.
  - Floating Dock for quick app access.
  - Rounded window corners and modern controls.
- **Custom Kernel**: Lightweight kernel designed for performance and simplicity.
- **Bootloader**: Uses **Limine** for fast and reliable booting.
- **Input Support**: 
  - Keyboard support (USB/PS2).
  - *In-progress*: Native USB Mouse support (HID stack).

## 🖥️ Interface Design

The Mulix desktop environment avoids direct copying but takes the best from both worlds:
- **Clean Aesthetics**: Minimalist icons and smooth animations.
- **Window Management**: Intuitive window controls with distinct color coding.
- **Responsiveness**: Optimized for QEMU emulation and future bare-metal deployment.

## 🚀 Getting Started

### Prerequisites

To build and run Mulix, ensure you have the following installed:
- `gcc` / `g++` (Cross-compiler recommended for x86_64)
- `nasm` or `yasm`
- `make`
- `qemu-system-x86_64`
- `limine` (bootloader)

### Building

Clone the repository and build the project:

```bash
git clone https://github.com/ArcXinch/Mulix.git
cd Mulix
make all
```

This will compile the kernel and generate the bootable image (usually `mulix.iso` or `disk.img`).

### Running with QEMU

Since Mulix uses the **Limine** bootloader, it is typically booted via an ISO or disk image.

**Option 1: Boot from ISO**
```bash
qemu-system-x86_64 -cdrom mulix.iso -boot d -device qemu-xhci -device usb-kbd -device usb-mouse
```

**Option 2: Boot from Disk Image**
```bash
qemu-system-x86_64 -drive format=raw,file=disk.img -device qemu-xhci -device usb-kbd -device usb-mouse
```

> **Note**: The flags `-device qemu-xhci -device usb-kbd -device usb-mouse` enable USB controller emulation and attach a keyboard and mouse, which are required for testing the GUI input features.

## 🛠️ Roadmap

- [x] Custom Hybrid GUI Implementation
- [x] Limine Bootloader Integration
- [ ] Full USB HID Mouse Driver Support
- [ ] Network Stack Implementation
- [ ] File System Drivers (EXT2/FAT32)
- [ ] Multi-tasking Scheduler Improvements

## 🤝 Contributing

Contributions are welcome! Whether it's fixing bugs, improving the GUI, or adding drivers, feel free to open an issue or submit a pull request.

## 📄 License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**. See the [LICENSE](LICENSE) file for details.

---

**Made with ❤️ by ArcXinch**