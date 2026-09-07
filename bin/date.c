#include <stdio.h>
#include <string.h>

static inline unsigned char inb_cmos(unsigned short port) {
    unsigned char res;
    __asm__ volatile("inb %1, %0" : "=a"(res) : "Nd"(port));
    return res;
}

static inline void outb_cmos(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

int main(int argc, char **argv) {
    int utc = 0, rfc = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u") || !strcmp(argv[i], "--utc")) utc = 1;
        else if (!strcmp(argv[i], "-R") || !strcmp(argv[i], "--rfc-2822")) rfc = 1;
    }

    outb_cmos(0x70, 0x00); unsigned char s = inb_cmos(0x71);
    outb_cmos(0x70, 0x02); unsigned char m = inb_cmos(0x71);
    outb_cmos(0x70, 0x04); unsigned char h = inb_cmos(0x71);
    outb_cmos(0x70, 0x07); unsigned char day = inb_cmos(0x71);
    outb_cmos(0x70, 0x08); unsigned char mon = inb_cmos(0x71);
    outb_cmos(0x70, 0x09); unsigned char yr = inb_cmos(0x71);

    int sec_val = (s & 0x0F) + ((s >> 4) * 10);
    int min_val = (m & 0x0F) + ((m >> 4) * 10);
    int hr_val  = (h & 0x0F) + ((h >> 4) * 10);
    int d_val   = (day & 0x0F) + ((day >> 4) * 10);
    int mo_val  = (mon & 0x0F) + ((mon >> 4) * 10);
    int y_val   = (yr & 0x0F) + ((yr >> 4) * 10) + 2000;

    if (!utc) hr_val = (hr_val + 1) % 24; // MEZ

    if (rfc) {
        printf("%02d.%02d.%04d %02d:%02d:%02d +0100\n", d_val, mo_val, y_val, hr_val, min_val, sec_val);
    } else {
        printf("%02d.%02d.%04d %02d:%02d:%02d %s\n", d_val, mo_val, y_val, hr_val, min_val, sec_val, utc ? "UTC" : "MEZ");
    }
    return 0;
}