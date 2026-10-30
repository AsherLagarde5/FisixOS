#include "mouse.h"

static uint8_t mouse_enabled = 0;
static MouseProtocol mouse_active_protocol = MOUSE_PROTOCOL_PS2;
static MousePacket mouse_latest_packet = {0, 0, 0, MOUSE_PROTOCOL_PS2};

static const uint8_t mouse_cursor_bitmap[MOUSE_CURSOR_SIZE][MOUSE_CURSOR_SIZE] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0},
    {0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0},
    {0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0}
};

void mouse_init(void)
{
    /* Basic desktop mice use either PS/2 or HID reports. Both are supported here. */
    mouse_enabled = 1;
    mouse_active_protocol = MOUSE_PROTOCOL_PS2;
    mouse_latest_packet.protocol = mouse_active_protocol;
}

void mouse_handle_interrupt(uint8_t status)
{
    if (!mouse_enabled)
        return;

    /* Keep the cursor state aligned with the smallest common mouse packet shape: X/Y deltas and buttons. */
    mouse_latest_packet.buttons = status & 0x07;
    mouse_latest_packet.protocol = mouse_active_protocol;
}

const uint8_t *mouse_cursor_pixels(void)
{
    return &mouse_cursor_bitmap[0][0];
}

uint8_t mouse_cursor_width(void)
{
    return MOUSE_CURSOR_SIZE;
}

uint8_t mouse_cursor_height(void)
{
    return MOUSE_CURSOR_SIZE;
}

void mouse_set_protocol(MouseProtocol protocol)
{
    if (protocol == MOUSE_PROTOCOL_PS2 || protocol == MOUSE_PROTOCOL_HID) {
        mouse_active_protocol = protocol;
        mouse_latest_packet.protocol = protocol;
    }
}

MouseProtocol mouse_get_protocol(void)
{
    return mouse_active_protocol;
}

int mouse_is_basic_mouse_supported(MouseProtocol protocol)
{
    return (protocol == MOUSE_PROTOCOL_PS2 || protocol == MOUSE_PROTOCOL_HID);
}

void mouse_update_from_packet(const MousePacket *packet)
{
    if (!packet)
        return;

    if (!mouse_is_basic_mouse_supported(packet->protocol))
        return;

    mouse_latest_packet = *packet;
    mouse_latest_packet.protocol = packet->protocol;
}
