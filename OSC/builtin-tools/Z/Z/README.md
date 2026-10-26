# Z language

Z is a small command language. Its compiler currently targets Windows x86-64
and writes a PE executable directly: it emits the executable headers, x86-64
machine code, and Windows import tables itself. It does not invoke `rustc`,
`link.exe`, or another compiler/linker when compiling a `.z` source file.
This gives Z programs native machine-code executables; it does not make every
program automatically faster than C, and the current language is intentionally
small.

## Build the Z compiler

```powershell
cargo build --release
```

## Run a program

```powershell
target\release\z-lang.exe assets\test.z
```

## Compile a native executable

```powershell
target\release\z-lang.exe compile assets\test.z
```

By default, this creates `assets\test.exe`. Choose another output path with
`-o`:

```powershell
target\release\z-lang.exe compile assets\test.z -o build\my_program.exe
```

The resulting executable is standalone and needs neither the Z compiler nor
Rust at runtime. The compiler supports Windows x86-64. Line-oriented `.z`
programs support the built-in commands listed below. XML programs also compile
native runtime code for `i64`/`bool` variables, arithmetic and comparisons,
`<if>`, `<while>`, and literal `<burn>` output. These conditions and loops run
inside the generated executable; they are not evaluated by the compiler.
`burn` writes bytes to standard output; the popup commands call the Windows
`MessageBoxW` API.

```text
Z\burn("text")       Print text to the console
Z\spam("text")       Show a Windows information dialog
Z\legalshit("text")  Show a Windows warning dialog
```

## Importing Z modules

`Z\import("woof")` and `Z\Load "woof"` import `woof.z`, relative to the
importing file; the `.z` extension is optional.
An imported module can expose named functions, dispatch them by flags, and
receive positional string arguments through `$1`, `$2`, and so on:

```text
// woof.z
Z\def.coolmode "
Z\burn("cool/$1")
"

Z\def.dumbmode "
Z\burn("dumb/$1")
"

Z\callby("Woof")
Z\if.flag("c")
Z\then run $coolmode
Z\if.flag("d")
Z\then run $dumbmode
```

Call the exported module from another `.z` file:

```text
Z\import("woof")
woof("-c")+("and/x/nice")
Z\woof.dumbmode("example")
```

The first call selects the function registered for `-c` and passes
`and/x/nice` as `$1`; the qualified call invokes `dumbmode` directly.
Imported modules are expanded into the program during both interpretation and
native compilation. Currently, imports are Z source modules only. Native DLL
and EXE exports require explicit function signatures/calling conventions and
are not yet callable through the import syntax.

## XML core syntax

XML Z files use a `<program>` root and support `string`, `i64`, and `bool`
values, expressions, functions, conditionals, loops, and interpolation:

```xml
<program>
  <fn name="double" return="i64">
    <param name="n" type="i64"/>
    <return value="n * 2"/>
  </fn>
  <let name="answer" type="i64" value="0"/>
  <call name="double" result="answer">
    <arg value="21"/>
  </call>
  <burn value="Answer: {answer}"/>
</program>
```

`<if test="...">` can contain an `<else>` child; `<while test="...">` repeats
its children while the expression is true. A function call can assign its
typed return value to an already-declared variable using `result="name"`.
Imports use `<import file="relative-file.xmlz"/>` (the `.z` extension may be
omitted).
Logical `&&` and `||` expressions short-circuit. XML evaluation is limited to
100,000 execution steps per file expansion; exceeding the limit reports an
error rather than hanging on an accidental infinite loop.

The interpreter evaluates the full supported XML subset before dispatching
primitive commands. Native XML compilation is an independent backend and
currently supports top-level `let`/`set` for `i64` and `bool`, the operators
`+ - * / %`, comparisons, `&&`/`||`, unary `-`/`!`, `<if>`, `<else>`,
`<while>`, and literal `<burn>` text. It does not yet support native XML
functions, imports, strings, interpolation, popup commands, or runtime input;
those constructs produce a compile error rather than being silently
pre-evaluated. Infinite native loops are possible, as in other compiled
languages. XML documents are parsed with `roxmltree`, an MIT OR Apache-2.0
licensed read-only XML parser.

Lines in the command syntax may have `//` comments, including trailing
comments. A trailing comma after the final string argument is allowed.
