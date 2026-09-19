#include <bnux/types.h>

extern void *pmm_alloc_page(void);
extern void  kprintf(const char *fmt, ...);
extern void  task_switch(uint64_t *old_rsp, uint64_t new_rsp);

#define MAX_TASKS   8
#define STACK_PAGES 1   // 4KB на стек задачи — достаточно для простых демо-задач

typedef void (*task_entry_t)(void);

enum task_state { TASK_UNUSED, TASK_READY, TASK_RUNNING };

struct task {
    uint64_t rsp;
    enum task_state state;
    char name[16];
};

static struct task tasks[MAX_TASKS];
static int current_task = -1;
static int task_count = 0;
static uint64_t hhdm_offset = 0;

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void *phys_to_virt(void *phys) {
    return (void*)((uint64_t)phys + hhdm_offset);
}

void task_init(uint64_t hhdm) {
    hhdm_offset = hhdm;
    for (int i = 0; i < MAX_TASKS; i++) tasks[i].state = TASK_UNUSED;
}

/* trampoline: раскладка initial-стека кладёт сюда адрес возврата.
 * Он вызывает реальную функцию задачи, а если та вернётся —
 * задача просто крутится в hlt (у нас нет task_exit/уничтожения). */
static task_entry_t pending_entry[MAX_TASKS];
static int pending_slot;

void task_trampoline(void) {
    task_entry_t entry = pending_entry[pending_slot];
    entry();
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

int task_create(const char *name, task_entry_t entry) {
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) { slot = i; break; }
    }
    if (slot < 0) return -1;

    void *phys = pmm_alloc_page();
    if (!phys) return -1;
    uint8_t *stack = (uint8_t*)phys_to_virt(phys);
    uint64_t stack_top = (uint64_t)(stack + 4096 * STACK_PAGES);

    pending_entry[slot] = entry;
    pending_slot = slot; // упрощение: первый switch на новую задачу должен случиться
                          // прежде чем создастся следующая (нормально для наших демо)

    /* Раскладываем начальный стек так, как его ожидает task_switch:
     * сначала "адрес возврата" (куда попадём через ret) — trampoline,
     * затем 5 нулей под callee-saved регистры (rbx,r12,r13,r14,r15,rbp),
     * которые task_switch восстановит первым делом. */
    extern void task_trampoline(void);
    uint64_t *sp = (uint64_t*)stack_top;
    *(--sp) = (uint64_t)task_trampoline; // return address
    *(--sp) = 0; // rbp
    *(--sp) = 0; // rbx
    *(--sp) = 0; // r12
    *(--sp) = 0; // r13
    *(--sp) = 0; // r14
    *(--sp) = 0; // r15

    tasks[slot].rsp = (uint64_t)sp;
    tasks[slot].state = TASK_READY;
    str_copy(tasks[slot].name, name, sizeof(tasks[slot].name));
    task_count++;
    return slot;
}

void task_yield(void) {
    if (task_count == 0) return;

    int prev = current_task;
    int next = current_task;
    for (int i = 0; i < MAX_TASKS; i++) {
        next = (next + 1) % MAX_TASKS;
        if (tasks[next].state == TASK_READY || tasks[next].state == TASK_RUNNING) break;
    }
    if (next == prev) return; // больше некому уступать

    if (prev >= 0) tasks[prev].state = TASK_READY;
    tasks[next].state = TASK_RUNNING;
    current_task = next;

    if (prev < 0) {
        /* самый первый переход — "старую" задачу некуда сохранять,
         * используем фиктивный указатель на стеке */
        uint64_t dummy;
        task_switch(&dummy, tasks[next].rsp);
    } else {
        task_switch(&tasks[prev].rsp, tasks[next].rsp);
    }
}

void task_list(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) continue;
        kprintf("  [%d] %s %s\n", i, tasks[i].name,
                tasks[i].state == TASK_RUNNING ? "(RUNNING)" : "(READY)");
    }
}
