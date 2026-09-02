<#
 .SYNOPSIS
   Builds the Beyond2LTE KMDF/URS USB driver and control tool for ARM64.

 .DESCRIPTION
   Run from WSL through powershell.exe.  Sources are staged in a native
   Windows temporary directory before MSVC/WDK tools are invoked.  Catalog,
   test signing and WinPE injection are handled by tools_b2lusb.sh.
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

function Fail([string]$Message) { throw "HATA: $Message" }

function Resolve-Directory([string]$Path, [string]$Description) {
    if ([string]::IsNullOrWhiteSpace($Path) -or
        -not (Test-Path -LiteralPath $Path -PathType Container)) {
        Fail "$Description yok: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Assert-Arm64Pe([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { Fail "$Description uretilmedi" }
    $Bytes = [IO.File]::ReadAllBytes($Path)
    if ($Bytes.Length -lt 0x40 -or $Bytes[0] -ne 0x4D -or $Bytes[1] -ne 0x5A) {
        Fail "$Description MZ/PE imzasi tasimiyor"
    }
    $PeOffset = [BitConverter]::ToInt32($Bytes, 0x3C)
    if ($PeOffset -lt 0 -or $PeOffset + 6 -gt $Bytes.Length -or
        [BitConverter]::ToUInt16($Bytes, $PeOffset + 4) -ne 0xAA64) {
        Fail "$Description ARM64 PE degil"
    }
}

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Join-Path $PSScriptRoot '..\..'
}
$ProjectRoot = Resolve-Directory $ProjectRoot 'Proje dizini'
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $ProjectRoot 'out\b2lusb'
}
$SourceDir = Resolve-Directory (Join-Path $ProjectRoot 'windows\b2lusb') 'B2LUSB kaynak dizini'

if ([string]::IsNullOrWhiteSpace($KitsRoot)) {
    $KitsRoot = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits\10'
}
$KitsRoot = Resolve-Directory $KitsRoot 'Windows Kits dizini'
$IncludeRoot = Resolve-Directory (Join-Path $KitsRoot 'Include') 'WDK Include dizini'

if ([string]::IsNullOrWhiteSpace($WdkVersion)) {
    $Candidate = Get-ChildItem -LiteralPath $IncludeRoot -Directory |
        Where-Object {
            (Test-Path -LiteralPath (Join-Path $_.FullName 'km\ntddk.h') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'km\urs\1.0\UrsCx.h') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $KitsRoot ("Lib\{0}\km\arm64\urs\1.0\UrsCxStub.lib" -f $_.Name)) -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $KitsRoot ("Lib\{0}\km\arm64\ntoskrnl.lib" -f $_.Name)) -PathType Leaf)
        } |
        Where-Object { $_.Name -match '^\d+(\.\d+){1,3}$' } |
        Sort-Object { [Version]$_.Name } -Descending |
        Select-Object -First 1
    if ($null -eq $Candidate) {
        Fail 'km\urs\1.0\UrsCx.h ve ARM64 urs\1.0\UrsCxStub.lib iceren Windows WDK bulunamadi'
    }
    $WdkVersion = $Candidate.Name
}

