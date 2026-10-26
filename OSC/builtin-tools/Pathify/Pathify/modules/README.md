# Native Pathify modules

Pathify modules are x64 Windows DLLs that export one synchronous C ABI. ABI version 1 uses these exports:

```c
uint32_t PathifyModuleAbiVersion(void);
const char *PathifyModuleName(void);
const char *PathifyModuleDescription(void);
int32_t PathifyModuleRun(
    const uint8_t *arguments_json,
    uint32_t arguments_length,
    uint8_t *output,
    uint32_t output_capacity);
```

The request is a UTF-8 JSON array of command-line argument strings. Return the number of UTF-8 output bytes written. Return a negative number on failure and write a NUL-terminated error message when the output buffer permits. The output buffer is limited to 4 MiB. Metadata strings must remain valid for the lifetime of the DLL. Pathify keeps a module loaded until its process exits because some runtimes, including Go's, cannot be safely unloaded.

Each module is installed in its own directory containing `module.json` and `module.dll`. The manifest format is:

```json
{
  "name": "hello-go",
  "version": "1.0.0",
  "description": "Example Go module that echoes its arguments.",
  "dll": "module.dll",
  "abi_version": 1
}
```

Names must be unique, lowercase, and consist of letters, digits, and hyphens; they cannot shadow built-in Pathify commands. The module folder must have the same name. Pathify checks that the DLL is a regular file inside that folder. Modules are discovered from:

- `<Pathify install directory>\modules\<name>\`
- `%APPDATA%\Pathify\modules\<name>\`

Run `Pathify Module List` to list installed modules. Run a module as `Pathify <module-name> <arguments...>` or `Pathify Module Run <module-name> <arguments...>`. The GUI's **Modules** page lists the same manifest data, requests confirmation, and passes arguments through to the DLL.

## Build the examples

Install Go, Rust/Cargo, and an x64 MinGW-w64 GCC compiler (needed by Go cgo), then run:

```powershell
.\build-modules.ps1
```

This writes ready-to-copy module folders under `target\exe-build\modules`. Copy either module's directory to one of Pathify's module directories. The standalone installer builds and bundles both examples automatically.

The example sources are in `examples\hello-go` and `examples\hello-rust`. Go modules use `go build -buildmode=c-shared`; Rust modules use the `cdylib` crate type.

## Security

Native DLL modules are **not sandboxed**. Loading a DLL runs its native initialization code, and invoking it runs arbitrary code with the same permissions as Pathify. A module launched elevated has administrator permissions. Only install and run modules you trust. Listing modules reads manifests and does not load their DLLs.
