#!/usr/bin/env python3
"""
Mu-Silicium'un UfsDxe.c'sine YAZMA YOLU ICIN tani ve duzeltme yamasi uygular.

    python3 tools_ufs_patch.py ~/Mu-Silicium

NEDEN YAMA, NEDEN KOPYA DEGIL:
UfsDxe.c ~1000 satir. Tamamini overlay'e kopyalarsak upstream duzeltmelerini
kaybederiz ve dosya cabuk bayatlar. Burada sadece dokundugumuz satirlar var,
uygulanmazsa script DURUYOR (sessizce eski davranisla devam etmiyor).

SORUN:
UEFI'den UFS'e yazma calismiyor. Iki bagimsiz kanit:
  - EFI Shell "> a dosya" yonlendirmesi "Unable to redirect file" veriyor
  - bootmgr acilis durumunu bootstat.dat olarak ESP'ye yazamiyor
Okuma sorunsuz: FAT baglaniyor, boot.wim okunuyor, Windows aciliyor.

BU YAMA NE YAPIYOR:

1) Onbellek bakimi (dogru olan, tek basina yeterli olmayabilir).
   UfsDxe.c'de DMA oncesi/sonrasi hicbir cache islemi yok. Yazmadan once
   tamponu geri yaziyoruz, okuduktan sonra gecersiz kiliyoruz.

   NOT: Bu tek basina aciklamiyor. Komut tanimlayicilari (UTRD/CmdDesc) da
   DMA ile okunuyor ve onlar calisiyor; koherans tamamen bozuk olsaydi okuma
   da calismazdi. Yine de dogru ve bedava.

2) TANI. Asil amac bu. Yazma yolunun her adimini ekrana basiyoruz:
     - UfsWriteBlocks cagriliyor mu, hangi LBA ve kac blok
     - Media.ReadOnly gercekten FALSE mu
     - SCSI WRITE_10 hangi durumla donuyor (OCS + SCSI status)
   Bir aciliste "yazma neden olmuyor" sorusu cevaplanacak.
"""
import sys, os, re

MU = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Mu-Silicium")
SRC = os.path.join(MU, "Silicon/Samsung/SamsungPkg/Drivers/UfsDxe/UfsDxe.c")

if not os.path.isfile(SRC):
    sys.exit(f"UfsDxe.c bulunamadi: {SRC}")

t = open(SRC).read()

if "B2L_UFS_PATCH" in t:
    print("  zaten yamali, atlaniyor")
    sys.exit(0)

orig = t
edits = 0

# --- 1) CacheMaintenanceLib include ----------------------------------------
old = "#include <Library/IoLib.h>"
new = "#include <Library/IoLib.h>\n#include <Library/CacheMaintenanceLib.h>   /* B2L_UFS_PATCH */"
if old in t:
    t = t.replace(old, new, 1); edits += 1

# --- 2) UfsWrite: DMA oncesi onbellegi geri yaz ----------------------------
old = """    Cmd.Lun = Lun;

    Status = UfsUtpCmdProcess(Ufs, &Cmd);
    if (EFI_ERROR (Status))
    {
      return Status;
    }

    Lba += ChunkBlks;
    BufPtr += ChunkBlks * BlkSize;
    BlkCnt -= ChunkBlks;
  }

  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
UfsReadBlocks ("""
new = """    Cmd.Lun = Lun;

    /* B2L_UFS_PATCH: aygit DRAM'den okuyacak, CPU'nun yazdigi veri
       onbellekte kalmasin */
    WriteBackDataCacheRange (BufPtr, (UINTN)(ChunkBlks * BlkSize));

    Status = UfsUtpCmdProcess(Ufs, &Cmd);
    if (EFI_ERROR (Status))
    {
      DEBUG ((EFI_D_ERROR, "B2L UFS: WRITE_10 basarisiz LBA=%u blok=%u -> %r\\n",
              Lba, (UINT32)ChunkBlks, Status));
      return Status;
    }

    Lba += ChunkBlks;
    BufPtr += ChunkBlks * BlkSize;
    BlkCnt -= ChunkBlks;
  }

  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
UfsReadBlocks ("""
if old in t:
    t = t.replace(old, new, 1); edits += 1

