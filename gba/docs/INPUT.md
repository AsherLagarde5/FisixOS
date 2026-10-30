# FisixGBA input — GBA buttons, not Xbox buttons

## The layout, because Xbox and GBA are different

Xbox (what Derek uses):
```
      Y (top)
  X         B (right)
      A (bottom)
```

Game Boy Advance (real hardware, only these exist):
```
Left side:  D-pad
Right side: B (left, slightly lower)   A (right)
Centre:     SELECT (left)  START (right)
Top edge:   L shoulder (left)  R shoulder (right)
```

There is NO X button and NO Y button on a GBA. KEYINPUT
0x04000130 only has bits for A, B, Select, Start, Right,
Left, Up, Down, R, L. Code that waits for Y would never
trigger on real hardware, so Y cannot be the keyboard key.

## What Derek asked for, translated to GBA buttons

Derek's intent:
- open keyboard, type with D-pad + A, Start sends
- in GUI, D-pad moves the mouse, A clicks, a second button
  right-clicks

GBA translation, implemented in v0.3:
- Keyboard open = SELECT, not Y. Select is unused elsewhere
  in terminal, it sits in the centre, and it is the closest
  thing to a "bring up input" key on this hardware.
- Right-click = B. B is the second face button, to the left
  of A. That is the natural left-click / right-click pair:
  A = primary, B = secondary, exactly like a 2-button mouse.
- The shoulder buttons L/R are kept free for v3 (for example
  L = faster mouse, R = faster keyboard cursor), not used
  for primary actions in v0.3, so nothing is overloaded.

## Scheme table

Main menu (list mode, NOT mouse mode):
- D-pad Up/Down: move selection
- A / Start: open selected item
- B: back (where applicable)

Terminal (ReTerm-GBA):
- SELECT: open on-screen keyboard
- In keyboard: D-pad moves letter cursor, A types letter,
  B deletes last letter, START sends the command and
  closes the keyboard, SELECT cancels and closes the
  keyboard without sending (keyboard_input returns -1)
- Outside keyboard: B exits terminal to menu
- Commands: HELP, FILES, ABOUT, EXIT (typed in caps via
  the keyboard, which renders lowercase as uppercase)

GUI mouse mode (new app, "GUI" in the main menu):
- D-pad: move mouse cursor, 3 pixels per frame, clamped
  to 0..239 x 0..159
- A (left click): if cursor is over an icon, launch that app
- B (right click): if cursor is over an icon, show that
  app's info line at the bottom; if over empty space, show
  "RIGHT CLICK AT X,Y"
- SELECT: exit GUI back to main menu
- START: also exits GUI (so Start is not trapped in GUI)

First-time setup keyboard:
- Same keyboard as terminal: D-pad, A, B delete, START done.
  Setup already used this in v0.2; v0.3 keeps it, so the
  keyboard feels identical everywhere.

## Small-screen note

Screen is 240x160. The keyboard is 13 columns x 3 rows,
letters 16 pixels apart, so it fits without scrolling.
Terminal text is 8-pixel font, 30 characters per line max.
GUI icons are 32x32 boxes with a 1-line label, 4 across,
so a mouse click target is big enough for D-pad control.
