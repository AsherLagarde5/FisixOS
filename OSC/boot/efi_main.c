#include "efi.h"
#include "eosfs.h"
#include "memory.h"
#include "accounts.h"
#include "../kernel/boot_info.h"

EFI_HANDLE gImageHandle;
EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;

/* Minimal COM1 serial debug, for QEMU: OVMF also logs to COM1, so these
   markers appear in the serial log before any ConOut output. */
static void serial_outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static uint8_t serial_inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static void serial_mark(const char *s) {
    serial_outb(0x3F8 + 1, 0x00);
    serial_outb(0x3F8 + 3, 0x80);
    serial_outb(0x3F8 + 0, 0x03);
    serial_outb(0x3F8 + 1, 0x00);
    serial_outb(0x3F8 + 3, 0x03);
    serial_outb(0x3F8 + 2, 0xC7);
    serial_outb(0x3F8 + 4, 0x0B);
    while (s && *s) {
        uint32_t t = 100000;
        while ((serial_inb(0x3F8 + 5) & 0x20) == 0 && t-- > 0) { }
        serial_outb(0x3F8, (uint8_t)*s);
        s++;
    }
    serial_outb(0x3F8, (uint8_t)'\n');
}

static const CHAR16 boot_message[] = {
    'F','i','s','i','x',' ','O','S',' ','E','F','I',' ','b','o','o','t','\r','\n',0
};

typedef enum {
    TERMINAL_MODE_BATCH = 0,
    TERMINAL_MODE_BASH = 1
} TerminalMode;

static void terminal_run_nano(void);
static void terminal_first_time_setup(void);
static int terminal_login(void);
static int terminal_read_line(CHAR16 *line, size_t max_length);
static int terminal_execute_command(const CHAR16 *command);

#define TERMINAL_MAX_ARGS 8
#define TERMINAL_MAX_LINE 128
#define TERMINAL_MAX_EDITOR_LINES 8
#define TERMINAL_DEFAULT_DRIVE 'B'

static TerminalMode terminal_mode = TERMINAL_MODE_BATCH;
static int terminal_echo_enabled = 1;
static CHAR16 terminal_current_drive = TERMINAL_DEFAULT_DRIVE;
static CHAR16 terminal_current_path[TERMINAL_MAX_LINE] = {
    TERMINAL_DEFAULT_DRIVE, ':', '/', 0
};

static const CHAR16 autoexec_batch[] = {
    '@','e','c','h','o',' ','o','f','f','\r','\n',
    'h','e','l','p','\r','\n',
    's','t','a','t','u','s','\r','\n',
    'v','e','r','\r','\n',
    'l','s','\r','\n',
    'm','e','m','\r','\n',
    'c','l','e','a','r','\r','\n',
    0
};

static int terminal_match_command(const CHAR16 *command, const CHAR16 *name)
{
    while (*command && *name && *command == *name) {
        command++;
        name++;
    }

    return (*name == 0) && (*command == 0 || *command == ' ');
}

static const CHAR16 *terminal_command_args(const CHAR16 *command)
{
    while (*command && *command != ' ') {
        command++;
    }

    while (*command == ' ') {
        command++;
    }

    return command;
}

static void terminal_write(const CHAR16 *text)
{
    if (gST && gST->ConOut && gST->ConOut->OutputString) {
        gST->ConOut->OutputString(gST->ConOut, text);
    }
}

static void terminal_write_line(const CHAR16 *text)
{
    terminal_write(text);
    terminal_write((const CHAR16[]){ '\r', '\n', 0 });
}

/* First-time setup writes plain ASCII strings, so it converts them to
   CHAR16 here instead of spelling every message out as a CHAR16 array
   literal like the older terminal code does. */
static void terminal_write_ascii(const char *text)
{
    CHAR16 buffer[256];
    size_t i = 0;
    while (text && text[i] && i + 1 < sizeof(buffer) / sizeof(buffer[0])) {
        buffer[i] = (CHAR16)(uint8_t)text[i];
        i++;
    }
    buffer[i] = 0;
    terminal_write(buffer);
}

static void terminal_write_ascii_line(const char *text)
{
    terminal_write_ascii(text);
    terminal_write((const CHAR16[]){ '\r', '\n', 0 });
}

