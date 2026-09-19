#include <bnux/types.h>

extern void bnuxfs_add_file(const char *name, const uint8_t *data, uint64_t size);

static const uint8_t readme_txt[] =
    "BNUX OS BY LEVERSOFT.\n"
    "THIS IS A DEMO FILE FROM BNUXFS RAMDISK.\n"
    "TYPE HELP FOR COMMAND LIST.\n";

static const uint8_t about_txt[] =
    "BNUXDOS V0.1 - KERNEL BUILT FROM SCRATCH.\n"
    "MONOLITHIC KERNEL, X86_64, LIMINE BOOT.\n";

void initrd_install(void) {
    bnuxfs_add_file("README.TXT", readme_txt, sizeof(readme_txt) - 1);
    bnuxfs_add_file("ABOUT.TXT", about_txt, sizeof(about_txt) - 1);
}
