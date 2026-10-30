#include "storage.h"

static const char *detected_storage = "EFI-managed";

void storage_init(void)
{
    /* UEFI provides disk access through EFI_BLOCK_IO and file protocols.
       Legacy ATA/IDE port probing is not used in pure EFI mode. */
    detected_storage = "EFI-managed";
}

const char *storage_type(void)
{
    return detected_storage;
}