static int terminal_equal_str(const CHAR16 *a, const CHAR16 *b)
{
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static int terminal_has_space(const CHAR16 *s)
{
    if (!s) return 0;
    while (*s) { if (*s == ' ') return 1; s++; }
    return 0;
}

static void terminal_set_drive(CHAR16 drive)
{
    if (drive == 'B' || drive == 'C') {
        terminal_current_drive = drive;
        terminal_current_path[0] = drive;
        terminal_current_path[1] = ':';
        terminal_current_path[2] = '/';
        terminal_current_path[3] = 0;
    }
}

static int terminal_drive_prefix(const CHAR16 *path, CHAR16 *drive)
{
    if (path && path[0] >= 'A' && path[0] <= 'Z' && path[1] == ':') {
        *drive = path[0];
        return 1;
    }

    return 0;
}

static void terminal_show_drive_root(CHAR16 drive)
{
    eosfs_entry_t entries[EOSFS_MAX_ENTRIES];
    int count = 0;
    int i;

    if (!eosfs_list_path(terminal_current_path, entries, EOSFS_MAX_ENTRIES, &count)) {
        terminal_write_line((const CHAR16[]){ 'f','i','l','e',' ','s','y','s','t','e','m',' ','u','n','a','v','a','i','l','a','b','l','e',0 });
        return;
    }

    if (drive == 'C') {
        terminal_write_line((const CHAR16[]){ 'C',':','/',' ','-',' ','G','P','T',' ','d','i','s','k',' ','p','a','r','t','i','t','i','o','n',0 });
    } else {
        terminal_write_line((const CHAR16[]){ 'B',':','/',' ','-',' ','G','P','T',' ','d','i','s','k',' ','p','a','r','t','i','t','i','o','n',0 });
    }

    for (i = 0; i < count; ++i) {
        terminal_write(entries[i].name);
        terminal_write((const CHAR16[]){ ' ', 0 });
        terminal_write(entries[i].type == EOSFS_ENTRY_DIR ? (const CHAR16[]){ '<','d','i','r','>',0 } : (const CHAR16[]){ '<','f','i','l','e','>',0 });
        terminal_write_line((const CHAR16[]){ 0 });
    }
}

static const CHAR16 *terminal_current_prompt(void)
{
    static CHAR16 prompt[64];
    int index = 0;

    prompt[index++] = terminal_current_drive;
    prompt[index++] = ':';
    prompt[index++] = '/';
    prompt[index++] = ' ';

    if (terminal_mode == TERMINAL_MODE_BASH) {
        prompt[index++] = 'b';
        prompt[index++] = 'a';
        prompt[index++] = 's';
        prompt[index++] = 'h';
    } else {
        prompt[index++] = 'b';
        prompt[index++] = 'a';
        prompt[index++] = 't';
        prompt[index++] = 'c';
        prompt[index++] = 'h';
    }

    prompt[index++] = '>';
    prompt[index++] = ' ';
    prompt[index] = 0;

    return prompt;
}

static void terminal_set_mode(TerminalMode mode)
{
    terminal_mode = mode;
    terminal_echo_enabled = 1;
}

static void terminal_write_arg_list(CHAR16 *argv[], int argc, int start_index)
{
    int i;
    for (i = start_index; i < argc; ++i) {
        if (i > start_index) {
            terminal_write((const CHAR16[]){ ' ', 0 });
        }
        terminal_write(argv[i]);
    }
}

static int terminal_parse_command(const CHAR16 *command, CHAR16 *argv[], int max_args)
{
    static CHAR16 line[TERMINAL_MAX_LINE];
    size_t i = 0;
    int argc = 0;
    CHAR16 *cursor;

    while (*command && i < (TERMINAL_MAX_LINE - 1)) {
        line[i++] = *command++;
    }
    line[i] = 0;

    cursor = line;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }

    while (*cursor && argc < max_args) {
        argv[argc++] = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t') {
            cursor++;
        }

        if (*cursor == 0) {
            break;
        }

        *cursor++ = 0;
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
    }

    return argc;
}

static int terminal_contains(const CHAR16 *text, const CHAR16 *needle)
{
    if (*needle == 0) return 1;
    while (*text) {
        const CHAR16 *a = text;
        const CHAR16 *b = needle;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (*b == 0) return 1;
        ++text;
    }
    return 0;
}

static void terminal_copy_segment(CHAR16 *destination, const CHAR16 *start, const CHAR16 *end)
{
    size_t length = 0;
    while (start < end && (*start == ' ' || *start == '\t')) ++start;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
    while (start < end && length + 1 < TERMINAL_MAX_LINE) destination[length++] = *start++;
    destination[length] = 0;
}

static const CHAR16 *terminal_end(const CHAR16 *text)
{
    while (*text) ++text;
    return text;
}

static int terminal_try_pipeline(const CHAR16 *command)
{
    const CHAR16 *pipe = command;
    CHAR16 left[TERMINAL_MAX_LINE];
    CHAR16 right[TERMINAL_MAX_LINE];
    CHAR16 *argv[TERMINAL_MAX_ARGS];
    int argc;

    while (*pipe && *pipe != '|') ++pipe;
    if (*pipe != '|') return -1;
    terminal_copy_segment(left, command, pipe);
    terminal_copy_segment(right, pipe + 1, terminal_end(pipe + 1));
    argc = terminal_parse_command(left, argv, TERMINAL_MAX_ARGS);
    if (argc < 2 || !terminal_match_command(argv[0], (const CHAR16[]){'e','c','h','o',0})) {
        terminal_write_line((const CHAR16[]){'p','i','p','e','s',' ','c','u','r','r','e','n','t','l','y',' ','s','u','p','p','o','r','t',' ','e','c','h','o',' ','|',' ','g','r','e','p',0});
        return 0;
    }
    argc = terminal_parse_command(right, argv, TERMINAL_MAX_ARGS);
    if (argc != 2 || !terminal_match_command(argv[0], (const CHAR16[]){'g','r','e','p',0})) {
        terminal_write_line((const CHAR16[]){'u','s','a','g','e',':',' ','e','c','h','o',' ','<','t','e','x','t','>',' ','|',' ','g','r','e','p',' ','<','p','a','t','t','e','r','n','>',0});
        return 0;
    }
    terminal_copy_segment(left, command + 5, pipe);
    if (terminal_contains(left, argv[1])) terminal_write_line(left);
    return 0;
}

static int terminal_open_file(const CHAR16 *path, UINT64 mode, UINT64 attributes, EFI_FILE_PROTOCOL **file)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_STATUS status;
    if (!gBS || !gBS->LocateProtocol) return 0;
    status = gBS->LocateProtocol(&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, 0, (void **)&fs);
    if (EFI_ERROR(status) || !fs || EFI_ERROR(fs->OpenVolume(fs, &root))) return 0;
    status = root->Open(root, file, path, mode, attributes);
    root->Close(root);
    return !EFI_ERROR(status) && *file;
}

