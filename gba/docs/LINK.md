# FisixLink — Link cable file transfer

## Mode chosen for v1: SIO Normal, 8-bit

- SIOCNT 0x04000128: mode bits = Normal (00), master sets
  internal clock, slave uses external clock
- SIODATA8 0x0400012A: one byte per transfer, master clocks it
- Transfer starts when master writes SIOCNT start bit; both
  sides get an interrupt / status bit when done

Why not Multiplayer mode first? Multiplayer can do 4 players
and 16-bit transfers, but its handshake is more complex and
timing-sensitive. Normal mode is the simplest correct transfer
to prove the protocol, then Multiplayer is a v2 speed upgrade.

Why not UART/JOYBUS first? UART needs a different cable/adapter
and JOYBUS is GameCube-oriented. Normal mode works with a plain
GBA link cable between two GBAs.

## Roles

- Master: the GBA that pressed "Send" / started the transfer.
  It supplies the clock.
- Slave: the other GBA, in "Receive / Link" menu, waiting.
- Roles are per-transfer, not per-device. Either GBA can be
  master next time.

## Packet format (all packets, both directions)

```
[MAGIC 0xF1][MAGIC 0x58]  "FX" in spirit, not Nintendo data
[TYPE 1 byte]
[LEN 2 bytes, little-endian, payload length, max 512]
[PAYLOAD LEN bytes]
[CRC16 2 bytes, over TYPE+LEN+PAYLOAD]
```

Types:
- 0x01 HELLO — "I am FisixGBA, version X, ready"
- 0x02 LIST_REQ / 0x03 LIST_RESP — file names + sizes
- 0x04 GET_REQ — "send me this file"
- 0x05 PUT_REQ — "I am sending this file"
- 0x06 DATA — a chunk of a file, max 512 bytes payload
- 0x07 ACK — chunk/file received, CRC ok
- 0x08 NAK — CRC bad or unknown type, resend last packet
- 0x09 DONE — transfer complete, close cleanly
- 0x0A ABORT — user cancelled / timeout

Rules:
- Sender waits for ACK before sending the next DATA chunk.
- Receiver NAKs a bad CRC; sender resends, max 3 retries,
  then ABORT.
- Timeout: 2 seconds per chunk (timer-driven). A timeout is
  an ABORT, not a silent hang.
- Filenames are the same 32-byte names as the cart filesystem,
  so a received file can be stored directly with fs_write().
- No file is executed on receipt in v1. Received files land
  in the file manager and must be opened explicitly. That is
  a deliberate safety rule: link cable is not a code-execution
  channel.

## What v1 can and cannot do

Can: two FisixGBA GBAs, link cable, list files, send/receive
a file, verify CRC, store it on the receiving cart. Then that
receiving cart can be moved to another GBA and the file is there.

Cannot yet: GBA-to-PC (needs USB link adapter + PC program),
4-player multiplayer, or transfer while a game/app is running
— link mode is a dedicated menu mode in v1.
