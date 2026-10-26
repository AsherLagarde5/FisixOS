#include <stdint.h>
#include "boot_info.h"
#include "cpu.h"
#include "drivers/memory.h"
#include "drivers/acpi.h"
#include "drivers/system/pic.h"
#include "drivers/system/idt.h"
#include "drivers/pci.h"
#include "drivers/touchscreen.h"
#include "drivers/smbios.h"
#include "drivers/nvme.h"
#include "drivers/xhci.h"
#include "drivers/ps2kbd.h"
#include "drivers/apic.h"
#include "drivers/latitude5420.h"
#include "drivers/wwan.h"
#include "drivers/generic.h"
#include "drivers/e1000.h"
#include "drivers/net.h"
#include "drivers/syscall.h"
#include "drivers/proc.h"
#include "drivers/gui.h"

/* Serial debug output on COM1 (0x3F8), for early kernel bring-up. */
static void debug_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void debug_print(const char *s) {
    while (s && *s) {
        /* Wait for transmitter holding register to be empty (LSR bit 5),
           but give up instead of hanging forever if there is no COM1. */
        uint8_t lsr = 0;
        uint32_t timeout = 100000;
        do {
            __asm__ volatile ("inb %1, %0" : "=a"(lsr) : "Nd"((uint16_t)(0x3F8 + 5)));
            if (timeout-- == 0) {
                return;
            }
        } while ((lsr & 0x20) == 0);
        debug_outb(0x3F8, (uint8_t)*s);
        s++;
    }
}

static void debug_print_u64(uint64_t value) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (value == 0) {
        debug_print("0");
        return;
    }
    while (value > 0 && i > 0) {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    }
    debug_print(&buf[i]);
}

static void debug_print_u32(uint32_t value) {
    debug_print_u64(value);
}

static void debug_print_hex16(uint16_t value) {
    const char *digits = "0123456789ABCDEF";
    char buf[7];
    buf[0] = '0'; buf[1] = 'x';
    buf[2] = digits[(value >> 12) & 0xF];
    buf[3] = digits[(value >> 8) & 0xF];
    buf[4] = digits[(value >> 4) & 0xF];
    buf[5] = digits[value & 0xF];
    buf[6] = '\0';
    debug_print(buf);
}

static void debug_print_hex32(uint32_t value) {
    const char *digits = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) buf[2 + i] = digits[(value >> (28 - 4 * i)) & 0xF];
    buf[10] = '\0';
    debug_print(buf);
}

static void debug_init_serial(void) {
    /* Disable interrupts */
    debug_outb(0x3F8 + 1, 0x00);

    /* Enable DLAB */
    debug_outb(0x3F8 + 3, 0x80);

    /* Set baud divisor to 3 -> 38400 baud */
    debug_outb(0x3F8 + 0, 0x03);
    debug_outb(0x3F8 + 1, 0x00);

    /* 8 bits, no parity, one stop */
    debug_outb(0x3F8 + 3, 0x03);

    /* Enable FIFO, clear, 14-byte threshold */
    debug_outb(0x3F8 + 2, 0xC7);

    /* IRQs enabled, RTS/DSR set */
    debug_outb(0x3F8 + 4, 0x0B);
}

/* --- MWAHAHAHA IM A DINOSAUR --- */

