#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

typedef struct {
    uint8_t present;
    uint8_t percent;
    uint8_t charging;
} battery_status_t;

void battery_init(void);
battery_status_t battery_get_status(void);

#endif
