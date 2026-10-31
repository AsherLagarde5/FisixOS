#include "accounts.h"

/* Restored from the pre-patch version: the "safer accounts" patch kept
   only the file helpers and dropped the include, the account types,
   the globals, and every public function, so the EFI loader could no
   longer link at all. The helpers below are the safer versions. */
#define ACCOUNT_TEXT_MAX 31
#define ACCOUNT_STORE_MAGIC 0x46555352u

typedef struct {
    CHAR16 name[ACCOUNT_TEXT_MAX + 1];
    CHAR16 password[ACCOUNT_TEXT_MAX + 1];
    int admin;
} account_t;

typedef struct {
    uint32_t magic;
    uint32_t count;
    account_t entries[FISIX_MAX_USERS];
} account_store_t;

static account_t accounts[FISIX_MAX_USERS];
static int account_count;
static int current_account = -1;
/* Set only when a valid saved user store was found on disk. Until then
   this is a first boot: no real user exists yet, only the in-memory
   fallback, so the loader must run first-time setup instead of the
   normal login screen. */
static int accounts_saved_store = 0;
/* Set by the bootloader menu in efi_main.c. NULL = first FS found. */
EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fisix_selected_fs = 0;
int fisix_live_boot = 0;
static const CHAR16 nobody[] = {'n', 'o', 'b', 'o', 'd', 'y', 0};
static void acct_outb(uint16_t port, uint8_t v) { __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port)); }
static uint8_t acct_inb(uint16_t port) { uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static void acct_hex(uint64_t v) {
    const char *hex = "0123456789ABCDEF";
    acct_outb(0x3F8, (uint8_t)'0'); acct_outb(0x3F8, (uint8_t)'x');
    for (int i = 15; i >= 0; i--) { uint32_t tt = 100000; while ((acct_inb(0x3F8 + 5) & 0x20) == 0 && tt-- > 0) { } acct_outb(0x3F8, (uint8_t)hex[(v >> (i * 4)) & 0xF]); }
    acct_outb(0x3F8, (uint8_t)'\n');
}
static void acct_mark(const char *s) {
    acct_outb(0x3F8 + 1, 0x00); acct_outb(0x3F8 + 3, 0x80); acct_outb(0x3F8 + 0, 0x03); acct_outb(0x3F8 + 1, 0x00); acct_outb(0x3F8 + 3, 0x03); acct_outb(0x3F8 + 2, 0xC7); acct_outb(0x3F8 + 4, 0x0B);
    while (s && *s) { uint32_t t = 100000; while ((acct_inb(0x3F8 + 5) & 0x20) == 0 && t-- > 0) { } acct_outb(0x3F8, (uint8_t)*s); s++; }
    acct_outb(0x3F8, (uint8_t)'\n');
}

/* --- PATCH: safer accounts_open() --- */
static int accounts_open(EFI_FILE_PROTOCOL **file, UINT64 mode)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_STATUS status;

    if (!file || !gBS || !gBS->LocateProtocol)
        return 0;

    if (fisix_selected_fs) {
        fs = fisix_selected_fs;
    } else {
        acct_mark("ACCT_LOCATE_CALL");
        status = gBS->LocateProtocol(&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, 0, (void **)&fs);
        acct_mark("ACCT_LOCATE_RET");
        if (EFI_ERROR(status) || !fs)
            return 0;
    }

    acct_mark("ACCT_VOLUME_CALL");
    status = fs->OpenVolume(fs, &root);
    acct_mark("ACCT_VOLUME_RET");
    if (EFI_ERROR(status) || !root)
        return 0;

    /* Use a static filename to avoid temp array lifetime issues */
    static const CHAR16 filename[] = L"\\fisix_users.db";

    acct_mark("ACCT_ROOT_PTR"); acct_hex((uint64_t)root); acct_mark("ACCT_OPEN_PTR"); acct_hex(root ? *(uint64_t *)root : 0); acct_mark("ACCT_FILE_OPEN_CALL");
    status = root->Open(root, file, filename, mode, 0);
    acct_mark("ACCT_FILE_OPEN_RET");
    root->Close(root);

    return (!EFI_ERROR(status) && *file);
}

/* --- PATCH: safer accounts_save() --- */
static int accounts_save(void)
{
    /* Liveboot: everything in RAM. Keep the in-memory account, do not
       touch any disk, but report success so setup does not loop asking
       for session-only confirmation. */
    if (fisix_live_boot) return 1;
    /* Never write a store with an out-of-range count: the fixed-size
       entries array below would overflow. */
    if (account_count < 0 || account_count > FISIX_MAX_USERS)
        return 0;
    EFI_FILE_PROTOCOL *file = 0;
    account_store_t store;
    UINTN bytes = sizeof(store);

    if (!accounts_open(&file, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE))
        return 0;

    store.magic = ACCOUNT_STORE_MAGIC;
    store.count = (uint32_t)account_count;

    /* Copy only the live accounts, then zero unused entries so no
       stale account data is ever written to disk. */
    for (int i = 0; i < account_count; ++i) {
        store.entries[i] = accounts[i];
    }
    for (int i = account_count; i < FISIX_MAX_USERS; ++i) {
        store.entries[i].name[0] = 0;
        store.entries[i].password[0] = 0;
        store.entries[i].admin = 0;
    }

    /* Rewind before writing; the store size is fixed, so this overwrites it. */
    if (file->SetPosition && EFI_ERROR(file->SetPosition(file, 0))) {
        file->Close(file);
        return 0;
    }

    if (EFI_ERROR(file->Write(file, &bytes, &store)) || bytes != sizeof(store)) {
        file->Close(file);
        return 0;
    }

    if (file->Flush)
        file->Flush(file);

    file->Close(file);
    return 1;
}

