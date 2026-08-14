<#
 .SYNOPSIS
   B2LUFS Faz 0 ARM64 StorPort miniportunu Windows MSVC/WDK ile derler.

 .DESCRIPTION
   Kaynak agaci WSL'de olsa bile cmd.exe'nin UNC calisma-dizini sinirina
   takilmaz: uc kaynak dosya Windows TEMP altindaki yerel bir dizine kopyalanir,
   cl.exe/link.exe orada calisir, yalniz dogrulanmis ARM64 SYS tekrar proje
   altindaki out\b2lufs dizinine yazilir.

   Bu betik SYS derler; katalog/imza/boot.wim paketlemesini WSL'deki
   tools_b2lufs.sh yapmaya devam eder.
#>
[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [string]$OutDir,
    [string]$KitsRoot,
    [string]$WdkVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Fail([string]$Message) {
    throw "HATA: $Message"
}

function Resolve-ExistingDirectory([string]$Path, [string]$Description) {
    if ([string]::IsNullOrWhiteSpace($Path) -or
        -not (Test-Path -LiteralPath $Path -PathType Container)) {
        Fail "$Description yok: $Path"
    }

    return (Resolve-Path -LiteralPath $Path).Path
}

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Join-Path $PSScriptRoot '..\..'
}
$ProjectRoot = Resolve-ExistingDirectory $ProjectRoot 'Proje dizini'

if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $ProjectRoot 'out\b2lufs'
}

$SourceDir = Resolve-ExistingDirectory (Join-Path $ProjectRoot 'windows\b2lufs') 'B2LUFS kaynak dizini'
if ([string]::IsNullOrWhiteSpace($KitsRoot)) {
    $ProgramFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $KitsRoot = Join-Path $ProgramFilesX86 'Windows Kits\10'
}
$KitsRoot = Resolve-ExistingDirectory $KitsRoot 'Windows Kits dizini'
$IncludeRoot = Resolve-ExistingDirectory (Join-Path $KitsRoot 'Include') 'WDK Include dizini'

if ([string]::IsNullOrWhiteSpace($WdkVersion)) {
    $Candidate = Get-ChildItem -LiteralPath $IncludeRoot -Directory |
        Where-Object {
            (Test-Path -LiteralPath (Join-Path $_.FullName 'km\ntddk.h') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'km\storport.h') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $KitsRoot ("Lib\{0}\km\arm64\storport.lib" -f $_.Name)) -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $KitsRoot ("Lib\{0}\km\arm64\ntoskrnl.lib" -f $_.Name)) -PathType Leaf)
        } |
        Where-Object { $_.Name -match '^\d+(\.\d+){1,3}$' } |
        Sort-Object { [Version]$_.Name } -Descending |
        Select-Object -First 1

    if ($null -eq $Candidate) {
        Fail 'ARM64 storport.lib ve ntoskrnl.lib iceren bir Windows WDK bulunamadi'
    }
    $WdkVersion = $Candidate.Name
}

$WdkInclude = Resolve-ExistingDirectory (Join-Path $KitsRoot ("Include\{0}" -f $WdkVersion)) 'Secilen WDK Include dizini'
$WdkLib = Resolve-ExistingDirectory (Join-Path $KitsRoot ("Lib\{0}\km\arm64" -f $WdkVersion)) 'Secilen WDK ARM64 kutuphane dizini'

$VcvarsCandidates = @()
foreach ($ProgramFilesRoot in @(
        [Environment]::GetFolderPath('ProgramFiles'),
        [Environment]::GetFolderPath('ProgramFilesX86'))) {
    if ([string]::IsNullOrWhiteSpace($ProgramFilesRoot)) {
        continue
    }
    $VisualStudioRoot = Join-Path $ProgramFilesRoot 'Microsoft Visual Studio'
    if (Test-Path -LiteralPath $VisualStudioRoot -PathType Container) {
        $VcvarsCandidates += Get-ChildItem -LiteralPath $VisualStudioRoot -Recurse -Filter 'vcvarsamd64_arm64.bat' -File
    }
}

$Vcvars = $VcvarsCandidates | Sort-Object FullName -Descending | Select-Object -First 1
if ($null -eq $Vcvars) {
    Fail "vcvarsamd64_arm64.bat yok; Visual Studio Installer'da C++ ARM64 build tools bilesenini ekle"
}

$VsBuildDirectory = Split-Path -Parent $Vcvars.FullName
$VsAuxiliaryDirectory = Split-Path -Parent $VsBuildDirectory
$VsVcDirectory = Split-Path -Parent $VsAuxiliaryDirectory
$VsRoot = Split-Path -Parent $VsVcDirectory
$MsvcToolsetRoot = Resolve-ExistingDirectory (Join-Path $VsRoot 'VC\Tools\MSVC') 'MSVC toolset dizini'
$MsvcToolset = Get-ChildItem -LiteralPath $MsvcToolsetRoot -Directory |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\arm64\cl.exe') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\arm64\link.exe') -PathType Leaf)
    } |
    Where-Object { $_.Name -match '^\d+(\.\d+){1,3}$' } |
    Sort-Object { [Version]$_.Name } -Descending |
    Select-Object -First 1
if ($null -eq $MsvcToolset) {
    Fail "MSVC ARM64 cl.exe/link.exe yok; Visual Studio Installer'da 'MSVC v143/v14x C++ ARM64/ARM64EC build tools' bilesenini ekle"
}
$MsvcArm64Bin = Join-Path $MsvcToolset.FullName 'bin\Hostx64\arm64'

