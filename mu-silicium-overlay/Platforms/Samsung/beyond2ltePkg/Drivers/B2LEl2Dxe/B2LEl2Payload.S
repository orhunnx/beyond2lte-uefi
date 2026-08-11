//
// B2LEl2Payload.S - EL2'de calisan yuk: istisna vektoru + SMC tuzagi.
//
// ===========================================================================
// NEDEN
// ===========================================================================
// Windows, ntoskrnl basladiktan ~12 saniye sonra sessizce cihazi yeniden
// baslatiyor. Olculenler:
//   - platform EBS sonrasi 60+ saniye saglam (EbsTest)  -> sifirlamayi Windows yapiyor
//   - ResetSystem kancasi hic atesmedi                  -> UEFI'yi atliyor
//   - AutoReboot=0 dogrulandi, yine sifirliyor          -> bugcheck degil
//   - hicbir yere kayit dusmuyor                        -> log kanali yok
//
// EL1'den yapilabilecek her sey tukendi. Geriye Windows'un EL3'e attigi
// SMC'leri gormek kaliyor ve bunun icin EL2 gerekiyor.
//
// ===========================================================================
// EL2'YE NASIL CIKIYORUZ
// ===========================================================================
// S-Boot cekirdegi EL1'de birakiyor, ama TrustZone'da EL2'ye kod yukleyen bir
// arka kapi var. Samsung kendi cekirdeginde RKP hipervizorunu bununla
// yukluyor (init/vmm.c). Arayuz sleirsgoevy/exynos-kvm-patch'te belgelenmis:
//
//   x0 = 0xC2000400   VMM_64BIT_SMC_CALL_MAGIC
//   x1 = entry_point  EL2'de calisacak rutinin FIZIKSEL adresi
//   x2 = 4096         VMM_STACK_OFFSET
//   x3 = 1            VMM_MODE_AARCH64
//   x4, x5            rutine x0/x1 olarak gecer
//   smc #0
//
// Rutin EL2'de calisir ve "smc #0" ile x0=0xC2000401, x1=status diyerek
// doner. status SIFIR OLMAK ZORUNDA - degilse firmware HYP durumunu
// varsayilana sifirliyor ve sonraki hvc'ler istisna veriyor.
//
// UC KISIT (hepsi belgelenmis davranis):
//   1) Init yalnizca acilis cekirdeginde calisir. Digerleri onun EL2
//      durumunu KOPYALAR; tek fark sp = bootcore.sp + 4096 * cekirdek_no.
//      Bu yuzden yigin bolgemiz sekiz cekirdeklik ayrilmis durumda.
//   2) Firmware vbar_el2'yi cikista kaydedip rastgele noktalarda GERI
//      YUKLUYOR. Yani vektor adresi sonradan degistirilemez - ama bizim
//      koydugumuz yerde kalir, ki bize gereken tam olarak bu.
//   3) EL2'de MMU kapali: butun adresler fiziksel. Kod ve tampon sabit
//      fiziksel adreslerde, hicbir haritalama gerekmiyor.
//
// ===========================================================================
// YERLESIM  (MemoryMapLib'de "B2L EL2", 0xCF000000, 256 KB, Reserved)
// ===========================================================================
//   0xCF000000  istisna vektoru (2 KB, VBAR_EL2 2 KB hizali olmak zorunda)
//   0xCF000800  init rutini      <- SMC'ye entry_point olarak bu verilir
//   0xCF010000  acilis cekirdegi yigin tepesi; cekirdek N -> +4096*N
//   0xCF020000  BU acilisin SMC kaydi
//   0xCF030000  ONCEKI acilisin kaydi (DXE surucusu kopyalar, Shell okur)
//
// Vektor tablosu dosyanin BASINDA: 0xCF000000 zaten 2 KB hizali oldugu icin
// hizalama numarasina gerek kalmiyor ve yuk tek parca kopyalanabiliyor.
//
// ===========================================================================
// KAYIT BICIMI  (0xCF020000)
// ===========================================================================
//   +0x00  u32 magic 'B2L2' -> dmem'de "42 32 4C 32"
//   +0x04  u32 kayit sayisi
//   +0x10  kayitlar, her biri 32 bayt:  x0, x1, x2, ELR_EL2
//
// x0 SMC fonksiyon numarasi. Beklenen supheli: 0x84000009 SYSTEM_RESET.
//

#define EL2_VECT        0xCF000000
#define EL2_STACK_TOP   0xCF010000
#define EL2_LOG         0xCF020000
#define EL2_LOG_MAGIC   0x324C3242      // "B2L2" little-endian
#define EL2_LOG_MAX     0xFF

#define VMM_RETURN      0xC2000401

//
// 64 bitlik sabiti movz/movk ile kur. Literal havuzu KULLANMIYORUZ: yuk
// baska bir adrese kopyalaniyor ve konumdan bagimsiz olmasi gerekiyor.
//
.macro MOV64 reg, val
  movz \reg, #(((\val) >> 48) & 0xFFFF), lsl #48
  movk \reg, #(((\val) >> 32) & 0xFFFF), lsl #32
  movk \reg, #(((\val) >> 16) & 0xFFFF), lsl #16
  movk \reg, #((\val) & 0xFFFF)
