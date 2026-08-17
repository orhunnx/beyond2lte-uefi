param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$StageDirectory
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $StageDirectory -PathType Container)) {
    Write-Error "BCD sahne dizini bulunamadi: $StageDirectory"
    exit 2
}

$BatchFile = Join-Path $StageDirectory "bcd_olustur.cmd"
if (-not (Test-Path -LiteralPath $BatchFile -PathType Leaf)) {
    Write-Error "BCD uretim betigi bulunamadi: $BatchFile"
    exit 2
}

Write-Host "  Windows yonetici izni gerekiyor; UAC penceresinde Evet'i secin."

try {
    # BCDEdit BCD degisikliklerinde yonetici yetkisi ister. Yalniz bu kisa
    # alt sureci RunAs ile yukselt; WSL orkestratorunun geri kalani normal
    # kullanici olarak calismaya devam etsin.
    $Process = Start-Process `
        -FilePath $env:ComSpec `
        -ArgumentList @("/d", "/c", "bcd_olustur.cmd", "disk", "BCD") `
        -WorkingDirectory $StageDirectory `
        -Verb RunAs `
        -Wait `
        -PassThru
} catch {
    Write-Error "BCD icin Windows yonetici izni alinamadi. UAC istegini onaylayin. Ayrinti: $($_.Exception.Message)"
    exit 1
}

if ($null -eq $Process.ExitCode) {
    Write-Error "Yukseltilmis BCD surecinin cikis kodu alinamadi."
    exit 1
}

exit $Process.ExitCode
