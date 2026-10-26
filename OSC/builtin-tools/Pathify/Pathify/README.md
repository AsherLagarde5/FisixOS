# Pathify

Pathify applies named Windows PATH presets by writing the registry directly. It does not use `setx`. Each change replaces the selected PATH value, asks for confirmation unless `-f` is supplied, and broadcasts the Windows environment-change notification.

## Build the Windows installer

Install the Rust toolchain and Windows .NET Framework compiler, then run this from PowerShell in the project directory:

```powershell
.\build-installer.ps1
```

The script builds the release executable and creates `target\exe-build\installer\Pathify-<version>-windows-setup.exe`, a branded, standalone Windows setup application. It embeds the Rust CLI, the installed app's C# WinForms GUI, the Go and Rust example module DLLs, and the uninstaller. The GUI includes animated navigation, direct controls for PATH changes, presets, backups, folder archives, and native modules, plus a Command Center that runs any CLI command with its interactive options. Building also requires Go and x64 MinGW-w64 GCC for the Go c-shared DLL. The setup offers an install-folder picker and lets you choose whether to add Pathify to your user PATH or create a desktop shortcut. Use the Dark mode button to switch the installer theme. The default install location is `C:\Program Files\Webtek\Pathify`; Windows requests administrator permission to install there. Open a new terminal to use the `Pathify` command. To uninstall, run the installed `Uninstall-Pathify.ps1` or use Pathify's entry in Windows Installed Apps; it removes Pathify and bundled example modules while leaving other modules and unrecognized files untouched.

## Commands

```text
Pathify Set <Preset> [-f] [-s] [-r] [-fr] [-rs] [-frs] [-rse] [-ddr] [-ri] [-b]
Pathify Restore [--latest] [-s] [-f]
Pathify Restore --backup <FILE> [-f]
Pathify Backup [-s]
Pathify Backups
Pathify Clean [-s | --preset <NAME>] [--include-presets] [--remove-broken] [--remove-shadowed] [--apply]
Pathify Nuke <FILE> [--mode shuffle|overkill]
Pathify ReReg
Pathify Gui
Pathify Archive Create [FOLDER] [--mode fast|balanced|max]
Pathify Archive Info <ZIP> [--mode fast|balanced|max]
Pathify Archive Restore <ZIP> <DESTINATION>
Pathify Archive Delete <ZIP>
Pathify Preset <Create|Edit|Rename|Remove> ...
Pathify Import <FILE> [-f]
Pathify Export <FILE> [-f]
Pathify Preview <Preset> [-s]
Pathify Validate [Preset]
Pathify Doctor
Pathify Module List
Pathify Module Run <NAME> [ARGS...]
Pathify List
Pathify Init
Pathify Launch <powershell|pwsh|cmd>
Pathify <MODULE-NAME> [ARGS...]
```

### Native modules

Pathify can run x64 Windows DLL modules written in Go or Rust. Each module provides a `module.json` manifest and `module.dll`, and exports the versioned C ABI documented in [modules/README.md](./modules/README.md). Install module folders under `<Pathify install directory>\modules\<name>` or `%APPDATA%\Pathify\modules\<name>`. List modules with `Pathify Module List`; invoke them as `Pathify hello-go "some argument"` or `Pathify Module Run hello-rust "some argument"`. The GUI lists installed modules on its Modules page.

Build the Go and Rust example modules with `.\build-modules.ps1`. Native modules are not sandboxed and run with the same permissions as Pathify. Only use modules you trust; elevated Pathify runs load modules with administrator permissions.

