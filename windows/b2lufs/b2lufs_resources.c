/*
 * B2LUFS kaynak bağlama katmanı.
 *
 * StorPort miniportu HwFindAdapter içinde ACCESS_RANGE dizisini bu saf
 * fonksiyona dönüştürür. Fiziksel adres taraması, MmMapIoSpace veya yazma
 * burada YOKTUR: yalnızca PnP'nin verdiği kaynakların 9820 sözleşmesine
 * uyduğu doğrulanır.
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