static int g_hid_ready = 0;
void kernel_startup(const kernel_boot_info_t *boot_info)
{
    __asm__ volatile ("cli");

    debug_init_serial();
    debug_print("[FisixOS] Serial initialized\n");

    if (!boot_info) {
        debug_print("[FisixOS] ERROR: boot_info is NULL! Halting.\n");
        goto halt;
    }

    debug_print("[FisixOS] Boot info OK\n");

    /* Checkpoint 1: Memory bytes */
    debug_print("[FisixOS] memory_bytes = ");
    debug_print_u64(boot_info->memory_bytes);
    debug_print("\n");

    memory_set_total_bytes(boot_info->memory_bytes);
    debug_print("[FisixOS] memory_set_total_bytes OK\n");

    acpi_set_rsdp(boot_info->rsdp);
    debug_print("[FisixOS] acpi_set_rsdp OK, acpi_available = ");
    debug_print_u32((uint32_t)acpi_is_available());
    debug_print("\n");

    /* Checkpoint 2: memory_init */
    debug_print("[FisixOS] Calling memory_init...\n");
    memory_init();
    debug_print("[FisixOS] memory_init OK\n");

    /* Checkpoint 3: acpi_init */
    debug_print("[FisixOS] Calling acpi_init...\n");
    acpi_init();
    debug_print("[FisixOS] acpi_init OK\n");

    /* Checkpoint 4: cpu_init + pic_disable */
    debug_print("[FisixOS] Calling cpu_init...\n");
    cpu_init();
    {
        cpu_info_t cpu;
        if (cpu_get_info(&cpu)) {
            debug_print("[FisixOS] cpu vendor = ");
            debug_print_u32(cpu.vendor);
            debug_print(", family = ");
            debug_print_u32(cpu.family);
            debug_print(", model = ");
            debug_print_u32(cpu.model);
            debug_print(", stepping = ");
            debug_print_u32(cpu.stepping);
            debug_print("\n");
        } else {
            debug_print("[FisixOS] cpu_get_info failed\n");
        }
    }
    debug_print("[FisixOS] cpu_init OK\n");

    debug_print("[FisixOS] Disabling PIC...\n");
    pic_disable();
    debug_print("[FisixOS] PIC disabled\n");

    /* Checkpoint 5: IDT */
    debug_print("[FisixOS] Calling idt_init...\n");
    idt_init();
    debug_print("[FisixOS] idt_init OK\n");
    syscall_init();
    if (syscall_selftest()) debug_print("[FisixOS] syscall int 0x80 OK (write, ring 0)\n");
    else debug_print("[FisixOS] syscall int 0x80 SELFTEST FAILED\n");
    (void)proc_run_fse;

    /* Wi-Fi / network, step 1 only: DETECT, do not claim to connect.
       FisixOS has no Wi-Fi driver, no 802.11 stack, no WPA, no DHCP and
       no TCP/IP yet. A modern Wi-Fi card (e.g. Intel AX201) needs
       firmware loading, 802.11 management, WPA2, DHCP and IP, which is
       a large driver project, not a one-line fix. What this does is
       find a PCI network controller, if one exists, and report it, so
       a future driver knows exactly what hardware it has to talk to.
       Subclass 0x00 = Ethernet, 0x80 and others are often wireless /
       other network controllers, depending on the vendor. */
    debug_print("[FisixOS] Calling pci network detection...\n");
    {
        pci_device_t net;
        if (pci_find_device_class(PCI_CLASS_NETWORK, &net)) {
            debug_print("[FisixOS] network: PCI network controller found, bus = ");
            debug_print_u32(net.bus);
            debug_print(", device = ");
            debug_print_u32(net.device);
            debug_print(", function = ");
            debug_print_u32(net.function);
            debug_print(", vendor = ");
            debug_print_hex16(net.vendor_id);
            debug_print(", device_id = ");
            debug_print_hex16(net.device_id);
            debug_print(", subclass = ");
            debug_print_u32(net.subclass);
            debug_print(", prog_if = ");
            debug_print_u32(net.prog_if);
            debug_print("\n");
            debug_print("[FisixOS] network: detection only, Wi-Fi/Ethernet connection NOT implemented yet\n");
        } else {
            debug_print("[FisixOS] network: no PCI network controller found\n");
        }
    }

    /* Driver pack v1: Intel Tiger Lake / Iris Xe / touch / BIOS info.
       Intel's Linux graphics drivers (i915, Xe) ARE open source (GPL,
       Linux kernel + Mesa), but they are a huge Linux DRM stack that
       needs GEM, interrupts, display pipelines and binary GuC/HuC
       firmware - they cannot be dropped into FisixOS. What FisixOS
       gets here is native, testable first steps for this hardware:
       CPU brand + Tiger Lake detection, PCI display detection with
       BARs, the UEFI GOP framebuffer the loader found, SMBIOS BIOS
       info, and the correct touch model (HID over I2C, not panel). */
    debug_print("[FisixOS] driver: cpu brand = ");
    debug_print(cpu_brand());
    debug_print("\n");
    if (cpu_is_tiger_lake()) {
        debug_print("[FisixOS] driver: Tiger Lake detected (family 6, model 140) - 11th Gen, e.g. i5-1145G7, Iris Xe GT2\n");
    } else {
        debug_print("[FisixOS] driver: not Tiger Lake (family/model above); Tiger Lake check is family 6, model 140\n");
    }
    {
        pci_device_t gpu;
        if (pci_find_device_class(PCI_CLASS_DISPLAY, &gpu)) {
            debug_print("[FisixOS] driver: PCI display found, bus = ");
            debug_print_u32(gpu.bus);
            debug_print(", device = ");
            debug_print_u32(gpu.device);
            debug_print(", vendor = ");
            debug_print_hex16(gpu.vendor_id);
            debug_print(", device_id = ");
            debug_print_hex16(gpu.device_id);
            debug_print(", BAR0 = ");
            debug_print_hex32(pci_read_bar(&gpu, 0));
            debug_print(" (full Iris Xe driver NOT implemented)\n");
        } else {
            debug_print("[FisixOS] driver: no PCI display controller found\n");
        }
    }
    if (boot_info->framebuffer_base) {
        debug_print("[FisixOS] driver: GOP framebuffer base = ");
        debug_print_u64(boot_info->framebuffer_base);
        debug_print(", size = ");
        debug_print_u32(boot_info->framebuffer_width);
        debug_print("x");
        debug_print_u32(boot_info->framebuffer_height);
        debug_print(", pitch = ");
        debug_print_u32(boot_info->framebuffer_pitch);
        debug_print(" (firmware framebuffer, not Iris Xe acceleration)\n");
    } else {
        debug_print("[FisixOS] driver: no GOP framebuffer found by loader\n");
    }
    smbios_set_table(boot_info->smbios);
    if (smbios_is_available()) {
        debug_print("[FisixOS] driver: BIOS vendor = ");
        debug_print(smbios_bios_vendor());
        debug_print(", version = ");
        debug_print(smbios_bios_version());
        debug_print(", date = ");
        debug_print(smbios_bios_date());
        debug_print("\n");
    } else {
        debug_print("[FisixOS] driver: no SMBIOS table found by loader\n");
    }
    touchscreen_init();
    debug_print("[FisixOS] driver: touchscreen = USB HID digitizer via xHCI when present; laptop HID-over-I2C still needs an I2C bus driver\n");

    /* Latitude 5420 boot drivers. */
    debug_print("[FisixOS] boot: acpi_available = ");
    debug_print_u32((uint32_t)acpi_is_available());
    debug_print(", rsdp = ");
    debug_print_u64((uint64_t)(uintptr_t)boot_info->rsdp);
    debug_print("\n");
    {
        apic_info_t apic;
        if (apic_detect(&apic)) {
            debug_print("[FisixOS] boot: Local APIC base = ");
            debug_print_u64(apic.base);
            debug_print(", enabled = ");
            debug_print_u32((uint32_t)apic.enabled);
            debug_print(", version = ");
            debug_print_hex32(apic.version);
            debug_print("\n");
        } else {
            debug_print("[FisixOS] boot: Local APIC not found\n");
        }
    }
    {
        pci_device_t list[64]; int count = 0, i;
        pci_scan_all(list, 64, &count);
        debug_print("[FisixOS] boot: PCI devices found = ");
        debug_print_u32((uint32_t)count);
        debug_print("\n");
        for (i = 0; i < count && i < 64; i++) {
            debug_print("[FisixOS] boot: PCI ");
            debug_print_u32(list[i].bus);
            debug_print(":");
            debug_print_u32(list[i].device);
            debug_print(".");
            debug_print_u32(list[i].function);
            debug_print(" class ");
            debug_print_hex16((uint16_t)list[i].class_code);
            debug_print(" vendor ");
            debug_print_hex16(list[i].vendor_id);
            debug_print(" device ");
            debug_print_hex16(list[i].device_id);
            debug_print("\n");
        }
    }
    if (ps2kbd_init()) debug_print("[FisixOS] boot: PS/2 keyboard controller OK\n");
    else debug_print("[FisixOS] boot: PS/2 keyboard controller NOT present / test failed (timeout, not a hang)\n");
    {
        nvme_info_t nv;
        if (nvme_init(&nv)) {
            uint8_t lba0[512];
            debug_print("[FisixOS] boot: NVMe WORKING, model = ");
            debug_print(nv.model);
            debug_print(", serial = ");
            debug_print(nv.serial);
            debug_print(", ns_size_lba = ");
            debug_print_u64(nv.ns_size_lba);
            debug_print(", lba_bytes = ");
            debug_print_u32(nv.lba_data_size);
            debug_print("\n");
            if (nvme_read_lba(0, lba0)) {
                int i;
                debug_print("[FisixOS] boot: NVMe READ LBA0 OK, first 16 bytes = ");
                for (i = 0; i < 16; i++) {
                    const char *digits = "0123456789ABCDEF";
                    char b[3]; b[0]=digits[(lba0[i]>>4)&0xF]; b[1]=digits[lba0[i]&0xF]; b[2]='\0';
                    debug_print(b);
                }
                debug_print("\n");
            } else {
                debug_print("[FisixOS] boot: NVMe READ LBA0 FAILED\n");
            }
        } else {
            debug_print("[FisixOS] boot: NVMe init failed / no NVMe controller found\n");
        }
    }
    {
        xhci_info_t xh;
        if (xhci_init(&xh)) {
            debug_print("[FisixOS] boot: xHCI RUNNING, version ");
            debug_print_hex16(xh.version);
            debug_print(", ports ");
            debug_print_u32(xh.ports);
            debug_print(", connected ");
            debug_print_u32(xh.connected);
            debug_print(" (device enumeration / HID input NOT implemented yet)\n");
            {
                uint8_t slot = 0; uint32_t psc0 = 0;
                if (xhci_enumerate_first(&slot, &psc0)) {
                    debug_print("[FisixOS] boot: xHCI ENUMERATED first device, slot = ");
                    debug_print_u32(slot);
                    debug_print(", portsc = ");
                    debug_print_hex32(psc0);
                    debug_print("\n");
                    {
                        xhci_hid_t hid;
                        if (xhci_hid_init(&hid)) {
                            debug_print("[FisixOS] boot: xHCI HID OK, vendor ");
                            debug_print_hex16(hid.vendor_id);
                            debug_print(", product ");
                            debug_print_hex16(hid.product_id);
                            debug_print(", kind ");
                            debug_print_u32((uint32_t)hid.kind);
                            debug_print(", ep ");
                            debug_print_hex16(hid.endpoint_address);
                            debug_print(", mps ");
                            debug_print_u32(hid.max_packet);
                            debug_print("\n");
                            g_hid_ready = 1;
                        } else {
                            debug_print("[FisixOS] boot: xHCI HID init failed (not a HID device, or endpoint config failed)\n");
                        }
                    }
                } else {
                    debug_print("[FisixOS] boot: xHCI enumeration failed (slot/address stage)\n");
                }
            }
        } else {
            debug_print("[FisixOS] boot: xHCI init failed / no xHCI controller found\n");
        }
    }
    {
        wwan_init();
    }
    {
        generic_init_all();
        e1000_init_kernel();
        net_init();
    }
    {
        latitude5420_boot_t chk;
        latitude5420_check(&chk);
        chk.gop = boot_info->framebuffer_base != 0;
        debug_print("[FisixOS] boot: Latitude 5420 checklist: tiger_lake=");
        debug_print_u32((uint32_t)chk.cpu_tiger_lake);
        debug_print(" acpi="); debug_print_u32((uint32_t)chk.acpi);
        debug_print(" pci="); debug_print_u32((uint32_t)chk.pci_count);
        debug_print(" apic="); debug_print_u32((uint32_t)chk.apic);
        debug_print(" gop="); debug_print_u32((uint32_t)chk.gop);
        debug_print(" ps2="); debug_print_u32((uint32_t)chk.ps2kbd);
        debug_print(" nvme="); debug_print_u32((uint32_t)chk.nvme);
        debug_print(" xhci="); debug_print_u32((uint32_t)chk.xhci);
        debug_print(" serial_io="); debug_print_u32((uint32_t)chk.serial_io);
        debug_print(" ethernet="); debug_print_u32((uint32_t)chk.ethernet);
        debug_print(" wifi_cnvi=0 (not claimed) smbios="); debug_print_u32((uint32_t)chk.smbios);
        debug_print("\n");
    }

    debug_print("[FisixOS] Kernel early setup complete.\n");
    if (boot_info->framebuffer_base) {
        gui_init(boot_info->framebuffer_base, boot_info->framebuffer_width, boot_info->framebuffer_height, boot_info->framebuffer_pitch);
        if (gui_is_ready()) {
            gui_draw();
            debug_print("[FisixOS] GUI_START taskbar+window drawn\n");
            while (1) {
                gui_poll_input();
                net_poll();
                if (g_hid_ready) {
                    xhci_hid_report_t rep;
                    int r = xhci_hid_poll(&rep);
                    if (r > 0) {
                        if (rep.kind == XHCI_HID_MOUSE) {
                            gui_usb_move(rep.dx, rep.dy, rep.buttons);
                        } else if (rep.kind == XHCI_HID_TABLET || rep.kind == XHCI_HID_TOUCH) {
                            uint32_t mx = rep.logical_max_x ? rep.logical_max_x : 32767;
                            uint32_t my = rep.logical_max_y ? rep.logical_max_y : 32767;
                            int sx = (int)((uint32_t)rep.abs_x * gui_width() / mx);
                            int sy = (int)((uint32_t)rep.abs_y * gui_height() / my);
                            touchscreen_update_absolute(sx, sy, rep.down);
                            gui_touch_set(sx, sy, rep.down);
                        }
                    }
                }
                gui_tick();
                __asm__ volatile ("pause");
            }
        }
    }
    debug_print("[FisixOS] Entering idle loop (no framebuffer for GUI).\n");

halt:
    while (1) {
        __asm__ volatile ("hlt");
    }
}
