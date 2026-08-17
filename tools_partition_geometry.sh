#!/usr/bin/env bash
#
# SYSTEM/CACHE bolumlerinin fiziksel UFS geometrisini stok PIT'ten okur ve
# dosya sistemi BPB'sinin ayni geometriyi tasidigini dogrular.
#
# Bu dosya calistirilmaz; imaj ureten betikler tarafindan source edilir.

readonly B2L_UFS_LOGICAL_SECTOR_BYTES=4096
readonly B2L_BPB_HEADS=255
readonly B2L_BPB_SECTORS_PER_TRACK=63

b2l_read_le16() {
  local file="$1" offset="$2" b0 b1
  read -r b0 b1 < <(od -An -tu1 -j "$offset" -N2 "$file")
  [[ -n "${b0:-}" && -n "${b1:-}" ]] || return 1
  printf '%u\n' "$((b0 | (b1 << 8)))"
}

b2l_read_le32() {
  local file="$1" offset="$2" b0 b1 b2 b3
  read -r b0 b1 b2 b3 < <(od -An -tu1 -j "$offset" -N4 "$file")
  [[ -n "${b0:-}" && -n "${b1:-}" && -n "${b2:-}" && -n "${b3:-}" ]] || return 1
  printf '%u\n' "$((b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)))"
}

b2l_read_u8() {
  local file="$1" offset="$2" value
  value="$(od -An -tu1 -j "$offset" -N1 "$file" | tr -d '[:space:]')"
  [[ -n "$value" ]] || return 1
  printf '%u\n' "$value"
}

b2l_pit_partition_geometry() {
  local pit="$1" partition="$2" name_offset start_lba block_count trailing_byte
  local name_offsets match_count

  [[ -s "$pit" ]] || {
    echo "HATA: PIT bulunamadi veya bos: $pit" >&2
    return 1
  }
  [[ "$partition" =~ ^[A-Z0-9_]+$ ]] || {
    echo "HATA: gecersiz PIT bolum adi: $partition" >&2
    return 1
  }

  name_offsets="$(LC_ALL=C grep -aob -- "$partition" "$pit" | cut -d: -f1 || true)"
  match_count="$(printf '%s\n' "$name_offsets" | awk 'NF { count++ } END { print count + 0 }')"
  (( match_count == 1 )) || {
    echo "HATA: PIT'te $partition adi bir kez bulunmadi (adet=$match_count)." >&2
    return 1
  }

  name_offset="$name_offsets"
  (( name_offset >= 16 )) || {
    echo "HATA: PIT $partition kaydi gecersiz offsette: $name_offset" >&2
    return 1
  }
  trailing_byte="$(b2l_read_u8 "$pit" $((name_offset + ${#partition})))" || {
    echo "HATA: PIT $partition adi sonu okunamadi." >&2
    return 1
  }
  [[ "$trailing_byte" == 0 ]] || {
    echo "HATA: PIT eslesmesi tam bolum adi degil: $partition" >&2
    return 1
  }

  # Samsung PIT girdisinde bes 32-bit kimlik/attribute alanindan sonra
  # BlockStart, BlockCount, FileOffset ve FileSize gelir; PartitionName,
  # BlockStart'tan 16 bayt sonra baslar.
  start_lba="$(b2l_read_le32 "$pit" $((name_offset - 16)))" || {
    echo "HATA: PIT $partition baslangic LBA'si okunamadi." >&2
    return 1
  }
  block_count="$(b2l_read_le32 "$pit" $((name_offset - 12)))" || {
    echo "HATA: PIT $partition blok sayisi okunamadi." >&2
    return 1
  }
  (( start_lba > 0 && block_count > 0 )) || {
    echo "HATA: PIT $partition geometrisi gecersiz: start=$start_lba blok=$block_count" >&2
    return 1
  }

  printf '%u %u\n' "$start_lba" "$block_count"
}

b2l_validate_image_fits_partition() {
  local image_bytes="$1" block_count="$2" context="$3" image_blocks

  (( image_bytes > 0 && image_bytes % B2L_UFS_LOGICAL_SECTOR_BYTES == 0 )) || {
    echo "HATA: $context imaj boyutu 4096 baytlik UFS bloklarina tam bolunmuyor: $image_bytes" >&2
    return 1
  }
  image_blocks=$((image_bytes / B2L_UFS_LOGICAL_SECTOR_BYTES))
  (( image_blocks <= block_count )) || {
    echo "HATA: $context imaji PIT bolumune sigmiyor: imaj=$image_blocks blok bolum=$block_count blok" >&2
    return 1
  }
  printf '%u\n' "$image_blocks"
}

b2l_validate_bpb_geometry() {
  local image="$1" expected_start_lba="$2" expected_cluster_sectors="$3" context="$4"
  local sector_bytes cluster_sectors sectors_per_track heads hidden_sectors

  [[ -s "$image" ]] || {
    echo "HATA: $context BPB imaji yok veya bos: $image" >&2
    return 1
  }
  sector_bytes="$(b2l_read_le16 "$image" 11)" || return 1
  cluster_sectors="$(b2l_read_u8 "$image" 13)" || return 1
  sectors_per_track="$(b2l_read_le16 "$image" 24)" || return 1
  heads="$(b2l_read_le16 "$image" 26)" || return 1
  hidden_sectors="$(b2l_read_le32 "$image" 28)" || return 1

  [[ "$sector_bytes" == "$B2L_UFS_LOGICAL_SECTOR_BYTES" &&
     "$cluster_sectors" == "$expected_cluster_sectors" &&
     "$sectors_per_track" == "$B2L_BPB_SECTORS_PER_TRACK" &&
     "$heads" == "$B2L_BPB_HEADS" &&
     "$hidden_sectors" == "$expected_start_lba" ]] || {
    echo "HATA: $context BPB boot geometrisi yanlis:" >&2
    echo "  sektor=$sector_bytes (beklenen $B2L_UFS_LOGICAL_SECTOR_BYTES)" >&2
    echo "  kume-sektoru=$cluster_sectors (beklenen $expected_cluster_sectors)" >&2
    echo "  iz-sektoru=$sectors_per_track (beklenen $B2L_BPB_SECTORS_PER_TRACK)" >&2
    echo "  kafa=$heads (beklenen $B2L_BPB_HEADS)" >&2
    echo "  gizli/baslangic-sektoru=$hidden_sectors (beklenen $expected_start_lba)" >&2
    return 1
  }
}
