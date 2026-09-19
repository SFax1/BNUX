#include <bnux/types.h>
#include <bnux/io.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static uint8_t bcd_to_bin(uint8_t v) {
    return (v & 0x0F) + ((v >> 4) * 10);
}

struct rtc_time { uint8_t sec, min, hour, day, month; uint16_t year; };

/* Простое чтение без ожидания "update in progress" — если попадём точно
 * в момент обновления регистров RTC, показания могут дребезжать на
 * секунду-две. Для команды DATE в шелле это несущественно. */
void rtc_read(struct rtc_time *t) {
    t->sec   = bcd_to_bin(cmos_read(0x00));
    t->min   = bcd_to_bin(cmos_read(0x02));
    t->hour  = bcd_to_bin(cmos_read(0x04));
    t->day   = bcd_to_bin(cmos_read(0x07));
    t->month = bcd_to_bin(cmos_read(0x08));
    t->year  = 2000 + bcd_to_bin(cmos_read(0x09));
}
