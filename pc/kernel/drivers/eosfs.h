#ifndef EOSFS_H
#define EOSFS_H

#include "efi.h"

#define EOSFS_MAX_ENTRIES 32
#define EOSFS_GPT_SIGNATURE 0x5452415020494645ULL

typedef enum {
    EOSFS_ENTRY_DIR = 0,
    EOSFS_ENTRY_FILE = 1
} eosfs_entry_type_t;

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t partition_entry_count;
    uint32_t partition_entry_size;
    uint32_t partition_crc32;
} eosfs_gpt_header_t;

typedef struct {
    uint8_t partition_type_guid[16];
    uint8_t unique_partition_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    CHAR16 name[36];
} eosfs_partition_t;

typedef struct {
    CHAR16 name[16];
    eosfs_entry_type_t type;
} eosfs_entry_t;

void eosfs_init(void);
int eosfs_list_path(const CHAR16 *path, eosfs_entry_t *entries, int max_entries, int *count);
int eosfs_get_gpt_header(eosfs_gpt_header_t *header);
int eosfs_list_partitions(eosfs_partition_t *partitions, int max_entries, int *count);

#endif
