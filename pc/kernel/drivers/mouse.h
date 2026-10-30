#ifndef __MOUSE_H__
#define __MOUSE_H__

#include <stdint.h>

#define MOUSE_CURSOR_SIZE 16

typedef enum {
    MOUSE_PROTOCOL_NONE = 0,
    MOUSE_PROTOCOL_PS2 = 1,
    MOUSE_PROTOCOL_HID = 2
} MouseProtocol;

typedef struct {
    int8_t dx;
    int8_t dy;
    uint8_t buttons;
    MouseProtocol protocol;
} MousePacket;

void mouse_init(void);
void mouse_handle_interrupt(uint8_t status);
const uint8_t *mouse_cursor_pixels(void);
uint8_t mouse_cursor_width(void);
uint8_t mouse_cursor_height(void);
void mouse_set_protocol(MouseProtocol protocol);
MouseProtocol mouse_get_protocol(void);
int mouse_is_basic_mouse_supported(MouseProtocol protocol);
void mouse_update_from_packet(const MousePacket *packet);

#endif