$StageRoot = Join-Path ([IO.Path]::GetTempPath()) ("b2lufs-arm64-{0}" -f [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $StageRoot -Force | Out-Null

try {
    foreach ($Name in @('b2lufs_hw.h', 'b2lufs_resources.c', 'b2lufs_storport.c')) {
        Copy-Item -LiteralPath (Join-Path $SourceDir $Name) -Destination (Join-Path $StageRoot $Name) -Force
    }

    $Cl = Join-Path $MsvcArm64Bin 'cl.exe'
    $Link = Join-Path $MsvcArm64Bin 'link.exe'
    # /kernel WDK'nin cekirdek derleme tanimlarini kendisi kurar. _KERNEL_MODE
    # rezerve bir MSVC makrosudur; elle /D ile vermek C4117'yi /WX altinda
    # derleme hatasina cevirir.
    # WDK 28000'in storport.h basligi, ARM64'e zorunlu hizalama eklenmis
    # yapilarinda C4324 uretir. Bu bir baslik/ABI bildirimi, bizim surucu
    # kaynak kodumuzun uyarisi degil; /WX korunurken yalniz onu sustururuz.
    $CompileFlags = "/nologo /c /TC /kernel /Zl /GS- /W4 /WX /wd4324 /O1 /D_WIN32_WINNT=0x0A00 /D_ARM64_=1 /I`"$WdkInclude\km`" /I`"$WdkInclude\shared`" /I`"$WdkInclude\ucrt`" /I`"$WdkInclude\um`""
    $BuildLines = @(
        '@echo off',
        'setlocal EnableExtensions DisableDelayedExpansion',
        "cd /d `"$StageRoot`"",
        "call `"$($Vcvars.FullName)`" >nul",
        'if errorlevel 1 exit /b 1',
        "`"$Cl`" $CompileFlags /Fo`"b2lufs_resources.obj`" `"b2lufs_resources.c`"",
        'if errorlevel 1 exit /b 1',
        "`"$Cl`" $CompileFlags /Fo`"b2lufs_storport.obj`" `"b2lufs_storport.c`"",
        'if errorlevel 1 exit /b 1',
        "`"$Link`" /nologo /kernel /driver /subsystem:native /entry:DriverEntry /machine:arm64 /nodefaultlib /libpath:`"$WdkLib`" /out:`"b2lufs.sys`" `"b2lufs_resources.obj`" `"b2lufs_storport.obj`" storport.lib ntoskrnl.lib",
        'exit /b %errorlevel%'
    )
    $BuildCmd = Join-Path $StageRoot 'build-b2lufs-arm64.cmd'
    [IO.File]::WriteAllText($BuildCmd, (($BuildLines -join "`r`n") + "`r`n"), [System.Text.Encoding]::ASCII)

    Write-Host ''
    Write-Host '==> B2LUFS Faz 0 derleniyor (yerel Windows MSVC ARM64 + WDK)' -ForegroundColor Cyan
    Write-Host "  WDK    : $WdkVersion"
    Write-Host "  vcvars : $($Vcvars.FullName)"
    Write-Host "  kaynak : $SourceDir"
    Write-Host "  gecici : $StageRoot"

    # PowerShell betigi bir WSL UNC yolundan acilmis olabilir. cmd.exe, o UNC
    # yolu calisma dizini olarak devralirsa komutu Windows klasorune dusurur.
    # Yerel staging dizinine once gecmek bu siniri tamamen ortadan kaldirir.
    Push-Location -LiteralPath $StageRoot
    try {
        & $env:ComSpec '/d' '/c' $BuildCmd
        if ($LASTEXITCODE -ne 0) {
            Fail "MSVC derlemesi basarisiz oldu (cmd.exe cikis kodu $LASTEXITCODE)"
        }
    }
    finally {
        Pop-Location
    }

    $BuiltSys = Join-Path $StageRoot 'b2lufs.sys'
    if (-not (Test-Path -LiteralPath $BuiltSys -PathType Leaf)) {
        Fail 'link.exe basarili dondu fakat b2lufs.sys yok'
    }

    $Bytes = [IO.File]::ReadAllBytes($BuiltSys)
    if ($Bytes.Length -lt 0x40 -or $Bytes[0] -ne 0x4D -or $Bytes[1] -ne 0x5A) {
        Fail 'b2lufs.sys MZ/PE imzasi tasimiyor'
    }
    $PeOffset = [BitConverter]::ToInt32($Bytes, 0x3C)
    if ($PeOffset -lt 0 -or $PeOffset + 6 -gt $Bytes.Length -or
        $Bytes[$PeOffset] -ne 0x50 -or $Bytes[$PeOffset + 1] -ne 0x45 -or
        [BitConverter]::ToUInt16($Bytes, $PeOffset + 4) -ne 0xAA64) {
        Fail 'b2lufs.sys dogrulanmis ARM64 PE surucusu degil'
    }

    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    $Destination = Join-Path $OutDir 'b2lufs.sys'
    Copy-Item -LiteralPath $BuiltSys -Destination $Destination -Force
    $Hash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
    Write-Host "  yazildi: $Destination"
    Write-Host "  SHA256 : $Hash"
}
finally {
    if (Test-Path -LiteralPath $StageRoot) {
        # Virüs tarayıcısı veya kapanan cl.exe kısa süreyle dosya tutabilir.
        # Birkaç kısa deneme normalde kilidi bırakır. Temizleme başarısızlığı
        # hiçbir zaman asıl derleme/link hatasını gölgelememelidir.
        $Removed = $false
        for ($Attempt = 1; $Attempt -le 5; ++$Attempt) {
            try {
                Remove-Item -LiteralPath $StageRoot -Recurse -Force -ErrorAction Stop
                $Removed = $true
                break
            }
            catch {
                if ($Attempt -lt 5) {
                    Start-Sleep -Milliseconds 250
                }
            }
        }
        if (-not $Removed) {
            Write-Warning "Gecici derleme dizini temizlenemedi: $StageRoot"
        }
    }
}