$WdkInclude = Resolve-Directory (Join-Path $KitsRoot ("Include\{0}" -f $WdkVersion)) 'Secilen WDK Include dizini'
foreach ($Header in @('km\ntddk.h', 'km\urs\1.0\UrsCx.h', 'um\setupapi.h')) {
    if (-not (Test-Path -LiteralPath (Join-Path $WdkInclude $Header) -PathType Leaf)) {
        Fail "$Header yok: $WdkInclude"
    }
}
$WdkLib = Resolve-Directory (Join-Path $KitsRoot ("Lib\{0}\km\arm64" -f $WdkVersion)) 'WDK ARM64 kernel kutuphane dizini'
foreach ($Library in @('ntoskrnl.lib', 'hal.lib', 'libcntpr.lib', 'BufferOverflowFastFailK.lib')) {
    if (-not (Test-Path -LiteralPath (Join-Path $WdkLib $Library) -PathType Leaf)) {
        Fail "$Library yok: $WdkLib"
    }
}
$UrsLib = Resolve-Directory (Join-Path $WdkLib 'urs\1.0') 'WDK ARM64 URS kutuphane dizini'
if (-not (Test-Path -LiteralPath (Join-Path $UrsLib 'UrsCxStub.lib') -PathType Leaf)) {
    Fail "UrsCxStub.lib yok: $UrsLib"
}
$WdkUserLib = Resolve-Directory (Join-Path $KitsRoot ("Lib\{0}\um\arm64" -f $WdkVersion)) 'SDK ARM64 kullanici kutuphane dizini'
foreach ($Library in @('kernel32.lib', 'setupapi.lib')) {
    if (-not (Test-Path -LiteralPath (Join-Path $WdkUserLib $Library) -PathType Leaf)) {
        Fail "$Library yok: $WdkUserLib"
    }
}
$KmdfInclude = Resolve-Directory (Join-Path $KitsRoot 'Include\wdf\kmdf\1.15') 'KMDF 1.15 Include dizini'
$KmdfLib = Resolve-Directory (Join-Path $KitsRoot 'Lib\wdf\kmdf\arm64\1.15') 'KMDF 1.15 ARM64 kutuphane dizini'
foreach ($Library in @('WdfDriverEntry.lib', 'WdfLdr.lib')) {
    if (-not (Test-Path -LiteralPath (Join-Path $KmdfLib $Library) -PathType Leaf)) {
        Fail "$Library yok: $KmdfLib"
    }
}

$VcvarsCandidates = @()
foreach ($ProgramFilesRoot in @(
        [Environment]::GetFolderPath('ProgramFiles'),
        [Environment]::GetFolderPath('ProgramFilesX86'))) {
    if ([string]::IsNullOrWhiteSpace($ProgramFilesRoot)) { continue }
    $Vs = Join-Path $ProgramFilesRoot 'Microsoft Visual Studio'
    if (Test-Path -LiteralPath $Vs -PathType Container) {
        $VcvarsCandidates += Get-ChildItem -LiteralPath $Vs -Recurse -Filter 'vcvarsamd64_arm64.bat' -File
    }
}
$Vcvars = $VcvarsCandidates | Sort-Object FullName -Descending | Select-Object -First 1
if ($null -eq $Vcvars) {
    Fail 'vcvarsamd64_arm64.bat yok; Visual Studio C++ ARM64 build tools bilesenini kur'
}

$VsRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $Vcvars.FullName)))
$ToolsetRoot = Resolve-Directory (Join-Path $VsRoot 'VC\Tools\MSVC') 'MSVC toolset dizini'
$Toolset = Get-ChildItem -LiteralPath $ToolsetRoot -Directory |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\arm64\cl.exe') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\arm64\link.exe') -PathType Leaf)
    } |
    Where-Object { $_.Name -match '^\d+(\.\d+){1,3}$' } |
    Sort-Object { [Version]$_.Name } -Descending |
    Select-Object -First 1
if ($null -eq $Toolset) { Fail 'MSVC ARM64 cl.exe/link.exe bulunamadi' }
$Arm64Bin = Join-Path $Toolset.FullName 'bin\Hostx64\arm64'