static int terminal_cat_file(const CHAR16 *path)
{
    EFI_FILE_PROTOCOL *file = 0;
    uint8_t bytes[120];
    CHAR16 text[121];
    UINTN count;
    if (!terminal_open_file(path, EFI_FILE_MODE_READ, 0, &file)) return 0;
    do {
        count = sizeof(bytes);
        if (EFI_ERROR(file->Read(file, &count, bytes))) { file->Close(file); return 0; }
        for (UINTN i = 0; i < count; ++i) text[i] = bytes[i];
        text[count] = 0;
        if (count) terminal_write(text);
    } while (count == sizeof(bytes));
    file->Close(file);
    terminal_write_line((const CHAR16[]){0});
    return 1;
}

static int terminal_touch_file(const CHAR16 *path)
{
    EFI_FILE_PROTOCOL *file = 0;
    if (!terminal_open_file(path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_ARCHIVE, &file)) return 0;
    file->Close(file);
    return 1;
}

static int terminal_remove_file(const CHAR16 *path)
{
    EFI_FILE_PROTOCOL *file = 0;
    EFI_STATUS status;
    if (!terminal_open_file(path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &file)) return 0;
    status = file->Delete(file);
    return !EFI_ERROR(status);
}

static int terminal_write_file(const CHAR16 *path, CHAR16 *argv[], int argc, int start, int append)
{
    EFI_FILE_PROTOCOL *file = 0;
    uint8_t bytes[TERMINAL_MAX_LINE];
    UINTN count = 0;
    if (!terminal_open_file(path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_ARCHIVE, &file)) return 0;
    if (append) file->SetPosition(file, (UINT64)-1);
    else if (EFI_ERROR(file->SetPosition(file, 0))) { file->Close(file); return 0; }
    for (int i = start; i < argc && count + 2 < sizeof(bytes); ++i) {
        if (i > start) bytes[count++] = ' ';
        for (int j = 0; argv[i][j] && count + 2 < sizeof(bytes); ++j) bytes[count++] = (uint8_t)argv[i][j];
    }
    bytes[count++] = '\n';
    if (EFI_ERROR(file->Write(file, &count, bytes))) { file->Close(file); return 0; }
    if (file->Flush) file->Flush(file);
    file->Close(file);
    return 1;
}

static int terminal_source_file(const CHAR16 *path)
{
    EFI_FILE_PROTOCOL *file = 0;
    uint8_t bytes[4096];
    UINTN count = sizeof(bytes) - 1;
    CHAR16 line[TERMINAL_MAX_LINE];
    size_t line_length = 0;
    if (!terminal_open_file(path, EFI_FILE_MODE_READ, 0, &file)) return 0;
    if (EFI_ERROR(file->Read(file, &count, bytes))) { file->Close(file); return 0; }
    file->Close(file);
    for (UINTN i = 0; i < count; ++i) {
        if (bytes[i] == '\r') continue;
        if (bytes[i] == '\n') {
            line[line_length] = 0;
            if (line_length) terminal_execute_command(line);
            line_length = 0;
        } else if (line_length + 1 < TERMINAL_MAX_LINE) line[line_length++] = bytes[i];
    }
    if (line_length) { line[line_length] = 0; terminal_execute_command(line); }
    return 1;
}