| Flag | Behavior |
| --- | --- |
| `-f` | Bypass the PATH replacement confirmation. |
| `-s` | Write the system-wide PATH. Administrator rights are required. Without it, Pathify changes the current user's PATH. |
| `-r` | Request a restart of eligible managed PowerShell/CMD sessions after a separate confirmation. The old shells are closed and replacements start fresh; unsaved work can be lost. |
| `-fr` | Force-restart eligible managed sessions after a separate confirmation. This forcibly terminates them; unsaved work can be lost. |
| `-rs` | Ask separately before scheduling a Windows restart 30 seconds after applying the change. |
| `-frs` | Ask separately, then force-restart Windows in 30 seconds; running apps may lose unsaved work. |
| `-rse` | Ask before force-closing all `explorer.exe` processes and starting Explorer again. |
| `-ddr` | Ask before restarting every present Display-class device with `pnputil`; screens may blank and admin rights are required. |
| `-ri` | Ask before restarting active physical network adapters; this disconnects internet and may end remote sessions. |
| `-b` | Save the existing PATH as a JSON snapshot in `%APPDATA%\Pathify\backups` before changing it. |
| `-h`, `-help` | Show help for the current command. |

`Pathify Backup` saves the current user PATH to a timestamped JSON snapshot; use `-s` to back up the system PATH. `Pathify Restore` restores the latest user JSON snapshot by default; `--latest` is optional, and `-s` selects the system scope. Latest selection never crosses scopes. Use `--backup <FILE>` to choose a listed snapshot; its filename determines the scope, so `--system` cannot be combined with it. Restore saves the current value as an undo point first. `Set -b` and restore undo points use the same JSON format. Older `.txt` backups remain readable.

`Pathify Clean` previews the user PATH by default and makes no changes. Add `-s` to select the system PATH, or `--preset <NAME>` to select a saved preset. It reports normalized duplicates (including differences in case, slash style, quotes, and trailing separators), nonexistent or unresolved directories, and distinct entries that resolve to the same canonical directory. Existing directories are canonicalized for shadow detection. The first occurrence and order are preserved; empty PATH segments remain unchanged. When cleaning user or system PATH, entries referenced by any saved preset are protected by default; `--include-presets` opts out. Broken and shadowed entries are reported but kept unless `--remove-broken` or `--remove-shadowed` is explicitly supplied. Use `--apply` to review the selected removals, confirm, and save a backup before writing. Registry PATH changes broadcast the environment update, and system PATH cleanup requires administrator rights. Cleaning a preset writes a timestamped JSON copy of the preset file before updating it. Preview mode never writes.

`Pathify Nuke <FILE>` refuses directories, symbolic links, multiply hard-linked files, and files inside Windows, Program Files, or ProgramData protected trees. In the CLI it requires typing `NUKE`; the GUI uses a native confirmation dialog instead. It supports `--mode shuffle` (the default: permute the file bytes once) or `--mode overkill` (seven cryptographically random overwrite passes). Both modes truncate and delete the file afterward; Overkill streams data in fixed-size chunks. **Neither mode can make recovery impossible or guarantee secure erasure**: SSD wear-leveling, filesystem snapshots/journals, backups, cloud sync, and other copies may retain data. Shuffle mode also needs enough memory to hold the whole file.

`Pathify ReReg` requests administrator permission and saves binary copies of `HKLM\SYSTEM`, `HKLM\SOFTWARE`, `HKLM\SAM`, `HKLM\SECURITY`, `HKEY_USERS\.DEFAULT`, and loaded user hives to a timestamped directory under `%APPDATA%\Pathify\registry-backups`. In the CLI it requires typing `BACKUP REGISTRY`; the GUI uses a native confirmation dialog. The copies can contain credential material and must be protected. Hive saves happen sequentially, not as an atomic snapshot; volatile `HKLM\HARDWARE` is excluded, and a failed save leaves any already-created hive files in place with an error identifying the backup directory.

`Pathify Gui` groups navigation into category tabs (PATH, Files, System, Advanced) with the individual features nested underneath. Sensitive actions in feature pages use Windows confirmation dialogs, then run without opening an interactive CLI console; elevated actions still require the standard Windows UAC approval. The Files category includes Nuke, with file selection and Shuffle/Overkill modes; the System category includes ReReg. Both apply the same safety checks and warnings as the CLI. The Overview has direct `Backup PATH`, `Restore PATH`, and `Make preset` actions for the selected user or system scope. The Cleaner page selects User PATH, System PATH, or a saved preset; protects preset-used PATH entries by default; and offers separate opt-ins for removing broken and shadowed entries. Scan is preview-only and Apply shows the reviewed cleanup before backup and write. Restore uses the latest backup for that scope and creates an undo backup before applying it. The preset editor uses a resizable split view and wrapping action buttons so its list, fields, and controls remain separate as the window changes size. The dark interface uses animated collapsible navigation and page transitions. The Command Center passes any CLI command and its interactive options through to the command-line executable.

