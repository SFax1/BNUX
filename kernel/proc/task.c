#include <bnux/types.h>
#include <bnux/syscall.h>

extern void *pmm_alloc_page(void);
extern void  kprintf(const char *fmt, ...);
extern void  task_switch(uint64_t *old_rsp, uint64_t new_rsp);
extern int   vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);

#define MAX_TASKS   8
#define STACK_PAGES 2   // 8KB на стек задачи — достаточно для простых демо-задач
#define CODE_PAGES  1   // 4KB на код user-программы

typedef void (*task_entry_t)(void);

enum task_state { TASK_UNUSED, TASK_READY, TASK_RUNNING };

struct task {
    uint64_t rsp;
    enum task_state state;
    char name[16];
    bool is_user;               /* true если задача ring3 */
    uint64_t user_entry;        /* точка входа для user-программы */
    uint64_t user_stack_virt;   /* виртуальный адрес стека user */
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
    tasks[slot].is_user = false;
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
        kprintf("  [%d] %s %s%s\n", i, tasks[i].name,
                tasks[i].state == TASK_RUNNING ? "(RUNNING)" : "(READY)",
                tasks[i].is_user ? " [user]" : "");
    }
}

/* 
 * task_create_user — создание user-задачи (ring3)
 * code: указатель на буфер с кодом программы
 * code_size: размер кода в байтах (максимум 4KB)
 * 
 * Выделяет:
 * - 1 страницу для кода (с PAGE_USER)
 * - STACK_PAGES страниц для стека (с PAGE_USER)
 * 
 * После создания задача готова к запуску через enter_user_mode
 */
int task_create_user(const char *name, const uint8_t *code, size_t code_size) {
    if (code_size > 4096) {
        kprintf("[task] ошибка: код слишком большой (%d байт)\n", (int)code_size);
        return -1;
    }
    
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) { slot = i; break; }
    }
    if (slot < 0) {
        kprintf("[task] ошибка: нет свободных слотов задач\n");
        return -1;
    }
    
    /* Выделяем физические страницы для кода и стека */
    void *code_phys = pmm_alloc_page();
    void *stack_phys = pmm_alloc_page();
    if (!code_phys || !stack_phys) {
        kprintf("[task] ошибка: не удалось выделить память\n");
        return -1;
    }
    
    /* Копируем код в выделенную страницу */
    uint8_t *code_virt = (uint8_t*)phys_to_virt(code_phys);
    for (size_t i = 0; i < 4096; i++) {
        code_virt[i] = (i < code_size) ? code[i] : 0;
    }
    
    /* Очищаем стек */
    uint8_t *stack_virt = (uint8_t*)phys_to_virt(stack_phys);
    for (size_t i = 0; i < 4096 * STACK_PAGES; i++) {
        stack_virt[i] = 0;
    }
    
    /* Выбираем виртуальные адреса для user-пространства */
    /* Код: 0x100000 (1MB), стек: 0x200000 (2MB) — простые фиксированные адреса */
    uint64_t code_virt_addr = 0x100000;
    uint64_t stack_virt_addr = 0x200000 + (STACK_PAGES * 4096);
    
    /* Отображаем страницы с флагом PAGE_USER */
    #define PAGE_PRESENT  (1ULL << 0)
    #define PAGE_WRITABLE (1ULL << 1)
    #define PAGE_USER     (1ULL << 2)
    
    /* Мапим код: read+execute для user */
    if (vmm_map_page(code_virt_addr, (uint64_t)code_phys, PAGE_PRESENT | PAGE_USER) != 0) {
        kprintf("[task] ошибка: не удалось отобразить код\n");
        return -1;
    }
    
    /* Мапим стек: read+write для user */
    for (int i = 0; i < STACK_PAGES; i++) {
        uint64_t stack_page_phys = (uint64_t)stack_phys + (i * 4096);
        uint64_t stack_page_virt = 0x200000 + (i * 4096);
        if (vmm_map_page(stack_page_virt, stack_page_phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            kprintf("[task] ошибка: не удалось отобразить стек\n");
            return -1;
        }
    }
    
    /* Инициализируем задачу */
    tasks[slot].rsp = 0;  /* не используется для user-задач */
    tasks[slot].state = TASK_READY;
    tasks[slot].is_user = true;
    tasks[slot].user_entry = code_virt_addr;  /* точка входа */
    tasks[slot].user_stack_virt = stack_virt_addr;
    str_copy(tasks[slot].name, name, sizeof(tasks[slot].name));
    task_count++;
    
    kprintf("[task] создана user-задача '%s': entry=0x%x stack=0x%x\n", 
            name, code_virt_addr, stack_virt_addr);
    
    return slot;
}

/* 
 * task_run_user — запуск user-задачи по индексу
 * Переключается в ring3 через enter_user_mode
 */
void task_run_user(int slot) {
    if (slot < 0 || slot >= MAX_TASKS) return;
    if (!tasks[slot].is_user) return;
    if (tasks[slot].state != TASK_READY) return;
    
    tasks[slot].state = TASK_RUNNING;
    current_task = slot;
    
    /* Переключаемся в ring3 */
    enter_user_mode(tasks[slot].user_entry, tasks[slot].user_stack_virt);
}
