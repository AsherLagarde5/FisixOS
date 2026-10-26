# GNU Nano source

The prebuilt `nano.mod` package has been removed because ReTerm `.mod` files
now contain source code only. The Nano source and license are included in
[`nano/`](./nano/).

GNU Nano is a multi-file C project; the current source-module format accepts a
single source file and does not package its build system or C dependencies.

## Build Nano on Linux

Install a C compiler, Autotools, Git, and wide-character ncurses development
headers for the distribution. Then build locally:

```sh
cd appport/nano/nano
./autogen.sh
./configure --disable-nls --disable-maintainer-mode
make -C lib
make -C src nano
```

Nano is licensed under GPL-3.0-or-later. Its corresponding source and license
are included in [`nano/`](./nano/).
