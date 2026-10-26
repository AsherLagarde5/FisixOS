#include "eosfs.h"

static eosfs_entry_t g_entries[EOSFS_MAX_ENTRIES];
static eosfs_partition_t g_partitions[4];
static eosfs_gpt_header_t g_gpt_header;
static int g_entry_count = 0;
static int g_partition_count = 0;

static int eosfs_read_disk_entries(const CHAR16 *path, eosfs_entry_t *entries, int max_entries, int *count)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_STATUS status;
    UINTN buffer_size;
    unsigned char buffer[256];
    EFI_FILE_INFO *info;
    int index = 0;
    (void)path;

    if (gBS == 0 || entries == 0 || count == 0 || max_entries <= 0) {
        return 0;
    }

    if (fisix_selected_fs) {
        fs = fisix_selected_fs;
    } else {
        status = gBS->LocateProtocol(&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, 0, (void **)&fs);
        if (EFI_ERROR(status) || fs == 0) {
            return 0;
        }
    }

    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status) || root == 0) {
        return 0;
    }

    while (index < max_entries) {
        buffer_size = sizeof(buffer);
        status = root->Read(root, &buffer_size, buffer);
        if (EFI_ERROR(status) || buffer_size == 0) {
            break;
        }

        info = (EFI_FILE_INFO *)buffer;
        if (info->FileName[0] == 0) {
            break;
        }

        {
            int i = 0;
            while (info->FileName[i] && i < 15) {
                entries[index].name[i] = info->FileName[i];
                i++;
            }
            entries[index].name[i] = 0;
            entries[index].type = (info->Attribute & EFI_FILE_DIRECTORY) ? EOSFS_ENTRY_DIR : EOSFS_ENTRY_FILE;
        }

        index++;
    }

    if (index == 0) {
        root->Close(root);
        return 0;
    }

    *count = index;
    root->Close(root);
    return 1;
}

static void eosfs_add_entry(const CHAR16 *name, eosfs_entry_type_t type)
{
    int i = 0;

    if (g_entry_count >= EOSFS_MAX_ENTRIES) {
        return;
    }

    while (name[i] && i < 15) {
        g_entries[g_entry_count].name[i] = name[i];
        i++;
    }
    g_entries[g_entry_count].name[i] = 0;
    g_entries[g_entry_count].type = type;
    g_entry_count++;
}

static void eosfs_add_partition(const CHAR16 *name, uint64_t start, uint64_t end)
{
    int i = 0;

    if (g_partition_count >= 4) {
        return;
    }

    while (name[i] && i < 35) {
        g_partitions[g_partition_count].name[i] = name[i];
        i++;
    }
    g_partitions[g_partition_count].name[i] = 0;
    g_partitions[g_partition_count].first_lba = start;
    g_partitions[g_partition_count].last_lba = end;
    g_partition_count++;
}

void eosfs_init(void)
{
    g_entry_count = 0;
    g_partition_count = 0;

    g_gpt_header.signature = EOSFS_GPT_SIGNATURE;
    g_gpt_header.revision = 0x00010000u;
    g_gpt_header.header_size = sizeof(eosfs_gpt_header_t);
    g_gpt_header.current_lba = 1;
    g_gpt_header.backup_lba = 0;
    g_gpt_header.first_usable_lba = 34;
    g_gpt_header.last_usable_lba = 0;
    g_gpt_header.partition_entry_lba = 2;
    g_gpt_header.partition_entry_count = 4;
    g_gpt_header.partition_entry_size = sizeof(eosfs_partition_t);
    g_gpt_header.partition_crc32 = 0;

    eosfs_add_entry((const CHAR16[]){ 'b','o','o','t',0 }, EOSFS_ENTRY_DIR);
    eosfs_add_entry((const CHAR16[]){ 'd','r','i','v','e','r','s',0 }, EOSFS_ENTRY_DIR);
    eosfs_add_entry((const CHAR16[]){ 'k','e','r','n','e','l',0 }, EOSFS_ENTRY_DIR);
    eosfs_add_entry((const CHAR16[]){ 'm','n','t',0 }, EOSFS_ENTRY_DIR);
    eosfs_add_entry((const CHAR16[]){ 'e','x','i','s','t','e','n','t','o','s','.','t','x','t',0 }, EOSFS_ENTRY_FILE);

    eosfs_add_partition((const CHAR16[]){ 'B',':','/',' ','E','O','S','F','S',0 }, 34, 2048);
    eosfs_add_partition((const CHAR16[]){ 'C',':','/',' ','E','O','S','F','S',0 }, 2049, 4096);
}

int eosfs_list_path(const CHAR16 *path, eosfs_entry_t *entries, int max_entries, int *count)
{
    int i;

    if (entries == 0 || count == 0 || max_entries <= 0) {
        return 0;
    }

    if (eosfs_read_disk_entries(path, entries, max_entries, count)) {
        return 1;
    }

    *count = g_entry_count;
    if (*count > max_entries) {
        *count = max_entries;
    }

    for (i = 0; i < *count; ++i) {
        entries[i] = g_entries[i];
    }

    return 1;
}

int eosfs_get_gpt_header(eosfs_gpt_header_t *header)
{
    if (header == 0) {
        return 0;
    }

    *header = g_gpt_header;
    return 1;
}

int eosfs_list_partitions(eosfs_partition_t *partitions, int max_entries, int *count)
{
    int i;

    if (partitions == 0 || count == 0 || max_entries <= 0) {
        return 0;
    }

    *count = g_partition_count;
    if (*count > max_entries) {
        *count = max_entries;
    }

    for (i = 0; i < *count; ++i) {
        partitions[i] = g_partitions[i];
    }

    return 1;
}