/* --- PATCH: safer accounts_load() --- */
static void accounts_load(void)
{
    EFI_FILE_PROTOCOL *file = 0;
    account_store_t store;
    UINTN bytes = sizeof(store);

    if (!accounts_open(&file, EFI_FILE_MODE_READ))
        return;

    /* Ensure full read */
    if (!EFI_ERROR(file->Read(file, &bytes, &store)) &&
        bytes == sizeof(store) &&
        store.magic == ACCOUNT_STORE_MAGIC &&
        store.count > 0 &&
        store.count <= FISIX_MAX_USERS)
    {
        accounts_saved_store = 1;
        account_count = (int)store.count;

        /* Copy only valid entries */
        for (int i = 0; i < account_count; ++i)
            accounts[i] = store.entries[i];

        /* Zero unused entries */
        for (int i = account_count; i < FISIX_MAX_USERS; ++i) {
            accounts[i].name[0] = 0;
            accounts[i].password[0] = 0;
            accounts[i].admin = 0;
        }
    }

    file->Close(file);
}

static int equal_str(const CHAR16 *a, const CHAR16 *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int valid_str(const CHAR16 *s)
{
    int n = 0;
    if (!s)
        return 0;
    while (*s) {
        if (*s == ' ' || ++n > ACCOUNT_TEXT_MAX)
            return 0;
        ++s;
    }
    return n != 0;
}

static void copy_str(CHAR16 *to, const CHAR16 *from)
{
    int i = 0;
    if (!to || !from)
        return;
    while (from[i] && i < ACCOUNT_TEXT_MAX) {
        to[i] = from[i];
        ++i;
    }
    to[i] = 0;
}

int accounts_first_boot(void)
{
    return !accounts_saved_store;
}

/* First-time setup only: replace the in-memory fallback account with the
   user's own admin account and save it. This deliberately does NOT
   require an existing admin login (there is none yet on a first boot),
   unlike accounts_add(). Refuses to run once a saved store exists, so
   setup can never overwrite an existing user on a later boot. */
int accounts_setup_first_user(const CHAR16 *name, const CHAR16 *password)
{
    if (accounts_saved_store || !valid_str(name) || !valid_str(password))
        return 0;
    copy_str(accounts[0].name, name);
    copy_str(accounts[0].password, password);
    accounts[0].admin = 1;
    account_count = 1;
    current_account = -1;
    if (!accounts_save())
        return 0;
    accounts_saved_store = 1;
    return 1;
}

void accounts_init(void)
{
    account_count = 1;
    accounts_saved_store = 0;
    current_account = -1;
    copy_str(accounts[0].name, (const CHAR16[]){'a', 'd', 'm', 'i', 'n', 0});
    copy_str(accounts[0].password, (const CHAR16[]){'f', 'i', 's', 'i', 'x', 0});
    accounts[0].admin = 1;
    accounts_load();
}

int accounts_login(const CHAR16 *name, const CHAR16 *password)
{
    for (int i = 0; i < account_count; ++i) {
        if (equal_str(accounts[i].name, name) && equal_str(accounts[i].password, password)) {
            current_account = i;
            return 1;
        }
    }
    return 0;
}

int accounts_add(const CHAR16 *name, const CHAR16 *password)
{
    /* Lower bound too: a negative count would index before the array. */
    if (!accounts_is_admin() || !valid_str(name) || !valid_str(password) || account_count < 0 || account_count >= FISIX_MAX_USERS)
        return 0;
    for (int i = 0; i < account_count; ++i) {
        if (equal_str(accounts[i].name, name))
            return 0;
    }
    copy_str(accounts[account_count].name, name);
    copy_str(accounts[account_count].password, password);
    accounts[account_count].admin = 0;
    account_count++;
    return accounts_save();
}

int accounts_change_password(const CHAR16 *old_password, const CHAR16 *new_password)
{
    /* current_account is only ever a login index, but never index past
       the live accounts: a stale index must not read out of bounds. */
    if (current_account < 0 || current_account >= account_count || !equal_str(accounts[current_account].password, old_password) || !valid_str(new_password))
        return 0;
    copy_str(accounts[current_account].password, new_password);
    return accounts_save();
}

int accounts_is_admin(void)
{
    /* Same stale-index guard as accounts_change_password(). */
    return current_account >= 0 && current_account < account_count && accounts[current_account].admin;
}

const CHAR16 *accounts_current_user(void)
{
    /* Same stale-index guard: a stale index falls back to "nobody". */
    return current_account >= 0 && current_account < account_count ? accounts[current_account].name : nobody;
}

int accounts_count(void)
{
    return account_count;
}

const CHAR16 *accounts_user_at(int index)
{
    return index >= 0 && index < account_count ? accounts[index].name : 0;
}
