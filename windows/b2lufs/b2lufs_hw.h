/*
 * Exynos 9820 UFS — Windows StorPort platform sözleşmesi.
 *
 * UEFI'de çalışan PlatformUfsLib ve sonraki Windows miniportunun ortak
 * donanım tanımıdır. Bu başlık işletim sistemi API'si içermez; WDK dışındaki
 * birim testlerde de derlenebilmesi kasıtlıdır.
 */
#ifndef B2LUFS_HW_H
#define B2LUFS_HW_H

#include <stdint.h>

#define B2LUFS_RESOURCE_COUNT          6U

#define B2LUFS_HCI_BASE                UINT64_C(0x13D60000)
#define B2LUFS_HCI_LENGTH              UINT32_C(0x00001000)
#define B2LUFS_VS_HCI_BASE             UINT64_C(0x13D61100)
#define B2LUFS_VS_HCI_LENGTH           UINT32_C(0x00000200)
#define B2LUFS_UNIPRO_BASE             UINT64_C(0x13D50000)
#define B2LUFS_UNIPRO_LENGTH           UINT32_C(0x00008000)
#define B2LUFS_PHY_PMA_BASE            UINT64_C(0x13D64000)
#define B2LUFS_PHY_PMA_LENGTH          UINT32_C(0x00000800)
#define B2LUFS_IO_COHERENCY_BASE       UINT64_C(0x13C10700)
#define B2LUFS_IO_COHERENCY_LENGTH     UINT32_C(0x00000004)
#define B2LUFS_PMU_UFS_PHY_BASE        UINT64_C(0x15860724)
#define B2LUFS_PMU_UFS_PHY_LENGTH      UINT32_C(0x00000004)

/* UEFI BoardInit.c ve stok sboot tarafında doğrulanmış değerler. */
#define B2LUFS_UNIPRO_MCLK_HZ          UINT32_C(166000000)
#define B2LUFS_IO_COHERENCY_BITS       UINT32_C(0x00C00000)
#define B2LUFS_1US_TO_CNT_MASK         UINT32_C(0x000003FF)
#define B2LUFS_IA_TICK_SEL             UINT32_C(0x00010000)

/* Standart UFSHCI alanları (HCI kaynağına göre ofset). */
#define B2LUFS_REG_CAP                 UINT32_C(0x0000)
#define B2LUFS_REG_VER                 UINT32_C(0x0008)
#define B2LUFS_REG_IS                  UINT32_C(0x0020)
#define B2LUFS_REG_HCE                 UINT32_C(0x0034)
#define B2LUFS_REG_UECPA               UINT32_C(0x0038)
#define B2LUFS_REG_UECDL               UINT32_C(0x003C)

/* Samsung VS-HCI alanları (VS-HCI kaynağına göre ofset). */
#define B2LUFS_VS_1US_TO_CNT           UINT32_C(0x000C)
#define B2LUFS_VS_UFSHCI_V2P1_CTRL     UINT32_C(0x008C)

typedef enum B2LUFS_RESOURCE_ID {
    B2LUfsResourceHci = 0,
    B2LUfsResourceVsHci,
    B2LUfsResourceUnipro,
    B2LUfsResourcePhyPma,
    B2LUfsResourceIoCoherency,
    B2LUfsResourcePmuUfsPhy
} B2LUFS_RESOURCE_ID;

/* StorPort ACCESS_RANGE'in işletim sistemi bağımsız görünümü. */
typedef struct B2LUFS_RESOURCE {
    uint64_t Base;
    uint32_t Length;
    uint8_t  InMemory;
    void    *MappedBase;
} B2LUFS_RESOURCE;

typedef struct B2LUFS_RESOURCES {
    void *Base[B2LUFS_RESOURCE_COUNT];
} B2LUFS_RESOURCES;

/* 0: kaynak seti eksiksiz ve tam; negatif: güvenli biçimde reddedildi. */
int B2LUfsBindResources(
    const B2LUFS_RESOURCE *Resources,
    uint32_t ResourceCount,
    B2LUFS_RESOURCES *Bound);

#endif /* B2LUFS_HW_H */
