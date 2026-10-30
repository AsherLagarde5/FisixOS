#ifndef WWAN_H
#define WWAN_H

#include <stdint.h>

/*
 * FisixOS WWAN / 4G LTE driver, v1.
 *
 * Scope, stated honestly:
 *  - Detect a possible LTE modem on PCI (WWAN cards in LTE laptops
 *    usually sit on an internal USB bus, so a full USB MBIM data
 *    path still needs working xHCI enumeration, which is currently
 *    stuck at Enable Slot. This driver does NOT claim a live
 *    connection yet.
 *  - Provide the two control paths LTE modems actually use:
 *    (a) MBIM framing: build OPEN / COMMAND messages. The caller
 *        supplies the service UUID, so no unverified UUID bytes
 *        are baked in as fact here.
 *    (b) AT commands over a serial (CDC-ACM / vendor) port:
 *        parse +CGMI / +CSQ / +CREG / +CGPADDR responses.
 *  - No TCP/IP stack exists in FisixOS yet, so there is no
 *    data-plane (PDP context -> IP -> socket) in v1. That is
 *    stage 2, after xHCI enumeration + a minimal IP stack.
 */

#define WWAN_VENDOR_QUALCOMM 0x17cbu
#define WWAN_VENDOR_INTEL    0x8086u
#define WWAN_VENDOR_FIBOCOM  0x2cb7u
#define WWAN_VENDOR_SIERRA   0x1199u
#define WWAN_VENDOR_TELIT    0x1bc7u
#define WWAN_VENDOR_QUECTEL  0x2c7cu

/* MBIM message types (MBIM spec, little-endian on the wire). */
#define MBIM_TYPE_OPEN    1u
#define MBIM_TYPE_CLOSE   2u
#define MBIM_TYPE_COMMAND 3u

typedef struct {
    int present;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    int likely_wwan; /* 1 = known WWAN vendor or WWAN-like class */
} wwan_device_t;

typedef struct {
    int signal_dbm;      /* from +CSQ, -113 + 2*csq, or -999 unknown */
    int signal_csq;      /* 0-31, 99 = unknown */
    int registered;      /* 1 = home/roaming registered from +CREG */
    int roaming;
    char ip[48];         /* from +CGPADDR, or "" */
    char operator_name[48];
} wwan_status_t;

/* PCI detection. Returns 1 and fills out on first candidate, else 0. */
int wwan_detect_pci(wwan_device_t *out);
int wwan_vendor_is_known(uint16_t vendor_id);

/* MBIM framing. Returns bytes written, or 0 on bad args / too small. */
uint32_t wwan_mbim_build_open(uint8_t *buf, uint32_t buf_len,
                              uint32_t transaction_id, uint32_t max_control_transfer);
uint32_t wwan_mbim_build_command(uint8_t *buf, uint32_t buf_len,
                                 uint32_t transaction_id,
                                 const uint8_t service_uuid[16],
                                 uint32_t command_id,
                                 const uint8_t *payload, uint32_t payload_len);

/* AT parsing. Each returns 1 on a recognised response, else 0. */
int wwan_at_parse_csq(const char *resp, wwan_status_t *st);
int wwan_at_parse_creg(const char *resp, wwan_status_t *st);
int wwan_at_parse_cgpaddr(const char *resp, wwan_status_t *st);
void wwan_status_init(wwan_status_t *st);

/* Kernel hook: scan PCI and print what was found on serial. */
void wwan_init(void);

#endif
