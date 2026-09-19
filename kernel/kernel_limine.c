#include <bnux/types.h>
#include <bnux/limine.h>

extern void serial_init(void);
extern void print_lix_logo(void);
struct mm_region { uint64_t base; uint64_t length; uint64_t type; };
extern void kernel_common_init(void *fb, uint64_t fb_w, uint64_t fb_h, uint64_t fb_pitch,
                                struct mm_region *regions, uint64_t region_count,
                                uint64_t hhdm_offset);

LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_reqs")))
static volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_reqs")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_reqs")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

static struct mm_region region_buf[256];

void kmain(void) {
    serial_init();
    print_lix_logo();

    void *fb = NULL;
    uint64_t fb_w = 0, fb_h = 0, fb_pitch = 0;
    if (fb_request.response && fb_request.response->framebuffer_count > 0) {
        struct limine_framebuffer *f = fb_request.response->framebuffers[0];
        fb = f->address; fb_w = f->width; fb_h = f->height; fb_pitch = f->pitch;
    }

    uint64_t count = 0;
    if (memmap_request.response) {
        count = memmap_request.response->entry_count;
        if (count > 256) count = 256;
        for (uint64_t i = 0; i < count; i++) {
            struct limine_memmap_entry *e = memmap_request.response->entries[i];
            region_buf[i].base = e->base;
            region_buf[i].length = e->length;
            region_buf[i].type = e->type;
        }
    }

    uint64_t hhdm_offset = hhdm_request.response ? hhdm_request.response->offset : 0;

    kernel_common_init(fb, fb_w, fb_h, fb_pitch, region_buf, count, hhdm_offset);
}
