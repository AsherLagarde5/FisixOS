#ifndef EFI_H
#define EFI_H

#include <stdint.h>
#include <stddef.h>

#define EFI_SUCCESS 0
#define EFI_BUFFER_TOO_SMALL 5
#define EFI_NOT_FOUND 14
#define EFI_NO_MEDIA 19
#define EFI_LOAD_ERROR 21
#define EFI_INVALID_PARAMETER 2
#define EFI_UNSUPPORTED 3
#define EFI_ACCESS_DENIED 4
#define EFI_DEVICE_ERROR 7
#define EFI_VOLUME_FULL 8
#define EFI_WRITE_PROTECTED 9
#define EFI_OUT_OF_RESOURCES 9
#define EFI_VOLUME_CORRUPTED 10
#define EFI_FILE_SYSTEM_CORRUPTED 11
#define EFI_BAD_BUFFER_SIZE 12

#define EFI_ERROR(Status) ((Status) != EFI_SUCCESS)
#define EFIAPI __attribute__((ms_abi))

typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint16_t CHAR16;
typedef uint64_t UINT64;
typedef uint32_t UINT32;
typedef size_t UINTN;
typedef uint8_t BOOLEAN;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} EFI_GUID;

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    (EFI_GUID){ 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_FILE_INFO_GUID \
    (EFI_GUID){ 0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_ACPI_20_TABLE_GUID \
    (EFI_GUID){ 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }

#define EFI_ACPI_TABLE_GUID \
    (EFI_GUID){ 0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE  0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL

#define EFI_FILE_READ_ONLY 0x0000000000000001ULL
#define EFI_FILE_HIDDEN    0x0000000000000002ULL
#define EFI_FILE_SYSTEM    0x0000000000000004ULL
#define EFI_FILE_RESERVED  0x0000000000000008ULL
#define EFI_FILE_DIRECTORY 0x0000000000000010ULL
#define EFI_FILE_ARCHIVE   0x0000000000000020ULL

/* EFI_TABLE_HEADER is 24 bytes, not a single pointer. The old code used
   void *Hdr (8 bytes) here and in EFI_BOOT_SERVICES, shifting every
   following field by 16 bytes: BootServices, ConOut, etc. were read
   from the wrong offsets, so OVMF crashed with #UD at a bogus RIP
   as soon as the loader touched firmware services. */
typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct EFI_CONFIGURATION_TABLE EFI_CONFIGURATION_TABLE;

typedef struct {
    uint16_t ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    void *Reset;
    EFI_STATUS (EFIAPI *ReadKeyStroke)(void *This, EFI_INPUT_KEY *Key);
    void *WaitForKey;
};

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *Reset;
    EFI_STATUS (EFIAPI *OutputString)(void *This, const CHAR16 *String);
    /* Real UEFI order after OutputString. The old header stopped here,
       so ClearScreen/SetAttribute could not be called at all. Adding
       these fields after OutputString does not move OutputString. */
    void *QueryMode;
    void *SetMode;
    EFI_STATUS (EFIAPI *SetAttribute)(void *This, UINTN Attribute);
    EFI_STATUS (EFIAPI *ClearScreen)(void *This);
    void *SetCursorPosition;
    void *EnableCursor;
    void *Mode;
};

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    void *StandardErrorHandle;
    void *ErrOut;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    size_t NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct EFI_CONFIGURATION_TABLE {
    EFI_GUID VendorGuid;
    void *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
    uint16_t Year;
    uint8_t Month;
    uint8_t Day;
    uint8_t Hour;
    uint8_t Minute;
    uint8_t Second;
    uint8_t Pad1;
    uint32_t Nanosecond;
    int16_t TimeZone;
    uint8_t Daylight;
    uint8_t Pad2;
} EFI_TIME;

typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64 Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

typedef struct {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(void *This, EFI_FILE_PROTOCOL **Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

#define EFI_BLOCK_IO_PROTOCOL_GUID \
    (EFI_GUID){ 0x964e5b21, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    (EFI_GUID){ 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

#define EFI_SMBIOS_TABLE_GUID \
    (EFI_GUID){ 0xeb9d2d31, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    (EFI_GUID){ 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

typedef struct {
    UINT32 MediaId;
    BOOLEAN RemovableMedia;
    BOOLEAN MediaPresent;
    BOOLEAN LogicalPartition;
    BOOLEAN ReadOnly;
    BOOLEAN WriteCaching;
    UINT32 BlockSize;
    UINT32 IoAlign;
    UINT64 LastBlock;
    UINT64 LowestAlignedLba;
    UINT32 LogicalBlocksPerPhysicalBlock;
    UINT32 OptimalTransferLengthGranularity;
} EFI_BLOCK_IO_MEDIA;

typedef struct EFI_BLOCK_IO_PROTOCOL EFI_BLOCK_IO_PROTOCOL;
struct EFI_BLOCK_IO_PROTOCOL {
    UINT64 Revision;
    EFI_BLOCK_IO_MEDIA *Media;
    EFI_STATUS (EFIAPI *Reset)(EFI_BLOCK_IO_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *ReadBlocks)(EFI_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, UINT64 Lba, UINTN BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *WriteBlocks)(EFI_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, UINT64 Lba, UINTN BufferSize, const void *Buffer);
    EFI_STATUS (EFIAPI *FlushBlocks)(EFI_BLOCK_IO_PROTOCOL *This);
};

typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    void *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    UINT32 LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    UINT64 ImageSize;
    UINT32 ImageCodeType;
    UINT32 ImageDataType;
    EFI_STATUS (EFIAPI *Unload)(EFI_HANDLE ImageHandle);
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelFormat;
    UINT32 PixelInformation[4]; /* EFI_PIXEL_BITMASK: Red, Green, Blue, Reserved */
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINT64 SizeOfInfo;
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    EFI_STATUS (EFIAPI *QueryMode)(void *This, UINT32 ModeNumber, UINT64 *SizeOfInfo, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);
    EFI_STATUS (EFIAPI *SetMode)(void *This, UINT32 ModeNumber);
    EFI_STATUS (EFIAPI *Blt)(void *This, void *BltBuffer, UINT32 BltOperation, UINT64 SourceX, UINT64 SourceY, UINT64 DestinationX, UINT64 DestinationY, UINT64 Width, UINT64 Height, UINT64 Delta);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;



struct EFI_FILE_PROTOCOL {
    /* UEFI 13.5.1: the first field is Revision, not Open. Omitting it
       shifted every method by 8 bytes, so root->Open jumped to the
       Revision value 0x00020000 itself (the "0x20000 Open pointer"
       anomaly) instead of the real Open at the next qword. */
    UINT64 Revision;
    EFI_STATUS (EFIAPI *Open)(EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **NewHandle, const CHAR16 *FileName, UINT64 OpenMode, UINT64 Attributes);
    EFI_STATUS (EFIAPI *Close)(EFI_FILE_PROTOCOL *This);
    EFI_STATUS (EFIAPI *Delete)(EFI_FILE_PROTOCOL *This);
    EFI_STATUS (EFIAPI *Read)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Write)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, const void *Buffer);
    EFI_STATUS (EFIAPI *GetPosition)(EFI_FILE_PROTOCOL *This, UINT64 *Position);
    EFI_STATUS (EFIAPI *SetPosition)(EFI_FILE_PROTOCOL *This, UINT64 Position);
    EFI_STATUS (EFIAPI *GetInfo)(EFI_FILE_PROTOCOL *This, const EFI_GUID *InformationType, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *SetInfo)(EFI_FILE_PROTOCOL *This, const EFI_GUID *InformationType, UINTN BufferSize, const void *Buffer);
    EFI_STATUS (EFIAPI *Flush)(EFI_FILE_PROTOCOL *This);
};

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    uint32_t Type;
    uint32_t Pad; // Ensures 8-byte alignment for PhysicalStart under x86_64 MS ABI
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} __attribute__((packed)) EFI_MEMORY_DESCRIPTOR;

/* Typedef function prototypes using EFIAPI (ms_abi) */
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    UINTN *MemoryMapSize, 
    EFI_MEMORY_DESCRIPTOR *MemoryMap, 
    UINTN *MapKey, 
    UINTN *DescriptorSize, 
    UINT32 *DescriptorVersion
);

typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE ImageHandle, 
    UINTN MapKey
);

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    UINT32 Type, 
    EFI_MEMORY_TYPE MemoryType, 
    UINTN Pages, 
    uint64_t *Memory
);

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(
    EFI_MEMORY_TYPE PoolType, 
    UINTN Size, 
    void **Buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(
    void *Buffer
);

typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    void *FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    void *HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void *GetNextMonotonicCount;
    void *Stall;
    void *SetWatchdogTimer;
    void *ConnectController;
    void *DisconnectController;
    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    EFI_STATUS (EFIAPI *LocateProtocol)(const EFI_GUID *Protocol, void *SearchKey, void **Interface);
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

enum { AllocateAnyPages, AllocateMaxAddress, AllocateAddress };

extern EFI_HANDLE gImageHandle;
extern EFI_SYSTEM_TABLE *gST;
extern EFI_BOOT_SERVICES *gBS;
/* Bootloader selection, shared with accounts/eosfs so a menu choice
   actually changes which disk the login, setup and kernel load use.
   NULL = first SimpleFileSystem found (old behaviour). */
extern EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fisix_selected_fs;
extern int fisix_live_boot;

#endif