static int terminal_execute_command(const CHAR16 *command)
{
    const CHAR16 *separator = command;
    CHAR16 *argv[TERMINAL_MAX_ARGS];
    int argc = terminal_parse_command(command, argv, TERMINAL_MAX_ARGS);
    const CHAR16 *args = terminal_command_args(command);

    while (*separator && *separator != '&') ++separator;
    if (*separator == '&') {
        CHAR16 left[TERMINAL_MAX_LINE];
        CHAR16 right[TERMINAL_MAX_LINE];
        terminal_copy_segment(left, command, separator);
        terminal_copy_segment(right, separator + 1, terminal_end(separator + 1));
        terminal_execute_command(left);
        return terminal_execute_command(right);
    }

    if (terminal_try_pipeline(command) >= 0) return 0;

    if (argc == 0) {
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ '@','e','c','h','o',0 })) {
        if (argc > 1 && terminal_match_command(argv[1], (const CHAR16[]){ 'o','f','f',0 })) {
            terminal_echo_enabled = 0;
        } else if (argc > 1 && terminal_match_command(argv[1], (const CHAR16[]){ 'o','n',0 })) {
            terminal_echo_enabled = 1;
        }
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'r','e','m',0 })) {
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 's','w','i','t','c','h','l','a','n','g',0 })) {
        if (argc > 1 && terminal_match_command(argv[1], (const CHAR16[]){ 'b','a','s','h',0 })) {
            terminal_set_mode(TERMINAL_MODE_BASH);
            terminal_write_line((const CHAR16[]){ 's','w','i','t','c','h','e','d',' ','t','o',' ','b','a','s','h',0 });
            return 0;
        }
        if (argc > 1 && terminal_match_command(argv[1], (const CHAR16[]){ 'b','a','t','c','h',0 })) {
            terminal_set_mode(TERMINAL_MODE_BATCH);
            terminal_write_line((const CHAR16[]){ 's','w','i','t','c','h','e','d',' ','t','o',' ','b','a','t','c','h',0 });
            return 0;
        }
        terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','s','w','i','t','c','h','l','a','n','g',' ','b','a','s','h',' ','|',' ','b','a','t','c','h',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'h','e','l','p',0 })) {
        terminal_write_line((const CHAR16[]){
            'a','v','a','i','l','a','b','l','e',' ','c','o','m','m','a','n','d','s',':',
            ' ','h','e','l','p',',',' ','e','c','h','o',',',' ','s','t','a','t','u','s',',',
            ' ','s','y','s','t','e','m',',',' ','v','e','r',',',' ','d','i','r',',',' ','l','s',',',
            ' ','m','e','m',',',' ','c','l','e','a','r',',',' ','c','l','s',',',' ','p','a','u','s','e',',',
            ' ','e','x','i','t',',',' ','s','h','u','t','d','o','w','n',',',' ','r','e','b','o','o','t',',',
            ' ','t','y','p','e',',',' ','s','e','t',',',' ','t','i','m','e',',',' ','d','a','t','e',',',
            ' ','c','d',',',' ','c','o','p','y',',',' ','d','e','l',',',' ','r','e','n',',',' ','m','d',',',' ','r','d',',',
            ' ','p','a','t','h',',',' ','w','h','o','a','m','i',',',' ','p','w','d',',',' ','s','w','i','t','c','h','l','a','n','g',0
        });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'e','c','h','o',0 })) {
        terminal_write_arg_list(argv, argc, 1);
        terminal_write_line((const CHAR16[]){ 0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'n','a','n','o',0 })) {
        terminal_run_nano();
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'a','p','p','s',0 })) {
        terminal_write_line((const CHAR16[]){ 'a','p','p','s',':',' ','n','a','n','o',',',' ','c','a','l','c',',',' ','c','a','t',',',' ','m','e','m','o',',',' ','t','o','d','o',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'c','a','l','c',0 })) {
        terminal_write_line((const CHAR16[]){ 'c','a','l','c','u','l','a','t','o','r',' ','s','t','u','b',' ','r','e','a','d','y',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'c','a','t',0 })) {
        if (argc != 2 || !terminal_cat_file(argv[1])) terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','c','a','t',' ','<','f','i','l','e','>',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 't','o','u','c','h',0 })) {
        if (argc != 2 || !terminal_touch_file(argv[1])) terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','t','o','u','c','h',' ','<','f','i','l','e','>',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'r','m',0 })) {
        if (argc != 2 || !terminal_remove_file(argv[1])) terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','r','m',' ','<','f','i','l','e','>',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'w','r','i','t','e',0 }) || terminal_match_command(argv[0], (const CHAR16[]){ 'a','p','p','e','n','d',0 })) {
        int append = terminal_match_command(argv[0], (const CHAR16[]){ 'a','p','p','e','n','d',0 });
        if (argc < 3 || !terminal_write_file(argv[1], argv, argc, 2, append)) terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','w','r','i','t','e','|','a','p','p','e','n','d',' ','<','f','i','l','e','>',' ','<','t','e','x','t',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 's','o','u','r','c','e',0 })) {
        if (argc != 2 || !terminal_source_file(argv[1])) terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','s','o','u','r','c','e',' ','<','s','c','r','i','p','t','>',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'm','e','m','o',0 })) {
        terminal_write_line((const CHAR16[]){ 'm','e','m','o',' ','a','p','p',' ','r','e','a','d','y',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 't','o','d','o',0 })) {
        terminal_write_line((const CHAR16[]){ 't','o','d','o',':',' ','s','o','m','e',' ','m','o','r','e',' ','j','u','n','k',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 's','t','a','t','u','s',0 })) {
        terminal_write_line((const CHAR16[]){ 'o','k',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 's','y','s','t','e','m',0 })) {
        terminal_write_line(args);
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'v','e','r',0 })) {
        terminal_write_line((const CHAR16[]){ 'F','i','s','i','x',' ','O','S',' ','b','a','t','c','h',' ','t','e','r','m','i','n','a','l',' ','0','.','1',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'd','i','r',0 }) || terminal_match_command(argv[0], (const CHAR16[]){ 'l','s',0 })) {
        if (argc > 1 && terminal_drive_prefix(argv[1], &terminal_current_drive)) {
            terminal_set_drive(terminal_current_drive);
        }

        terminal_show_drive_root(terminal_current_drive);
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'm','e','m',0 })) {
        terminal_write_line((const CHAR16[]){ 'm','e','m','o','r','y',' ','m','a','n','a','g','e','r',' ','r','e','a','d','y',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'c','l','e','a','r',0 }) || terminal_match_command(argv[0], (const CHAR16[]){ 'c','l','s',0 })) {
        terminal_write_line((const CHAR16[]){ ' ',0 });
        terminal_write_line((const CHAR16[]){ ' ',0 });
        terminal_write_line((const CHAR16[]){ ' ',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'p','a','u','s','e',0 })) {
        terminal_write_line((const CHAR16[]){ 'p','r','e','s','s',' ','a','n','y',' ','k','e','y',' ','t','o',' ','c','o','n','t','i','n','u','e',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'e','x','i','t',0 }) || terminal_match_command(argv[0], (const CHAR16[]){ 's','h','u','t','d','o','w','n',0 })) {
        terminal_write_line((const CHAR16[]){ 's','h','u','t','d','o','w','n',' ','c','o','m','m','a','n','d',' ','r','e','c','e','i','v','e','d',0 });
        return 1;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'r','e','b','o','o','t',0 })) {
        terminal_write_line((const CHAR16[]){ 'r','e','b','o','o','t',' ','c','o','m','m','a','n','d',' ','r','e','c','e','i','v','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 't','y','p','e',0 })) {
        terminal_write_line((const CHAR16[]){ 'b','a','t',' ','s','c','r','i','p','t',' ','e','x','e','c','u','t','i','o','n',' ','s','u','p','p','o','r','t',' ','a','c','t','i','v','e',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 's','e','t',0 })) {
        terminal_write_line((const CHAR16[]){ 'e','n','v','i','r','o','n','m','e','n','t',' ','v','a','r','i','a','b','l','e','s',' ','s','e','t',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 't','i','m','e',0 })) {
        terminal_write_line((const CHAR16[]){ '0','0',':','0','0',':','0','0',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'd','a','t','e',0 })) {
        terminal_write_line((const CHAR16[]){ '0','6','/','0','8','/','2','0','2','6',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'c','d',0 })) {
        if (argc > 1 && terminal_drive_prefix(argv[1], &terminal_current_drive)) {
            terminal_set_drive(terminal_current_drive);
            terminal_write_line((const CHAR16[]){ 'd','r','i','v','e',' ','s','e','t',' ','t','o',0 });
            terminal_write_line(terminal_current_path);
            return 0;
        }

        terminal_write_line(terminal_current_path);
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'c','o','p','y',0 })) {
        terminal_write_line((const CHAR16[]){ 'c','o','p','y',' ','o','p','e','r','a','t','i','o','n',' ','s','u','p','p','o','r','t','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'd','e','l',0 })) {
        terminal_write_line((const CHAR16[]){ 'd','e','l','e','t','e',' ','o','p','e','r','a','t','i','o','n',' ','s','u','p','p','o','r','t','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'r','e','n',0 })) {
        terminal_write_line((const CHAR16[]){ 'r','e','n','a','m','e',' ','o','p','e','r','a','t','i','o','n',' ','s','u','p','p','o','r','t','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'm','d',0 })) {
        terminal_write_line((const CHAR16[]){ 'd','i','r','e','c','t','o','r','y',' ','c','r','e','a','t','i','o','n',' ','s','u','p','p','o','r','t','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'r','d',0 })) {
        terminal_write_line((const CHAR16[]){ 'd','i','r','e','c','t','o','r','y',' ','r','e','m','o','v','a','l',' ','s','u','p','p','o','r','t','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'p','a','t','h',0 })) {
        terminal_write_line((const CHAR16[]){ 'p','a','t','h',' ','s','e','t',' ','t','o',' ','F','i','s','i','x',' ','O','S',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'w','h','o','a','m','i',0 })) {
        terminal_write_line(accounts_current_user());
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'g','r','e','p',0 })) {
        if (argc < 3) {
            terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','g','r','e','p',' ','<','p','a','t','t','e','r','n','>',' ','<','t','e','x','t','>',0 });
            return 0;
        }
        for (int i = 2; i < argc; ++i) if (terminal_contains(argv[i], argv[1])) terminal_write_line(argv[i]);
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'u','s','e','r','s',0 })) {
        for (int i = 0; i < accounts_count(); ++i) terminal_write_line(accounts_user_at(i));
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'u','s','e','r','a','d','d',0 })) {
        if (argc == 3 && accounts_add(argv[1], argv[2])) terminal_write_line((const CHAR16[]){ 'u','s','e','r',' ','c','r','e','a','t','e','d',0 });
        else terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','u','s','e','r','a','d','d',' ','<','n','a','m','e','>',' ','<','p','a','s','s','w','o','r','d','>',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'p','a','s','s','w','d',0 })) {
        CHAR16 old_password[32];
        CHAR16 new_password[32];
        terminal_write((const CHAR16[]){ 'c','u','r','r','e','n','t',' ','p','a','s','s','w','o','r','d',':',' ',0 });
        terminal_echo_enabled = 0;
        terminal_read_line(old_password, sizeof(old_password) / sizeof(old_password[0]));
        terminal_echo_enabled = 1;
        terminal_write((const CHAR16[]){ 'n','e','w',' ','p','a','s','s','w','o','r','d',':',' ',0 });
        terminal_echo_enabled = 0;
        terminal_read_line(new_password, sizeof(new_password) / sizeof(new_password[0]));
        terminal_echo_enabled = 1;
        terminal_write_line(accounts_change_password(old_password, new_password)
            ? (const CHAR16[]){ 'p','a','s','s','w','o','r','d',' ','u','p','d','a','t','e','d',0 }
            : (const CHAR16[]){ 'p','a','s','s','w','o','r','d',' ','n','o','t',' ','c','h','a','n','g','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'l','o','g','i','n',0 })) {
        terminal_login();
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'p','w','d',0 })) {
        terminal_write_line((const CHAR16[]){ '/',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'b','a','s','h',0 })) {
        terminal_set_mode(TERMINAL_MODE_BASH);
        terminal_write_line((const CHAR16[]){ 'b','a','s','h',' ','m','o','d','e',' ','e','n','a','b','l','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'b','a','t','c','h',0 })) {
        terminal_set_mode(TERMINAL_MODE_BATCH);
        terminal_write_line((const CHAR16[]){ 'b','a','t','c','h',' ','m','o','d','e',' ','e','n','a','b','l','e','d',0 });
        return 0;
    }

    terminal_write_line((const CHAR16[]){ 'u','n','k','n','o','w','n',' ','c','o','m','m','a','n','d',0 });
    return 0;
}

static void terminal_run_batch_file(const CHAR16 *batch_data)
{
    const CHAR16 *line_start = batch_data;

    while (*line_start) {
        const CHAR16 *line_end = line_start;
        CHAR16 line[128];
        size_t offset = 0;

        while (*line_end && *line_end != '\r' && *line_end != '\n' && offset < (sizeof(line) - 1)) {
            line[offset++] = *line_end++;
        }

        line[offset] = 0;

        if (offset > 0) {
            if (terminal_echo_enabled) {
                terminal_write(terminal_current_prompt());
            }
            terminal_execute_command(line);
        }

        while (*line_end == '\r' || *line_end == '\n') {
            line_end++;
        }

        line_start = line_end;
    }
}

static int terminal_read_line(CHAR16 *line, size_t max_length)
{
    size_t length = 0;
    EFI_INPUT_KEY key;

    while (gST && gST->ConIn && gST->ConIn->ReadKeyStroke) {
        gST->ConIn->ReadKeyStroke(gST->ConIn, &key);

        if (key.UnicodeChar == 0) {
            continue;
        }

        if (key.UnicodeChar == '\r') {
            terminal_write((const CHAR16[]){ '\r', '\n', 0 });
            line[length] = 0;
            return (int)length;
        }

        if (key.UnicodeChar == '\b') {
            if (length > 0) {
                length--;
                line[length] = 0;
                terminal_write((const CHAR16[]){ '\b', ' ', '\b', 0 });
            }
            continue;
        }

        if (length + 1 >= max_length) {
            continue;
        }

        if (terminal_echo_enabled) {
            terminal_write((const CHAR16[]){ key.UnicodeChar, 0 });
        }

        line[length++] = key.UnicodeChar;
    }

    return 0;
}

static void terminal_run_nano(void)
{
    int line_count = 0;

    terminal_write_line((const CHAR16[]){ 'n','a','n','o','-','l','i','k','e',' ','e','d','i','t','o','r',' ','e','n','t','e','r','e','d',0 });
    terminal_write_line((const CHAR16[]){ 't','y','p','e',' ','l','i','n','e','s',',',' ','s','a','v','e',',',' ','o','r',' ','e','x','i','t',0 });

    for (;;) {
        CHAR16 line[TERMINAL_MAX_LINE];
        size_t length;
        terminal_write((const CHAR16[]){ 'n','a','n','o','>',' ',0 });
        length = (size_t)terminal_read_line(line, sizeof(line) / sizeof(line[0]));

        if (length == 0) {
            continue;
        }

        if (terminal_match_command(line, (const CHAR16[]){ 'e','x','i','t',0 }) || terminal_match_command(line, (const CHAR16[]){ 'q','u','i','t',0 })) {
            terminal_write_line((const CHAR16[]){ 'e','x','i','t','i','n','g',' ','n','a','n','o',0 });
            return;
        }

        if (terminal_match_command(line, (const CHAR16[]){ 's','a','v','e',0 })) {
            terminal_write_line((const CHAR16[]){ 's','a','v','e','d',' ','n','a','n','o',' ','b','u','f','f','e','r',0 });
            return;
        }

        if (line_count >= TERMINAL_MAX_EDITOR_LINES) {
            terminal_write_line((const CHAR16[]){ 'e','d','i','t','o','r',' ','b','u','f','f','e','r',' ','f','u','l','l',0 });
            continue;
        }

        line_count++;
        terminal_write_line((const CHAR16[]){ 'l','i','n','e',' ','a','d','d','e','d',0 });
    }
}

static void terminal_run_shell(void)
{
    CHAR16 line[TERMINAL_MAX_LINE];

    terminal_write_line((const CHAR16[]){ 'F','i','s','i','x',' ','O','S',' ','s','h','e','l','l',' ','r','e','a','d','y',0 });

    for (;;) {
        size_t length;

        terminal_write(terminal_current_prompt());
        length = (size_t)terminal_read_line(line, sizeof(line) / sizeof(line[0]));

        if (length == 0) {
            continue;
        }

        if (terminal_execute_command(line)) {
            break;
        }
    }
}

static void terminal_run_batch(void)
{
    terminal_write_line((const CHAR16[]){ 'F','i','s','i','x',' ','O','S',' ','b','a','t','c','h',' ','t','e','r','m','i','n','a','l',' ','r','e','a','d','y',0 });
    terminal_run_batch_file(autoexec_batch);
}

/* Save the setup choice (not any Wi-Fi password) to \fisix_setup.txt,
   so a later boot can tell setup already ran even if the user store
   is checked separately. Best effort: a failed record write does not
   block login, because the saved user account is the real gate. */
static void terminal_save_setup_record(const char *network, const CHAR16 *ssid)
{
    EFI_FILE_PROTOCOL *file = 0;
    char record[192];
    size_t i = 0, j = 0;
    UINTN count;
    const char *p;

    for (p = "fisix_setup=done\nnetwork="; *p && i + 1 < sizeof(record); p++) record[i++] = *p;
    for (p = network; *p && i + 1 < sizeof(record); p++) record[i++] = *p;
    if (ssid && ssid[0]) {
        for (p = "\nssid="; *p && i + 1 < sizeof(record); p++) record[i++] = *p;
        while (ssid[j] && i + 1 < sizeof(record)) record[i++] = (char)(uint8_t)ssid[j++];
    }
    if (i + 1 < sizeof(record)) record[i++] = '\n';
    record[i] = 0;

    if (!terminal_open_file((const CHAR16[]){ '\\', 'f', 'i', 's', 'i', 'x', '_', 's', 'e', 't', 'u', 'p', '.', 't', 'x', 't', 0 },
                            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_ARCHIVE, &file))
        return;
    if (file->SetPosition && EFI_ERROR(file->SetPosition(file, 0))) { file->Close(file); return; }
    count = i;
    if (!EFI_ERROR(file->Write(file, &count, record)) && file->Flush) file->Flush(file);
    file->Close(file);
}

/* First-time setup. Runs only on a first boot (no saved user store),
   before the normal login screen. Steps: create the first admin
   account, then pick an internet option. There is no network driver
   in FisixOS yet, so the internet step records the choice for later
   and continues offline instead of pretending to connect. The Wi-Fi
   password, if typed, is never written to disk. */
static void terminal_first_time_setup(void)
{
    CHAR16 username[32];
    CHAR16 password[32];
    CHAR16 confirm[32];
    CHAR16 ssid[32];
    CHAR16 wifi_password[32];
    CHAR16 choice[8];
    const char *network = "skip";
    int i;

    serial_mark("FIRST_SETUP_START");
    terminal_write_ascii_line("========================================");
    terminal_write_ascii_line("Welcome to FisixOS - First-Time Setup");
    terminal_write_ascii_line("========================================");
    terminal_write_ascii_line("This only runs once, on the first boot.");
    terminal_write_ascii_line("After setup, log in with the account you create here.");
    terminal_write_ascii_line("");

    for (;;) {
        terminal_write_ascii("Choose a username: ");
        if (terminal_read_line(username, sizeof(username) / sizeof(username[0])) == 0) continue;
        if (terminal_has_space(username)) {
            terminal_write_ascii_line("Usernames cannot contain spaces. Try again.");
            continue;
        }
        for (;;) {
            terminal_write_ascii("Choose a password: ");
            terminal_echo_enabled = 0;
            terminal_read_line(password, sizeof(password) / sizeof(password[0]));
            terminal_echo_enabled = 1;
            if (password[0] == 0) continue;
            terminal_write_ascii("Type the password again: ");
            terminal_echo_enabled = 0;
            terminal_read_line(confirm, sizeof(confirm) / sizeof(confirm[0]));
            terminal_echo_enabled = 1;
            if (!terminal_equal_str(password, confirm)) {
                terminal_write_ascii_line("Passwords did not match. Try again.");
                continue;
            }
            break;
        }
        if (accounts_setup_first_user(username, password)) break;
        terminal_write_ascii_line("Could not save that account. Try again.");
    }
    serial_mark("FIRST_SETUP_ACCOUNT_OK");

    terminal_write_ascii_line("");
    terminal_write_ascii_line("Internet setup:");
    terminal_write_ascii_line("  1 = Wired (Ethernet)");
    terminal_write_ascii_line("  2 = Wi-Fi");
    terminal_write_ascii_line("  3 = Skip for now");
    terminal_write_ascii("Choose 1, 2 or 3: ");
    terminal_read_line(choice, sizeof(choice) / sizeof(choice[0]));
    ssid[0] = 0;
    if (choice[0] == '1') {
        network = "wired";
        serial_mark("FIRST_SETUP_NET_WIRED");
        terminal_write_ascii_line("Wired selected.");
    } else if (choice[0] == '2') {
        network = "wifi";
        serial_mark("FIRST_SETUP_NET_WIFI");
        terminal_write_ascii("Wi-Fi network name (SSID): ");
        terminal_read_line(ssid, sizeof(ssid) / sizeof(ssid[0]));
        terminal_write_ascii("Wi-Fi password: ");
        terminal_echo_enabled = 0;
        terminal_read_line(wifi_password, sizeof(wifi_password) / sizeof(wifi_password[0]));
        terminal_echo_enabled = 1;
        /* Deliberately never saved: clear it as soon as it is read. */
        for (i = 0; i < 32; i++) wifi_password[i] = 0;
        terminal_write_ascii_line("Wi-Fi selected.");
    } else {
        network = "skip";
        serial_mark("FIRST_SETUP_NET_SKIP");
        terminal_write_ascii_line("Skipping internet setup. You can set it up later.");
    }

    terminal_write_ascii_line("Note: FisixOS does not have a network driver yet,");
    terminal_write_ascii_line("so this choice is saved for later and the system");
    terminal_write_ascii_line("will continue offline for now. No explosion today.");
    terminal_save_setup_record(network, ssid);

    terminal_write_ascii_line("");
    terminal_write_ascii_line("Setup complete!");
    terminal_write_ascii("Welcome to FisixOS, ");
    terminal_write(username);
    terminal_write_ascii_line("");
    terminal_write_ascii_line("Please log in with your new account.");
    serial_mark("FIRST_SETUP_DONE");
}

static int terminal_login(void)
{
    CHAR16 username[32];
    CHAR16 password[32];

    for (;;) {
        terminal_write((const CHAR16[]){ 'l','o','g','i','n',':',' ',0 });
        if (terminal_read_line(username, sizeof(username) / sizeof(username[0])) == 0) continue;
        terminal_write((const CHAR16[]){ 'p','a','s','s','w','o','r','d',':',' ',0 });
        terminal_echo_enabled = 0;
        terminal_read_line(password, sizeof(password) / sizeof(password[0]));
        terminal_echo_enabled = 1;
        if (accounts_login(username, password)) return 1;
        terminal_write_line((const CHAR16[]){ 'i','n','v','a','l','i','d',' ','c','r','e','d','e','n','t','i','a','l','s',0 });
    }
}

typedef void (*kernel_entry_t)(const kernel_boot_info_t *boot_info);

#define KERNEL_LOAD_ADDRESS 0x200000ULL
#define KERNEL_MAX_BYTES    (1024ULL * 1024ULL)

static EFI_STATUS efi_load_kernel(void)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_FILE_PROTOCOL *kernel = 0;
    EFI_ALLOCATE_PAGES allocate_pages;
    uint64_t address = KERNEL_LOAD_ADDRESS;
    UINTN bytes = KERNEL_MAX_BYTES;
    EFI_STATUS status;

    if (!gBS || !gBS->LocateProtocol || !gBS->AllocatePages) return EFI_LOAD_ERROR;
    status = gBS->LocateProtocol(&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, 0, (void **)&fs);
    if (EFI_ERROR(status) || !fs) return status;
    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status) || !root) return status;
    status = root->Open(root, &kernel, (const CHAR16[]){'\\','k','e','r','n','e','l','.','b','i','n',0}, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || !kernel) { root->Close(root); return status; }

    allocate_pages = (EFI_ALLOCATE_PAGES)gBS->AllocatePages;
    status = allocate_pages(AllocateAddress, EfiLoaderData, KERNEL_MAX_BYTES / 4096, &address);
    if (EFI_ERROR(status)) {
        address = 0;
        status = allocate_pages(AllocateAnyPages, EfiLoaderData, KERNEL_MAX_BYTES / 4096, &address);
    }
    if (!EFI_ERROR(status)) {
        uint8_t *memory = (uint8_t *)(uintptr_t)KERNEL_LOAD_ADDRESS;
        for (UINTN i = 0; i < KERNEL_MAX_BYTES; ++i) memory[i] = 0;
        status = kernel->Read(kernel, &bytes, memory);
        if (bytes == 0) status = EFI_LOAD_ERROR;
    }
    kernel->Close(kernel);
    root->Close(root);
    return status;
}

/* This is the only code allowed to use Boot Services during the handoff.
   The map key is valid only for the exact map returned immediately before
   ExitBootServices(), so do not print or allocate between those calls. */
static EFI_STATUS efi_leave_boot_services(kernel_boot_info_t *boot_info)
{
    static uint8_t memory_map_buffer[128 * 1024];
    EFI_GET_MEMORY_MAP get_memory_map;
    EFI_EXIT_BOOT_SERVICES exit_boot_services;
    size_t memory_map_size;
    size_t map_key;
    size_t descriptor_size;
    uint32_t descriptor_version;
    EFI_STATUS status;
    uint64_t total_memory = 0;
    uint8_t *cursor;

    if (!gBS || !gBS->GetMemoryMap || !gBS->ExitBootServices) {
        return EFI_LOAD_ERROR;
    }

    get_memory_map = (EFI_GET_MEMORY_MAP)gBS->GetMemoryMap;
    exit_boot_services = (EFI_EXIT_BOOT_SERVICES)gBS->ExitBootServices;

    for (;;) {
        memory_map_size = sizeof(memory_map_buffer);
        map_key = 0;
        descriptor_size = 0;
        descriptor_version = 0;
        status = get_memory_map(&memory_map_size,
                                (EFI_MEMORY_DESCRIPTOR *)memory_map_buffer,
                                &map_key, &descriptor_size,
                                &descriptor_version);
        if (EFI_ERROR(status) || descriptor_size == 0) {
            return status;
        }

        total_memory = 0;
        cursor = memory_map_buffer;
        for (size_t i = 0; i < memory_map_size / descriptor_size; ++i) {
            EFI_MEMORY_DESCRIPTOR *descriptor = (EFI_MEMORY_DESCRIPTOR *)cursor;
            if (descriptor->Type == EfiConventionalMemory ||
                descriptor->Type == EfiLoaderCode ||
                descriptor->Type == EfiLoaderData ||
                descriptor->Type == EfiBootServicesCode ||
                descriptor->Type == EfiBootServicesData) {
                total_memory += descriptor->NumberOfPages * 4096ULL;
            }
            cursor += descriptor_size;
        }

        status = exit_boot_services(gImageHandle, map_key);
        if (status == EFI_SUCCESS) {
            boot_info->memory_bytes = total_memory;
            gBS = NULL;
            return EFI_SUCCESS;
        }
        if (status != EFI_INVALID_PARAMETER) {
            return status;
        }
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    serial_mark("EFI_MAIN_START");
    gImageHandle = ImageHandle;
    gST = SystemTable;
    gBS = SystemTable ? SystemTable->BootServices : NULL;
    serial_mark("EFI_MAIN_GBS_SET");

    eosfs_init();
    serial_mark("EFI_MAIN_EOSFS_OK");
    accounts_init();
    serial_mark("EFI_MAIN_ACCOUNTS_OK");

    serial_mark("EFI_MAIN_BEFORE_BOOTMSG");
    terminal_write(boot_message);
    serial_mark("EFI_MAIN_AFTER_BOOTMSG");
    if (accounts_first_boot()) {
        terminal_first_time_setup();
    }
    terminal_run_batch();
    terminal_login();
    terminal_run_shell();

    kernel_boot_info_t boot_info = { 0, 0 };
    if (efi_load_kernel() != EFI_SUCCESS || efi_leave_boot_services(&boot_info) != EFI_SUCCESS) {
        terminal_write_line((const CHAR16[]){ 'f','a','i','l','e','d',' ','t','o',' ','s','t','a','r','t',' ','k','e','r','n','e','l',0 });
        return EFI_LOAD_ERROR;
    }

    ((kernel_entry_t)(uintptr_t)KERNEL_LOAD_ADDRESS)(&boot_info);
    return EFI_SUCCESS; /* kernel_startup does not return */
}