$StageRoot = Join-Path ([IO.Path]::GetTempPath()) ("b2lusb-arm64-{0}" -f [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $StageRoot -Force | Out-Null

try {
    foreach ($Name in @('b2lusb.h', 'b2lusb_public.h', 'b2lusb.c', 'b2lusb_hw.c', 'b2lusbmon.c', 'b2lusbctl.c')) {
        Copy-Item -LiteralPath (Join-Path $SourceDir $Name) -Destination (Join-Path $StageRoot $Name) -Force
    }

    $Compiler = Join-Path $Arm64Bin 'cl.exe'
    $Linker = Join-Path $Arm64Bin 'link.exe'
    $Flags = "/nologo /c /TC /kernel /Zl /GS- /W4 /WX /wd4324 /O1 /D_WIN32_WINNT=0x0A00 /D_ARM64_=1 /DKMDF_VERSION_MAJOR=1 /DKMDF_VERSION_MINOR=15 /I`"$KmdfInclude`" /I`"$WdkInclude\km`" /I`"$WdkInclude\shared`" /I`"$WdkInclude\ucrt`" /I`"$WdkInclude\um`""
    $UserFlags = "/nologo /TC /O1 /W4 /WX /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /I`"$WdkInclude\um`" /I`"$WdkInclude\shared`" /I`"$WdkInclude\ucrt`""
    $Lines = @(
        '@echo off',
        'setlocal EnableExtensions DisableDelayedExpansion',
        "cd /d `"$StageRoot`"",
        "call `"$($Vcvars.FullName)`" >nul",
        'if errorlevel 1 exit /b 1',
        "`"$Compiler`" $Flags /Fo`"b2lusb.obj`" `"b2lusb.c`"",
        'if errorlevel 1 exit /b 1',
        "`"$Compiler`" $Flags /Fo`"b2lusb_hw.obj`" `"b2lusb_hw.c`"",
        'if errorlevel 1 exit /b 1',
        "`"$Linker`" /nologo /kernel /driver /subsystem:native /entry:FxDriverEntry /machine:arm64 /nodefaultlib /libpath:`"$WdkLib`" /libpath:`"$UrsLib`" /libpath:`"$KmdfLib`" /out:`"b2lusb.sys`" `"b2lusb.obj`" `"b2lusb_hw.obj`" WdfDriverEntry.lib WdfLdr.lib UrsCxStub.lib libcntpr.lib ntoskrnl.lib hal.lib BufferOverflowFastFailK.lib",
        'if errorlevel 1 exit /b 1',
        "`"$Compiler`" $Flags /Fo`"b2lusbmon.obj`" `"b2lusbmon.c`"",
        'if errorlevel 1 exit /b 1',
        "`"$Linker`" /nologo /kernel /driver /subsystem:native /entry:FxDriverEntry /machine:arm64 /nodefaultlib /libpath:`"$WdkLib`" /libpath:`"$KmdfLib`" /out:`"b2lusbmon.sys`" `"b2lusbmon.obj`" WdfDriverEntry.lib WdfLdr.lib libcntpr.lib ntoskrnl.lib hal.lib BufferOverflowFastFailK.lib",
        'if errorlevel 1 exit /b 1',
        "`"$Compiler`" $UserFlags /Fo`"b2lusbctl.obj`" `"b2lusbctl.c`" /Fe:`"b2lusbctl.exe`" /link /machine:arm64 /subsystem:console /libpath:`"$WdkUserLib`" kernel32.lib setupapi.lib",
        'exit /b %errorlevel%'
    )
    $BuildCmd = Join-Path $StageRoot 'build-b2lusb-arm64.cmd'
    [IO.File]::WriteAllText($BuildCmd, (($Lines -join "`r`n") + "`r`n"), [Text.Encoding]::ASCII)

    Write-Host ''
    Write-Host '==> B2LUSB derleniyor (Windows MSVC ARM64 + KMDF 1.15 + URS)' -ForegroundColor Cyan
    Write-Host "  WDK    : $WdkVersion"
    Write-Host "  vcvars : $($Vcvars.FullName)"
    Push-Location -LiteralPath $StageRoot
    try {
        & $env:ComSpec '/d' '/c' $BuildCmd
        if ($LASTEXITCODE -ne 0) { Fail "MSVC derlemesi basarisiz (kod $LASTEXITCODE)" }
    }
    finally { Pop-Location }

    $BuiltDriver = Join-Path $StageRoot 'b2lusb.sys'
    $BuiltMonitor = Join-Path $StageRoot 'b2lusbmon.sys'
    $BuiltControl = Join-Path $StageRoot 'b2lusbctl.exe'
    Assert-Arm64Pe $BuiltDriver 'b2lusb.sys'
    Assert-Arm64Pe $BuiltMonitor 'b2lusbmon.sys'
    Assert-Arm64Pe $BuiltControl 'b2lusbctl.exe'

    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    foreach ($Item in @('b2lusb.sys', 'b2lusbmon.sys', 'b2lusbctl.exe')) {
        $Source = Join-Path $StageRoot $Item
        $Destination = Join-Path $OutDir $Item
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
        Write-Host "  yazildi: $Destination"
        Write-Host "  SHA256 : $((Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash)"
    }
}
finally {
    if (Test-Path -LiteralPath $StageRoot) {
        Remove-Item -LiteralPath $StageRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