.endm

  .section .text.b2lel2, "ax"
  .align 11                      // 2 KB: VBAR_EL2 hizalama sarti

  .global ASM_PFX(B2LEl2PayloadStart)
  .global ASM_PFX(B2LEl2PayloadEnd)
  .global ASM_PFX(B2LEl2InitOffset)

ASM_PFX(B2LEl2PayloadStart):

// ---------------------------------------------------------------------------
// EL2 istisna vektoru - 16 giris, her biri 128 bayt
// ---------------------------------------------------------------------------
// Yalnizca "Lower EL, AArch64, Synchronous" (0x400) bizi ilgilendiriyor;
// EL1'den tuzaklanan SMC oraya duser. Digerleri oldugu gibi geri doner -
// tuzaklamadigimiz bir sey zaten EL2'ye gelmemeli, gelirse de sistemi
// bozmayalim.
// ---------------------------------------------------------------------------
  .align 7
cur_el_sp0_sync:      eret
  .align 7
cur_el_sp0_irq:       eret
  .align 7
cur_el_sp0_fiq:       eret
  .align 7
cur_el_sp0_serr:      eret

  .align 7
cur_el_spx_sync:      eret
  .align 7
cur_el_spx_irq:       eret
  .align 7
cur_el_spx_fiq:       eret
  .align 7
cur_el_spx_serr:      eret

  .align 7
low_el_a64_sync:      b   b2l_sync_handler
  .align 7
low_el_a64_irq:       eret
  .align 7
low_el_a64_fiq:       eret
  .align 7
low_el_a64_serr:      eret

  .align 7
low_el_a32_sync:      eret
  .align 7
low_el_a32_irq:       eret
  .align 7
low_el_a32_fiq:       eret
  .align 7
low_el_a32_serr:      eret

// ---------------------------------------------------------------------------
// Init rutini - vektor tablosunun hemen ardinda, yani +0x800
// ---------------------------------------------------------------------------
  .align 11
ASM_PFX(B2LEl2InitOffset):
b2l_el2_init:
  //
  // Kendi yiginimiz. Firmware cikistaki sp'yi "bootcore.sp" olarak kaydedip
  // diger cekirdeklere +4096*N veriyor, o yuzden burada dogru degeri
  // kurmak sekiz cekirdegin tamamini belirliyor.
  //
  MOV64 x9, EL2_STACK_TOP
  mov   sp, x9

  // Istisna vektoru
  MOV64 x9, EL2_VECT
  msr   vbar_el2, x9
  isb

  //
  // HCR_EL2.TSC (bit 19): EL1'den gelen her SMC EL2'ye tuzaklanir.
  // Projede aradigimiz tek sey bu - Windows'un EL3'e ne sordugu.
  //
  mrs   x9, hcr_el2
  orr   x9, x9, #(1 << 19)
  msr   hcr_el2, x9
  isb

  //
  // Firmware'e don. status SIFIR olmak zorunda; degilse HYP durumu
  // varsayilana geri aliniyor ve butun bu is bosa gidiyor.
  //
  MOV64 x0, VMM_RETURN
  mov   x1, xzr
  smc   #0

  // Buraya normalde hic gelinmez.
1:
  wfe
  b     1b

// ---------------------------------------------------------------------------
// Tuzaklanan SMC
// ---------------------------------------------------------------------------
b2l_sync_handler:
  //
  // x0..x3 SMC'nin argumanlari ve donusu, yani onlara dokunabiliriz.
  // BASKA HICBIR REGISTER BOZULMAMALI - misafir isletim sistemi calismaya
  // devam edecek. Kazidiklarimizi yigina alip geri koyuyoruz.
  //
  stp   x4, x5, [sp, #-32]!
  stp   x6, x7, [sp, #16]

  mrs   x4, esr_el2
  lsr   x4, x4, #26
  cmp   x4, #0x17                 // EC = 0x17: tuzaklanan SMC
  b.ne  2f

  MOV64 x4, EL2_LOG
  ldr   w5, [x4, #4]              // kayit sayisi
  cmp   w5, #EL2_LOG_MAX
  b.hs  2f                        // tampon doldu, sessizce gec

  add   w6, w5, #1
  str   w6, [x4, #4]
  MOV64 x6, EL2_LOG_MAGIC
  str   w6, [x4]

  add   x7, x4, #0x10
  add   x7, x7, x5, lsl #5        // kayit = LOG + 0x10 + sayi*32
  stp   x0, x1, [x7]
  mrs   x6, elr_el2
  stp   x2, x6, [x7, #16]

2:
  ldp   x6, x7, [sp, #16]
  ldp   x4, x5, [sp], #32

  //
  // GERCEK SMC'yi EL3'e ilet. EL2'den yapilan SMC tuzaklanmaz (TSC yalnizca
  // EL1'i baglar), yani burada sonsuz donguye girmiyoruz. x0..x3 hala
  // cagiranin degerleri, donuste de sonuclar onlarda kaliyor.
  //
  smc   #0

  //
  // Tuzaklanan SMC'de ELR_EL2 SMC KOMUTUNUN KENDISINI gosterir; ilerletmek
  // bize dusuyor, yoksa ayni komuta donup sonsuz tuzaga gireriz.
  //
  str   x4, [sp, #-16]!
  mrs   x4, elr_el2
  add   x4, x4, #4
  msr   elr_el2, x4
  ldr   x4, [sp], #16
  eret

ASM_PFX(B2LEl2PayloadEnd):
  .align 3
