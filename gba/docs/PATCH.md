# Patch — batch files for ReTerm, exactly Terminal commands

Patch is a script format, alternative to .bat (Windows)
and .sh (Unix shell). A Patch script is a .patch file,
plain text, one Terminal command per line.

That last part is literal, v1.0 onward: Patch does NOT
have its own separate syntax anymore. It calls the same
terminal_exec() parser as typed input in Terminal, so
a patch line and the same line typed in Terminal do
exactly the same thing, because they are the same code.

## Commands — these are Terminal commands

```
HELP
ABOUT
EXIT
RUN <name.gep>
LIST
PATCH <name.patch>
UPDATE <name.update>
PRINT <text>
SEND <channel> <text>
RECV <channel>
CLEAR <channel>
WAIT <ms>
# a comment, or a blank line (ignored)
```

SEND/RECV/CLEAR use patch paths (see docs/PATCH.md in
v0.9, and src/pipe.c): named pipes in RAM so programs
can interact. PRINT, SEND, etc. are Terminal commands
first, Patch gets them because it is Terminal.

EXIT in a patch stops the script and returns to
Terminal. It does not exit Terminal itself. Nested
PATCH is allowed, up to 3 deep, then PATCH TOO DEEP.

## Example, DEMO.PATCH (built into v1.0)

```
# DEMO.PATCH — in Terminal commands
PRINT PATCH DEMO
RUN SENDER.GEP
RUN RECEIVER.GEP
PRINT DONE
```

Run it with, in Terminal:
```
PATCH DEMO.PATCH
RUN DEMO.PATCH
LIST
```
RUN with a .patch name also runs the script, instead
of trying to load it as ARM code. LIST shows cart
files, built-in GEPs, and built-in patches.

## Patch is not an update

A .patch file does not replace system files, it only
runs commands. An update file does that, see
docs/UPDATE.md. If you try PATCH on an .update file,
Terminal says THAT IS AN UPDATE, USE UPDATE. If you
try UPDATE on a .patch file, it says THAT IS A PATCH,
USE PATCH. The checker keeps them apart.
