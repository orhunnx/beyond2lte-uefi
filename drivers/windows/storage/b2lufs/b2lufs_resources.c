/*
 * B2LUFS işletim sistemi bağımsız sözleşme katmanı.
 *
 * StorPort miniportu HwFindAdapter içinde ACCESS_RANGE dizisini bu saf
 * fonksiyona dönüştürür. Fiziksel adres taraması, MmMapIoSpace veya yazma
 * burada YOKTUR: PnP'nin verdiği kaynakların 9820 sözleşmesine uyduğu
 * doğrulanır. Aynı dosyadaki GPT yardımcısı da yalnız sürücünün CPU'daki
 * salt Windows snapshot'ını değiştirir; fiziksel medyaya erişmez.
 */
#include "b2lufs_hw.h"

typedef struct B2LUFS_EXPECTED_RESOURCE {
    uint64_t Base;
    uint32_t MinimumLength;
} B2LUFS_EXPECTED_RESOURCE;

static const B2LUFS_EXPECTED_RESOURCE B2LUfsExpected[B2LUFS_RESOURCE_COUNT] = {
    { B2LUFS_HCI_BASE,          B2LUFS_HCI_LENGTH },
    { B2LUFS_VS_HCI_BASE,       B2LUFS_VS_HCI_LENGTH },
    { B2LUFS_UNIPRO_BASE,       B2LUFS_UNIPRO_LENGTH },
    { B2LUFS_PHY_PMA_BASE,      B2LUFS_PHY_PMA_LENGTH },
    { B2LUFS_IO_COHERENCY_BASE, B2LUFS_IO_COHERENCY_LENGTH },
    { B2LUFS_PMU_UFS_PHY_BASE,  B2LUFS_PMU_UFS_PHY_LENGTH }
};

int
B2LUfsBindResources(
    const B2LUFS_RESOURCE *Resources,
    uint32_t ResourceCount,
    B2LUFS_RESOURCES *Bound)
{
    uint32_t ExpectedIndex;

    if (Resources == 0 || Bound == 0 || ResourceCount != B2LUFS_RESOURCE_COUNT) {
        return -1;
    }

    for (ExpectedIndex = 0; ExpectedIndex < B2LUFS_RESOURCE_COUNT; ++ExpectedIndex) {
        uint32_t GivenIndex;
        uint32_t Matches;

        Matches = 0;
        Bound->Base[ExpectedIndex] = 0;

        for (GivenIndex = 0; GivenIndex < ResourceCount; ++GivenIndex) {
            const B2LUFS_RESOURCE *Given = &Resources[GivenIndex];

            if (Given->Base != B2LUfsExpected[ExpectedIndex].Base) {
                continue;
            }

            if (Given->InMemory == 0 ||
                Given->Length < B2LUfsExpected[ExpectedIndex].MinimumLength ||
                Given->MappedBase == 0) {
                return -2;
            }

            Bound->Base[ExpectedIndex] = Given->MappedBase;
            ++Matches;
        }

        /* Aynı taban iki kez gelirse hangi pencerenin gerçek olduğu belirsizdir. */
        if (Matches != 1) {
            return -3;
        }
    }

    return 0;
}

int
B2LUfsGptEntryNameEqualsAscii(
    const uint8_t *Entry,
    uint32_t EntrySize,
    const char *ExpectedName)
{
    uint32_t Index;

    if (Entry == 0 || ExpectedName == 0 || ExpectedName[0] == '\0' ||
        EntrySize < B2LUFS_GPT_ENTRY_NAME_OFFSET +
                    (B2LUFS_GPT_ENTRY_NAME_CODE_UNITS * UINT32_C(2))) {
        return 0;
    }

    for (Index = 0; ExpectedName[Index] != '\0'; ++Index) {
        uint8_t EntryValue;
        uint8_t ExpectedValue;

        /* GPT adında NUL sonlandırıcı için en az bir UTF-16 code unit bırak. */
        if (Index >= B2LUFS_GPT_ENTRY_NAME_CODE_UNITS - UINT32_C(1)) {
            return 0;
        }
        ExpectedValue = (uint8_t)ExpectedName[Index];
        if (ExpectedValue >= (uint8_t)'a' && ExpectedValue <= (uint8_t)'z') {
            ExpectedValue = (uint8_t)(ExpectedValue -
                                      ((uint8_t)'a' - (uint8_t)'A'));
        }
        if (ExpectedValue < (uint8_t)'A' || ExpectedValue > (uint8_t)'Z') {
            return 0;
        }

        EntryValue = Entry[B2LUFS_GPT_ENTRY_NAME_OFFSET +
                           (Index * UINT32_C(2))];
        if (EntryValue >= (uint8_t)'a' && EntryValue <= (uint8_t)'z') {
            EntryValue = (uint8_t)(EntryValue -
                                   ((uint8_t)'a' - (uint8_t)'A'));
        }
        if (EntryValue != ExpectedValue ||
            Entry[B2LUFS_GPT_ENTRY_NAME_OFFSET +
                  (Index * UINT32_C(2)) + UINT32_C(1)] != 0) {
            return 0;
        }
    }

    /* SYSTEMX, SYSTEM-old vb. kısmi eşleşmeler boot bölümünü seçmemeli. */
    return Entry[B2LUFS_GPT_ENTRY_NAME_OFFSET + (Index * UINT32_C(2))] == 0 &&
           Entry[B2LUFS_GPT_ENTRY_NAME_OFFSET + (Index * UINT32_C(2)) +
                 UINT32_C(1)] == 0;
}

int
B2LUfsNormalizeWindowsDataPartition(
    uint8_t *Entry,
    uint32_t EntrySize,
    const char *ExpectedName)
{
    static const uint8_t BasicDataGuid[B2LUFS_GPT_ENTRY_TYPE_BYTES] = {
        0xA2U, 0xA0U, 0xD0U, 0xEBU, 0xE5U, 0xB9U, 0x33U, 0x44U,
        0x87U, 0xC0U, 0x68U, 0xB6U, 0xB7U, 0x26U, 0x99U, 0xC7U
    };
    uint32_t Index;

    if (!B2LUfsGptEntryNameEqualsAscii(Entry, EntrySize, ExpectedName)) {
        return 0;
    }

    for (Index = 0; Index < B2LUFS_GPT_ENTRY_TYPE_BYTES; ++Index) {
        Entry[B2LUFS_GPT_ENTRY_TYPE_OFFSET + Index] = BasicDataGuid[Index];
    }
    for (Index = 0; Index < B2LUFS_GPT_ENTRY_ATTRIBUTES_BYTES; ++Index) {
        Entry[B2LUFS_GPT_ENTRY_ATTRIBUTES_OFFSET + Index] = 0;
    }
    return 1;
}