# --- 3) UfsRead: DMA sonrasi onbellegi gecersiz kil ------------------------
old = """    Status = UfsUtpCmdProcess (Ufs, &Cmd);
    if (EFI_ERROR (Status))
    {
      return Status;
    }

    Lba += ChunkBlks;"""
new = """    Status = UfsUtpCmdProcess (Ufs, &Cmd);
    if (EFI_ERROR (Status))
    {
      return Status;
    }

    /* B2L_UFS_PATCH: aygit DRAM'e yazdi, CPU bayat satir okumasin */
    InvalidateDataCacheRange (BufPtr, (UINTN)(ChunkBlks * BlkSize));

    Lba += ChunkBlks;"""
if old in t:
    t = t.replace(old, new, 1); edits += 1

# --- 4) UfsWriteBlocks: cagriliyor mu, hangi sartla donuyor ----------------
old = """  UFS_LUN_DEV *Dev = UFS_LUN_FROM_BLOCKIO(This);

  if (!Buffer)                       return EFI_INVALID_PARAMETER;
  if (MediaId != Dev->Media.MediaId) return EFI_MEDIA_CHANGED;
  if (Dev->Media.ReadOnly)           return EFI_WRITE_PROTECTED;
  if (BufferSize == 0)               return EFI_SUCCESS;
  if (BufferSize % Dev->Media.BlockSize) return EFI_BAD_BUFFER_SIZE;

  return UfsWrite("""
new = """  UFS_LUN_DEV *Dev = UFS_LUN_FROM_BLOCKIO(This);
  EFI_STATUS   B2LSt;

  /* B2L_UFS_PATCH: yazma yolu tanisi */
  DEBUG ((EFI_D_ERROR,
          "B2L UFS: WriteBlocks LUN=%d LBA=%lu boyut=%lu blok=%d ro=%d\\n",
          Dev->Lun, (UINT64)Lba, (UINT64)BufferSize,
          Dev->Media.BlockSize, Dev->Media.ReadOnly));

  if (!Buffer)                       return EFI_INVALID_PARAMETER;
  if (MediaId != Dev->Media.MediaId) return EFI_MEDIA_CHANGED;
  if (Dev->Media.ReadOnly) {
    DEBUG ((EFI_D_ERROR, "B2L UFS: Media.ReadOnly TRUE -> WRITE_PROTECTED\\n"));
    return EFI_WRITE_PROTECTED;
  }
  if (BufferSize == 0)               return EFI_SUCCESS;
  if (BufferSize % Dev->Media.BlockSize) {
    DEBUG ((EFI_D_ERROR, "B2L UFS: boyut blok katı degil -> BAD_BUFFER_SIZE\\n"));
    return EFI_BAD_BUFFER_SIZE;
  }

  B2LSt = UfsWrite("""
if old in t:
    t = t.replace(old, new, 1); edits += 1
    # cagrinin sonundaki ");" den sonra sonucu bas ve dondur
    old2 = """  B2LSt = UfsWrite(
    Dev->Ufs,
    Dev->Lun,
    (UINT32)Lba,
    BufferSize / Dev->Media.BlockSize,
    Dev->Media.BlockSize,
    Buffer
  );
}"""
    new2 = """  B2LSt = UfsWrite(
    Dev->Ufs,
    Dev->Lun,
    (UINT32)Lba,
    BufferSize / Dev->Media.BlockSize,
    Dev->Media.BlockSize,
    Buffer
  );

  DEBUG ((EFI_D_ERROR, "B2L UFS: WriteBlocks sonuc = %r\\n", B2LSt));
  return B2LSt;
}"""
    if old2 in t:
        t = t.replace(old2, new2, 1); edits += 1

if edits < 5:
    sys.exit(f"HATA: yamanin sadece {edits}/5 parcasi uygulandi.\n"
             f"UfsDxe.c upstream'de degismis olabilir. Elle bakmak gerek:\n  {SRC}")

open(SRC, "w").write(t)
print(f"  UfsDxe.c yamalandi ({edits} parca, {len(t)-len(orig):+d} bayt)")
