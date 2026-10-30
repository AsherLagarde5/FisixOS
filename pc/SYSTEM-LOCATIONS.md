# FisixOS system locations: PATH and install location

Added 2026-10-25 at Derek's request, as the Fisix equivalents of
Windows' Program Files / AppData / PATH.

## Install location: SYSTEM/APPS/

This is Program Files for FisixOS. Installed .fse programs live
here, e.g.:

```
SYSTEM/APPS/BROWSY.FSE
SYSTEM/APPS/PATHIFY.FSE
SYSTEM/APPS/RETERM.FSE
SYSTEM/APPS/Z.FSE
SYSTEM/APPS/CLIPONG.FSE
```

In the loader terminal today, run them with their path, e.g.
`run SYSTEM\APPS\BROWSY.FSE`, or copy a .fse to the disk root
and run `run BROWSY.FSE`. The loader does not yet search PATH
automatically — that is a later loader change, not claimed here.

## App data: SYSTEM/DATA/

This is AppData for FisixOS. Per-tool data files live here,
not mixed in with the programs:

```
SYSTEM/DATA/PATH.BAK     (Pathify backup, see below)
SYSTEM/DATA/BROWSY.TXT   (reserved for Browsy bookmarks/history)
SYSTEM/DATA/CLIPONG.TXT  (reserved for CliPong scores)
SYSTEM/DATA/RETERM/      (reserved, formerly ReTerm modules — .mod files were removed 2026-10-25, ReTerm is now Dosify)
```

## PATH: SYSTEM/PATH.TXT

The PATH itself is a single text file, semicolon-separated,
managed by Pathify (pathify.fse):

```
SYSTEM/APPS;SYSTEM/DRIVERS
```

Default if no PATH file exists: `SYSTEM/APPS;SYSTEM/DRIVERS`.
Pathify reads `SYSTEM\PATH.TXT` first, then falls back to the
older root file `fisixpath.txt` for disks made before SYSTEM/
existed. It saves to `SYSTEM\PATH.TXT` first, then falls back
to `fisixpath.txt`. Backups go to `SYSTEM\DATA\PATH.BAK`
first, then `fisixpath.bak`.

## Full system layout (extends the FisixFF layout)

```
SYSTEM/BOOT/      BOOTX64.EFI, KERNEL.BIN, STARTUP.NSH
SYSTEM/APPS/      installed .fse programs (Program Files)
SYSTEM/DRIVERS/   driver binaries / notes
SYSTEM/DATA/      app data (AppData)
SYSTEM/USER/      user files
SYSTEM/PATH.TXT   PATH
```

On Liveboot, writes to SYSTEM/ fail by design (RAM-only), so
Pathify reports Save/Backup failures honestly instead of
pretending they wrote.
