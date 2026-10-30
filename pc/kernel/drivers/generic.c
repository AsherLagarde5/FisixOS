#include "generic.h"
static void outb(uint16_t p, uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static uint8_t inb(uint16_t p){ uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static void delay_loop(uint32_t n){ volatile uint32_t i; for(i=0;i<n;i++){ __asm__ volatile("pause"); } }

void generic_uart_init(void){
    outb(0x3F8+1,0x00); outb(0x3F8+3,0x80);
    outb(0x3F8+0,0x03); outb(0x3F8+1,0x00);
    outb(0x3F8+3,0x03); outb(0x3F8+2,0xC7); outb(0x3F8+4,0x0B);
}
void generic_uart_putc(char c){
    uint32_t to=100000; while(to-- && ((inb(0x3F8+5)&0x20)==0)){}
    outb(0x3F8,(uint8_t)c);
}
void generic_uart_puts(const char *s){ while(s&&*s) generic_uart_putc(*s++); }

static uint8_t cmos_read(uint8_t reg){
    uint8_t v; /* wait for update-in-progress to clear, bounded */
    uint32_t to=100000;
    outb(0x70,0x0A); while(to-- && (inb(0x71)&0x80)){}
    outb(0x70,reg); v=inb(0x71); return v;
}
static int bcd_to_bin(uint8_t v){ return ((v>>4)*10)+(v&0x0F); }
int generic_rtc_read(rtc_time_t *out){
    uint8_t status_b, sec, min, hr, day, mon, yr;
    if(!out) return 0;
    status_b=cmos_read(0x0B);
    sec=cmos_read(0x00); min=cmos_read(0x02); hr=cmos_read(0x04);
    day=cmos_read(0x07); mon=cmos_read(0x08); yr=cmos_read(0x09);
    if(!(status_b&0x04)){ sec=bcd_to_bin(sec); min=bcd_to_bin(min); hr=bcd_to_bin(hr); day=bcd_to_bin(day); mon=bcd_to_bin(mon); yr=bcd_to_bin(yr); }
    if(!(status_b&0x02)){ if(hr&0x80) hr=((hr&0x7F)+12)%24; }
    out->second=sec; out->minute=min; out->hour=hr; out->day=day; out->month=mon; out->year=2000+yr;
    return (mon>=1 && mon<=12 && day>=1 && day<=31) ? 1 : 0;
}

void generic_speaker_beep(uint32_t freq_hz, uint32_t approx_ms){
    uint32_t div; uint8_t tmp;
    if(freq_hz==0) return;
    div=1193182u/freq_hz; if(div==0) div=1;
    outb(0x43,0xB6); outb(0x42,(uint8_t)div); outb(0x42,(uint8_t)(div>>8));
    tmp=inb(0x61); outb(0x61,tmp|0x03);
    delay_loop(approx_ms*2000u);
    tmp=inb(0x61); outb(0x61,tmp&~0x03);
}

/* PS/2 aux (mouse) port: enable aux, send reset-defaults + enable,
 * expect ACK 0xFA. Returns 1 on ACK, else 0. Polling, bounded. */
static int ps2_wait_input(void){ uint32_t to=100000; while(to--){ if(inb(0x64)&0x01) return 1; } return 0; }
static int ps2_wait_output(void){ uint32_t to=100000; while(to--){ if(!(inb(0x64)&0x02)) return 1; } return 0; }
int generic_ps2_mouse_init(void){
    uint8_t ack;
    if(!ps2_wait_output()) return 0; outb(0x64,0xA8); /* enable aux */
    if(!ps2_wait_output()) return 0; outb(0x64,0xD4);
    if(!ps2_wait_output()) return 0; outb(0x60,0xF6); /* set defaults */
    if(!ps2_wait_input()) return 0; ack=inb(0x60); if(ack!=0xFA) return 0;
    if(!ps2_wait_output()) return 0; outb(0x64,0xD4);
    if(!ps2_wait_output()) return 0; outb(0x60,0xF4); /* enable data */
    if(!ps2_wait_input()) return 0; ack=inb(0x60); return ack==0xFA;
}

void generic_init_all(void){
    rtc_time_t t;
    generic_uart_init();
    generic_uart_puts("[FisixOS] generic: UART OK\n");
    if(generic_rtc_read(&t)){
        generic_uart_puts("[FisixOS] generic: RTC OK\n");
    } else {
        generic_uart_puts("[FisixOS] generic: RTC not readable\n");
    }
    generic_uart_puts(generic_ps2_mouse_init() ? "[FisixOS] generic: PS/2 mouse ACK OK\n" : "[FisixOS] generic: PS/2 mouse no ACK\n");
}
