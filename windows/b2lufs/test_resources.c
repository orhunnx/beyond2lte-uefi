/* WDK gerekmeden kaynak ve Windows GPT sözleşmelerini doğrulayan birim testi. */
#include <assert.h>
#include <string.h>

#include "b2lufs_hw.h"

static B2LUFS_RESOURCE Good[B2LUFS_RESOURCE_COUNT] = {
    { B2LUFS_PHY_PMA_BASE,      B2LUFS_PHY_PMA_LENGTH,      1, (void *)0x4000 },
    { B2LUFS_HCI_BASE,          B2LUFS_HCI_LENGTH,          1, (void *)0x1000 },
    { B2LUFS_PMU_UFS_PHY_BASE,  B2LUFS_PMU_UFS_PHY_LENGTH,  1, (void *)0x6000 },
    { B2LUFS_UNIPRO_BASE,       B2LUFS_UNIPRO_LENGTH,       1, (void *)0x3000 },
    { B2LUFS_VS_HCI_BASE,       B2LUFS_VS_HCI_LENGTH,       1, (void *)0x2000 },
    { B2LUFS_IO_COHERENCY_BASE, B2LUFS_IO_COHERENCY_LENGTH, 1, (void *)0x5000 }
};

static void
SetGptName(uint8_t *Entry, const char *Name)
{
    uint32_t Index;

    memset(Entry, 0, 128);
    for (Index = 0; Name[Index] != '\0'; ++Index) {
        Entry[B2LUFS_GPT_ENTRY_NAME_OFFSET + (Index * 2U)] =
            (uint8_t)Name[Index];
    }
}

static void
TestWindowsGptNormalization(void)
{
    static const uint8_t BasicDataGuid[B2LUFS_GPT_ENTRY_TYPE_BYTES] = {
        0xA2U, 0xA0U, 0xD0U, 0xEBU, 0xE5U, 0xB9U, 0x33U, 0x44U,
        0x87U, 0xC0U, 0x68U, 0xB6U, 0xB7U, 0x26U, 0x99U, 0xC7U
    };
    uint8_t Entry[128];
    uint8_t Before[128];
    uint32_t Index;

    SetGptName(Entry, "system");
    memset(Entry + B2LUFS_GPT_ENTRY_TYPE_OFFSET, 0x11,
           B2LUFS_GPT_ENTRY_TYPE_BYTES);
    memset(Entry + B2LUFS_GPT_ENTRY_ATTRIBUTES_OFFSET, 0xFF,
           B2LUFS_GPT_ENTRY_ATTRIBUTES_BYTES);
    assert(B2LUfsGptEntryNameEqualsAscii(Entry, sizeof(Entry), "SYSTEM") == 1);
    assert(B2LUfsNormalizeWindowsDataPartition(
               Entry, sizeof(Entry), "SYSTEM") == 1);
    assert(memcmp(Entry + B2LUFS_GPT_ENTRY_TYPE_OFFSET,
                  BasicDataGuid, sizeof(BasicDataGuid)) == 0);
    for (Index = 0; Index < B2LUFS_GPT_ENTRY_ATTRIBUTES_BYTES; ++Index) {
        assert(Entry[B2LUFS_GPT_ENTRY_ATTRIBUTES_OFFSET + Index] == 0);
    }

    SetGptName(Entry, "CACHE");
    assert(B2LUfsNormalizeWindowsDataPartition(
               Entry, sizeof(Entry), "cache") == 1);

    SetGptName(Entry, "SYSTEMX");
    memcpy(Before, Entry, sizeof(Entry));
    assert(B2LUfsNormalizeWindowsDataPartition(
               Entry, sizeof(Entry), "SYSTEM") == 0);
    assert(memcmp(Entry, Before, sizeof(Entry)) == 0);

    SetGptName(Entry, "SYSTEM");
    Entry[B2LUFS_GPT_ENTRY_NAME_OFFSET + 1U] = 1U;
    assert(B2LUfsGptEntryNameEqualsAscii(Entry, sizeof(Entry), "SYSTEM") == 0);
    assert(B2LUfsGptEntryNameEqualsAscii(Entry, sizeof(Entry) - 1U,
                                         "SYSTEM") == 0);
    assert(B2LUfsGptEntryNameEqualsAscii(0, sizeof(Entry), "SYSTEM") == 0);
    assert(B2LUfsGptEntryNameEqualsAscii(Entry, sizeof(Entry), 0) == 0);
}

int main(void)
{
    B2LUFS_RESOURCES Bound;

    assert(B2LUfsBindResources(Good, B2LUFS_RESOURCE_COUNT, &Bound) == 0);
    assert(Bound.Base[B2LUfsResourceHci] == (void *)0x1000);
    assert(Bound.Base[B2LUfsResourceVsHci] == (void *)0x2000);
    assert(Bound.Base[B2LUfsResourceUnipro] == (void *)0x3000);
    assert(Bound.Base[B2LUfsResourcePhyPma] == (void *)0x4000);
    assert(Bound.Base[B2LUfsResourceIoCoherency] == (void *)0x5000);
    assert(Bound.Base[B2LUfsResourcePmuUfsPhy] == (void *)0x6000);

    Good[0].InMemory = 0;
    assert(B2LUfsBindResources(Good, B2LUFS_RESOURCE_COUNT, &Bound) < 0);
    Good[0].InMemory = 1;

    Good[3].Base = B2LUFS_HCI_BASE;
    assert(B2LUfsBindResources(Good, B2LUFS_RESOURCE_COUNT, &Bound) < 0);
    Good[3].Base = B2LUFS_UNIPRO_BASE;

    TestWindowsGptNormalization();

    return 0;
}