`Pathify Archive Create` opens a folder picker; provide `FOLDER` to run from a command line. The default is `--mode max` (Deflate level 9); `balanced` uses Deflate level 6, and `fast` stores files without compression. The ZIP is written under the Pathify executable's `archive` folder as `<folder>.zip`. After building and reading back the complete ZIP, Pathify checks that the source has not changed, scans for locked files, atomically moves the source folder aside, and creates a `.lnk` restore menu in its place. The shortcut records the compression mode in its arguments and description. Pathify retries cleanup and reports the recovery location if Windows prevents complete deletion. A failed archive or shortcut operation leaves the original folder intact; archive cleanup failures are reported.

Open the shortcut for restore, archive details, deletion, or restore to a new location. Restoring creates the destination from a temporary extraction and leaves the ZIP intact; restoring to the original location removes its shortcut. Restore destinations must not already exist. ZIP extraction rejects unsafe paths. Folder archiving refuses symbolic links and reparse points rather than following them outside the selected folder. The archive is first created on the Pathify install drive, so ensure there is enough free space for the ZIP before archiving. Uninstall leaves the `archive` folder and other unrecognized files untouched.

Manage the JSON presets with `Pathify Preset Create <Name> <Entry>...`, `Edit <Name> <Entry>...`, `Rename <Name> <NewName>`, and `Remove <Name>`. Edit and Remove ask before changing the preset file; their `-f` option skips that prompt. Export writes a preset JSON file; Import merges presets and asks before replacing name matches. Use `-f` with Import or Export to skip eligible overwrite prompts.

`Preview <Preset>` shows the selected user PATH diff without writing anything; add `-s` to inspect the system PATH. `Validate [Preset]` checks one preset or all presets for malformed entries, duplicates, unresolved environment variables, and missing directories. `Doctor` reports config, registry-read, backup, and managed-session status without changing PATH.

`-fr` does not imply `-f`; combine them (`-f -fr`) to skip the PATH confirmation and force-restart managed sessions. The shell-restart confirmation is separate and cannot be bypassed with `-f`. `-r` and `-fr` only act on sessions opened with `Pathify Launch`; other terminals are left alone. A replacement shell does not resume the old interactive session or preserve its arguments or working directory, so save work and expect to restore your shell context manually. Graceful restart can leave a session running if Windows does not close it in response to the request.

The `-frs`, `-rse`, `-ddr`, and `-ri` action confirmations are independent of `-f` and cannot be bypassed.

The first `List` or `Set` creates `%APPDATA%\Pathify\presets.json` with an `Example` preset. Edit this file to add or change presets. `Example` is a minimal Windows system-tools PATH, not a backup of your current PATH.

## Examples

```powershell
Pathify List
Pathify Set Example
Pathify Set -f -b Example
Pathify Set -s -b Example
Pathify Set -r Example
Pathify Set -fr Example
Pathify Set -rs Example
Pathify Backup
Pathify Restore --latest
Pathify Restore --latest --system
Pathify Backups
Pathify Gui
Pathify Archive Create
Pathify Archive Create "C:\Games\Old Game" --mode max
Pathify Preset Create Tools "C:\Tools" "C:\SDK\bin"
Pathify Preset Rename Tools DevTools
Pathify Export .\pathify-presets.json
Pathify Import .\pathify-presets.json
Pathify Launch pwsh
```

The current PATH is backed up only when `-b` is supplied. Registry writes preserve percent-style environment variables by storing the new value as `REG_EXPAND_SZ`.

Preset and managed-session JSON updates are written to a temporary file and replaced only after serialization and flushing succeed.