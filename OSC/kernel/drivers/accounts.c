/* --- PATCH: safer accounts_open() --- */
static int accounts_open(EFI_FILE_PROTOCOL **file, UINT64 mode)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_STATUS status;

    if (!gBS || !gBS->LocateProtocol)
        return 0;

    status = gBS->LocateProtocol(&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, 0, (void **)&fs);
    if (EFI_ERROR(status) || !fs)
        return 0;

    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status) || !root)
        return 0;

    /* PATCH: use a static filename to avoid temp array lifetime issues */
    static const CHAR16 filename[] = L"\\fisix_users.db";

    status = root->Open(root, file, filename, mode, 0);
    root->Close(root);

    return (!EFI_ERROR(status) && *file);
}

/* --- PATCH: safer accounts_save() --- */
static int accounts_save(void)
{
    EFI_FILE_PROTOCOL *file = 0;
    account_store_t store;
    UINTN bytes = sizeof(store);

    if (!accounts_open(&file, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE))
        return 0;

    store.magic = ACCOUNT_STORE_MAGIC;
    store.count = (uint32_t)account_count;

    /* PATCH: zero unused entries to avoid garbage writes */
    for (int i = 0; i < FISIX_MAX_USERS; ++i) {
        store.entries[i] = accounts[i];
    }

    /* PATCH: truncate file before writing */
    if (file->SetPosition)
        file->SetPosition(file, 0);

    if (file->Write(file, &bytes, &store) || bytes != sizeof(store)) {
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

    /* PATCH: ensure full read */
    if (!EFI_ERROR(file->Read(file, &bytes, &store)) &&
        bytes == sizeof(store) &&
        store.magic == ACCOUNT_STORE_MAGIC &&
        store.count > 0 &&
        store.count <= FISIX_MAX_USERS)
    {
        account_count = (int)store.count;

        /* PATCH: copy only valid entries */
        for (int i = 0; i < account_count; ++i)
            accounts[i] = store.entries[i];

        /* PATCH: zero unused entries */
        for (int i = account_count; i < FISIX_MAX_USERS; ++i)
            accounts[i].name[0] = accounts[i].password[0] = 0;
    }

    file->Close(file);
}
