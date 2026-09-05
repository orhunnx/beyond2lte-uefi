[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [string]$WimPath,
    [ValidateSet("Usb", "Touch", "UsbTouch", "Ufs", "TouchUfs")]
    [string]$Package,
    [string]$PreparedRoot,
    [switch]$DisableUsb
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Test-B2LAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Write-B2LStageLog {
    param([string]$Message)

    Write-Host $Message
    if ($script:LogPath) {
        Add-Content -LiteralPath $script:LogPath -Value $Message -Encoding UTF8
    }
}

function Invoke-B2LReg {
    param(
        [string[]]$Arguments,
        [switch]$ReturnOutput
    )

    # Windows PowerShell 5.1 native stderr'i ErrorRecord'a cevirir. Scriptin
    # genel Stop tercihi reg.exe cikis kodunu okumadan once kontrolu kesmesin.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & reg.exe @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    foreach ($line in $output) {
        Write-B2LStageLog ([string]$line)
    }
    if ($exitCode -ne 0) {
        throw "reg.exe basarisiz (exit=$exitCode): $($Arguments -join ' ')"
    }
    if ($ReturnOutput) {
        return $output
    }
}

function Dismount-B2LRegistryHive {
    param([string]$HiveName)

    $lastOutput = @()
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        [GC]::Collect()
        [GC]::WaitForPendingFinalizers()

        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            $lastOutput = @(& reg.exe unload $HiveName 2>&1)
            $exitCode = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $previousPreference
        }

        if ($exitCode -eq 0) {
            Write-B2LStageLog "Offline registry hive kapatildi: $HiveName"
            return
        }
        if ($attempt -lt 5) {
            Start-Sleep -Milliseconds (200 * $attempt)
        }
    }

    $detail = ($lastOutput | ForEach-Object { [string]$_ }) -join " | "
    throw "Offline registry hive kapatilamadi: $HiveName ($detail)"
}

function Get-B2LRequiredRegistryValue {
    param(
        [Microsoft.Win32.RegistryKey]$Key,
        [string]$Name
    )

    if ($Key.GetValueNames() -notcontains $Name) {
        throw "Offline registry degeri yok: $($Key.Name)\\$Name"
    }
    return $Key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
}

function Set-B2LVhfOfflineService {
    param([string]$MountPath)

    $vhfDriver = Join-Path $MountPath "Windows\System32\drivers\vhf.sys"
    if (-not (Test-Path -LiteralPath $vhfDriver -PathType Leaf)) {
        throw "Touch offline kurulumu icin inbox vhf.sys yok: $vhfDriver"
    }

    $systemHive = Join-Path $MountPath "Windows\System32\config\SYSTEM"
    $hiveName = "HKLM\B2LOFFLINE_$PID"
    $loaded = $false
    try {
        Invoke-B2LReg -Arguments @("load", $hiveName, $systemHive)
        $loaded = $true
        $selectOutput = Invoke-B2LReg -Arguments @("query", "$hiveName\Select", "/v", "Current") -ReturnOutput
        $selectText = $selectOutput -join "`n"
        if ($selectText -notmatch "0x([0-9a-fA-F]+)") {
            throw "Offline SYSTEM Select\\Current okunamadi"
        }
        $current = [Convert]::ToInt32($Matches[1], 16)
        $controlSet = "ControlSet{0:D3}" -f $current
        $serviceKey = "$hiveName\$controlSet\Services\vhf"

        Invoke-B2LReg -Arguments @("add", $serviceKey, "/v", "Type", "/t", "REG_DWORD", "/d", "1", "/f")
        Invoke-B2LReg -Arguments @("add", $serviceKey, "/v", "Start", "/t", "REG_DWORD", "/d", "3", "/f")
        Invoke-B2LReg -Arguments @("add", $serviceKey, "/v", "ErrorControl", "/t", "REG_DWORD", "/d", "1", "/f")
        Invoke-B2LReg -Arguments @("add", $serviceKey, "/v", "ImagePath", "/t", "REG_EXPAND_SZ", "/d", "\SystemRoot\System32\drivers\vhf.sys", "/f")
        Invoke-B2LReg -Arguments @("add", $serviceKey, "/v", "DisplayName", "/t", "REG_SZ", "/d", "Virtual HID Framework (VHF) Driver", "/f")
        Write-B2LStageLog "Offline VHF service hazir: $controlSet\\Services\\vhf"
    }
    finally {
        if ($loaded) {
            Dismount-B2LRegistryHive -HiveName $hiveName
        }
    }
}

function Test-B2LUfsOfflineBootService {
    param(
        [string]$MountPath,
        [string]$DriverRoot
    )

    $sourceDriver = Join-Path $DriverRoot "b2lufs.sys"
    $installedDriver = Join-Path $MountPath "Windows\System32\drivers\b2lufs.sys"
    if (-not (Test-Path -LiteralPath $installedDriver -PathType Leaf)) {
        throw "B2LUFS boot SYS offline Windows dizininde yok: $installedDriver"
    }
    $sourceHash = (Get-FileHash -LiteralPath $sourceDriver -Algorithm SHA256).Hash
    $installedHash = (Get-FileHash -LiteralPath $installedDriver -Algorithm SHA256).Hash
    if ($sourceHash -ne $installedHash) {
        throw "B2LUFS boot SYS hash'i paket girdisinden farkli"
    }

    $systemHive = Join-Path $MountPath "Windows\System32\config\SYSTEM"
    $hiveLeaf = "B2LUFSOFFLINE_$PID"
    $hiveName = "HKLM\$hiveLeaf"
    $loaded = $false
    $baseKey = $null
    $offlineHive = $null
    try {
        Invoke-B2LReg -Arguments @("load", $hiveName, $systemHive)
        $loaded = $true

        # PowerShell Registry provider'i yuklu hive uzerinde cache'lenmis handle
        # birakabiliyor. Anahtarlari .NET ile acip deterministik olarak kapat;
        # aksi halde reg unload ve DISM dismount ACCESS_DENIED olur.
        $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
            [Microsoft.Win32.RegistryHive]::LocalMachine,
            [Microsoft.Win32.RegistryView]::Default
        )
        $offlineHive = $baseKey.OpenSubKey($hiveLeaf)
        if ($null -eq $offlineHive) {
            throw "B2LUFS offline SYSTEM hive acilamadi: $hiveLeaf"
        }

        $selectKey = $offlineHive.OpenSubKey("Select")
        if ($null -eq $selectKey) {
            throw "B2LUFS offline SYSTEM Select anahtari yok"
        }
        try {
            $controlSetNumbers = @(
                [int](Get-B2LRequiredRegistryValue -Key $selectKey -Name "Current"),
                [int](Get-B2LRequiredRegistryValue -Key $selectKey -Name "Default")
            )
            if ($selectKey.GetValueNames() -contains "LastKnownGood") {
                $controlSetNumbers += [int]$selectKey.GetValue("LastKnownGood")
            }
        }
        finally {
            $selectKey.Close()
        }

        $controlSetNumbers = @($controlSetNumbers | Where-Object { $_ -gt 0 } | Sort-Object -Unique)
        if ($controlSetNumbers.Count -eq 0) {
            throw "B2LUFS offline SYSTEM etkin control set icermiyor"
        }

        foreach ($controlSetNumber in $controlSetNumbers) {
            $controlSet = "ControlSet{0:D3}" -f $controlSetNumber
            $service = $offlineHive.OpenSubKey("$controlSet\Services\b2lufs")
            if ($null -eq $service) {
                throw "B2LUFS offline servis kaydi yok: $controlSet\Services\b2lufs"
            }
            try {
                $type = [int](Get-B2LRequiredRegistryValue -Key $service -Name "Type")
                $start = [int](Get-B2LRequiredRegistryValue -Key $service -Name "Start")
                $errorControl = [int](Get-B2LRequiredRegistryValue -Key $service -Name "ErrorControl")
                $group = [string](Get-B2LRequiredRegistryValue -Key $service -Name "Group")
                $imagePath = ([string](Get-B2LRequiredRegistryValue -Key $service -Name "ImagePath")).Replace('/', '\')

                if ($type -ne 1) {
                    throw "B2LUFS offline Type boot surucusu degil: $controlSet=$type"
                }
                if ($start -ne 0) {
                    throw "B2LUFS offline Start=0 degil: $controlSet=$start"
                }
                if ($errorControl -ne 3) {
                    throw "B2LUFS offline ErrorControl=3 degil: $controlSet=$errorControl"
                }
                if ($group -cne "SCSI Miniport") {
                    throw "B2LUFS offline load-order grubu gecersiz: $controlSet='$group'"
                }
                if ($imagePath -notmatch '(?i)^(\\SystemRoot\\)?System32\\drivers\\b2lufs\.sys$') {
                    throw "B2LUFS offline ImagePath gecersiz: $controlSet='$imagePath'"
                }

                $override = $service.OpenSubKey("StartOverride")
                if ($null -ne $override) {
                    try {
                        foreach ($valueName in $override.GetValueNames()) {
                            $overrideValue = [int]$override.GetValue($valueName)
                            if ($overrideValue -ne 0) {
                                throw "B2LUFS StartOverride boot-start'i engelliyor: $controlSet/$valueName=$overrideValue"
                            }
                        }
                    }
                    finally {
                        $override.Close()
                    }
                }

                Write-B2LStageLog "Offline B2LUFS boot servisi dogrulandi: $controlSet\\Services\\b2lufs Start=0 Group=SCSI Miniport ErrorControl=3"
            }
            finally {
                $service.Close()
            }
        }

        Write-B2LStageLog "Offline B2LUFS SYS hash'i paketle ayni: $($installedHash.ToLowerInvariant())"
    }
    finally {
        if ($null -ne $offlineHive) { $offlineHive.Close() }
        if ($null -ne $baseKey) { $baseKey.Close() }
        if ($loaded) {
            Dismount-B2LRegistryHive -HiveName $hiveName
        }
    }
}

function Test-B2LRuntimeFiles {
    param([string]$MountPath, [object]$Record, [string]$Stage, [switch]$RequireMatch)
    $different = @()
    foreach ($entry in $Record.files.PSObject.Properties) {
        # Manifest keys must be the fixed runtime paths, never arbitrary paths.
        if ($entry.Name -notmatch '^/Windows/(System32/(ntoskrnl\.exe|drivers/(UcmCx|Urscx01000|Ufx01000|ufxsynopsys)\.sys)|INF/ufxsynopsys\.inf)$') {
            throw "Beklenmeyen runtime manifest yolu: $($entry.Name)"
        }
        $path = Join-Path $MountPath $entry.Name.TrimStart('/').Replace('/', '\')
        $actual = if (Test-Path -LiteralPath $path -PathType Leaf) {
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        } else { "MISSING" }
        Write-B2LStageLog "RUNTIME [$Stage] $($entry.Name) SHA256=$actual"
        if ($actual -ne $entry.Value) { $different += $entry.Name }
    }
    if ($Record.files.PSObject.Properties.Name.Count -ne 6) {
        throw "Runtime manifest alti dosya icermeli"
    }
    if ($RequireMatch -and $different.Count -gt 0) {
        throw "Runtime degisti [$Stage]: $($different -join ', '); gecici WIM kaydedilmeyecek"
    }
}

function Invoke-B2LOfflineServicing {
    param(
        [string]$WorkRoot,
        [string]$SelectedPackage,
        [switch]$DisableUsb
    )

    Import-Module Dism -ErrorAction Stop
    $localWim = Join-Path $WorkRoot "boot.wim"
    $driverRoot = Join-Path $WorkRoot "drivers"
    $mountRoot = Join-Path $WorkRoot "mount"
    New-Item -ItemType Directory -Path $mountRoot -Force | Out-Null

    $hasUsb = $SelectedPackage -in @("Usb", "UsbTouch")
    $hasTouch = $SelectedPackage -in @("Touch", "UsbTouch", "TouchUfs")
    $hasUfs = $SelectedPackage -in @("Ufs", "TouchUfs")
    if ($DisableUsb -and -not $hasUfs) {
        throw "DisableUsb yalniz Ufs iceren offline stage ile kullanilabilir"
    }

    if ($SelectedPackage -eq "Usb") {
        $requiredInfs = @("b2lmax77705.inf", "b2lusb-host.inf", "b2lusb-function.inf", "b2lusb.inf")
        $markerDirectories = @("Windows\B2LUSB")
    }
    elseif ($SelectedPackage -eq "Touch") {
        $requiredInfs = @("b2ltouch.inf")
        $markerDirectories = @("Windows\B2LTOUCH")
    }
    elseif ($SelectedPackage -eq "UsbTouch") {
        $requiredInfs = @("b2lmax77705.inf", "b2lusb-host.inf", "b2lusb-function.inf", "b2lusb.inf", "b2ltouch.inf")
        $markerDirectories = @("Windows\B2LUSB", "Windows\B2LTOUCH")
    }
    elseif ($SelectedPackage -eq "TouchUfs") {
        $requiredInfs = @("b2ltouch.inf", "b2lufs.inf")
        $markerDirectories = @("Windows\B2LTOUCH", "Windows\B2LUFS")
    }
    else {
        $requiredInfs = @("b2lufs.inf")
        $markerDirectories = @("Windows\B2LUFS")
    }
    $removeInfs = @($requiredInfs)
    if ($DisableUsb) {
        $removeInfs += @("b2lmax77705.inf", "b2lusb-host.inf", "b2lusb-function.inf", "b2lusb.inf")
    }

    $mounted = $false
    try {
        Write-B2LStageLog "DISM mount: $SelectedPackage"
        Mount-WindowsImage -ImagePath $localWim -Index 1 -Path $mountRoot -CheckIntegrity | Out-Null
        $mounted = $true

        $runtime = $null
        $runtimeManifest = Join-Path $WorkRoot "experimental-ufx.json"
        if ($hasUsb -and (Test-Path -LiteralPath $runtimeManifest)) {
            $runtime = Get-Content -LiteralPath $runtimeManifest -Raw | ConvertFrom-Json
            if ($runtime.profile -ne "experimental-ufx-overlay-v2") {
                throw "Bilinmeyen runtime paket profili"
            }
            $wimManifest = Join-Path $mountRoot "Windows\B2LUSB\experimental-ufx.json"
            if ((Get-FileHash -LiteralPath $runtimeManifest).Hash -ne (Get-FileHash -LiteralPath $wimManifest).Hash) {
                throw "Paket/WIM runtime manifestleri farkli"
            }
            Test-B2LRuntimeFiles -MountPath $mountRoot -Record $runtime -Stage "DISM-oncesi" -RequireMatch
        }

        $installed = @(Get-WindowsDriver -Path $mountRoot -All)
        foreach ($driver in $installed) {
            $originalName = [IO.Path]::GetFileName([string]$driver.OriginalFileName)
            if ($removeInfs -contains $originalName) {
                Write-B2LStageLog "Eski offline paket kaldiriliyor: $originalName ($($driver.Driver))"
                Remove-WindowsDriver -Path $mountRoot -Driver $driver.Driver | Out-Null
                if ($null -ne $runtime) {
                    Test-B2LRuntimeFiles -MountPath $mountRoot -Record $runtime -Stage "Remove-$originalName"
                }
            }
        }

        if ($DisableUsb) {
            $usbTestRoot = Join-Path $mountRoot "Windows\B2LUSB"
            if (Test-Path -LiteralPath $usbTestRoot) {
                Remove-Item -LiteralPath $usbTestRoot -Recurse -Force
            }
            Write-B2LStageLog "B2LUSB DriverStore paketleri ve runtime test marker'lari devre disi birakildi."
        }

        foreach ($infName in $requiredInfs) {
            $infPath = Join-Path $driverRoot $infName
            if (-not (Test-Path -LiteralPath $infPath -PathType Leaf)) {
                throw "Offline INF bulunamadi: $infPath"
            }
            Write-B2LStageLog "Offline DriverStore ekleniyor: $infName"
            Add-WindowsDriver -Path $mountRoot -Driver $infPath -ForceUnsigned | Out-Null
            if ($null -ne $runtime) {
                Test-B2LRuntimeFiles -MountPath $mountRoot -Record $runtime -Stage "Add-$infName"
            }
        }

        if ($hasTouch) {
            Set-B2LVhfOfflineService -MountPath $mountRoot
        }

        if ($hasUfs) {
            Test-B2LUfsOfflineBootService -MountPath $mountRoot -DriverRoot $driverRoot
        }

        $verified = @(Get-WindowsDriver -Path $mountRoot -All)
        foreach ($infName in $requiredInfs) {
            $match = @($verified | Where-Object {
                [IO.Path]::GetFileName([string]$_.OriginalFileName) -ieq $infName
            })
            if ($match.Count -ne 1) {
                throw "Offline DriverStore dogrulamasi basarisiz: $infName sayi=$($match.Count)"
            }
            Write-B2LStageLog "Offline DriverStore dogrulandi: $infName -> $($match[0].Driver)"
        }

        if ($null -ne $runtime) {
            # Verify the registered platform package really owns the selected SYS.
            $storeRoot = Join-Path $mountRoot "Windows\System32\DriverStore\FileRepository"
            $sourceInfHash = (Get-FileHash -LiteralPath (Join-Path $driverRoot "b2lusb-function.inf")).Hash
            $stores = @(Get-ChildItem -LiteralPath $storeRoot -Filter "b2lusb-function.inf" -Recurse -File |
                Where-Object { (Get-FileHash -LiteralPath $_.FullName).Hash -eq $sourceInfHash })
            if ($stores.Count -ne 1) { throw "Kaynak function DriverStore paketi belirsiz/eksik" }
            $storeSys = Join-Path $stores[0].DirectoryName "ufxsynopsys.sys"
            $expected = $runtime.files.'/Windows/System32/drivers/ufxsynopsys.sys'
            if ((Get-FileHash -LiteralPath $storeSys).Hash -ne $expected) {
                throw "Function DriverStore SYS kaynaktan farkli"
            }
            Test-B2LRuntimeFiles -MountPath $mountRoot -Record $runtime -Stage "DISM-sonrasi" -RequireMatch
        }

        foreach ($markerDirectory in $markerDirectories) {
            $markerRoot = Join-Path $mountRoot $markerDirectory
            New-Item -ItemType Directory -Path $markerRoot -Force | Out-Null
            $marker = @(
                "B2L_OFFLINE_DRIVERSTORE=1"
                "B2L_OFFLINE_PACKAGE=$($SelectedPackage.ToUpperInvariant())"
                "B2L_OFFLINE_INFS=$($requiredInfs -join ',')"
            )
            if ($hasUfs -and $markerDirectory -eq "Windows\B2LUFS") {
                $marker += @(
                    "B2L_BOOT_START=1"
                    "B2L_LOAD_ORDER_GROUP=SCSI Miniport"
                    "B2L_ERROR_CONTROL=3"
                )
                if ($DisableUsb) {
                    $marker += "B2L_USB_TESTS=DISABLED"
                }
            }
            $marker | Set-Content -LiteralPath (Join-Path $markerRoot "offline-driverstore.txt") -Encoding ASCII
        }

        Write-B2LStageLog "DISM commit: $SelectedPackage"
        Dismount-WindowsImage -Path $mountRoot -Save -CheckIntegrity | Out-Null
        $mounted = $false
    }
    catch {
        Write-B2LStageLog "HATA: $($_.Exception.Message)"
        throw
    }
    finally {
        if ($mounted) {
            Write-B2LStageLog "Basarisiz servisleme geri aliniyor."
            Dismount-WindowsImage -Path $mountRoot -Discard -ErrorAction SilentlyContinue | Out-Null
        }
    }
}

if ($PreparedRoot) {
    $script:LogPath = Join-Path $PreparedRoot "offline-driverstore.log"
    if (-not (Test-B2LAdministrator)) {
        Write-B2LStageLog "HATA: DISM offline servisleme yonetici yetkisi gerektirir."
        exit 1
    }
    try {
        Invoke-B2LOfflineServicing -WorkRoot $PreparedRoot -SelectedPackage $Package -DisableUsb:$DisableUsb
        exit 0
    }
    catch {
        exit 1
    }
}

if (-not $ProjectRoot -or -not $WimPath -or -not $Package) {
    throw "ProjectRoot, WimPath ve Package zorunludur."
}
if (-not (Test-Path -LiteralPath $WimPath -PathType Leaf)) {
    throw "boot.wim bulunamadi: $WimPath"
}

$usbSourceDirectory = Join-Path $ProjectRoot "out\b2lusb"
$touchSourceDirectory = Join-Path $ProjectRoot "out\b2ltouch"
$ufsSourceDirectory = Join-Path $ProjectRoot "out\b2lufs"
$usbFiles = @(
    "b2lmax77705.inf", "b2lusb-host.inf", "b2lusb-function.inf", "b2lusb.inf",
    "b2lmax77705.sys", "b2lusb.sys", "b2lusbmon.sys", "b2lusb.cat"
)
$touchFiles = @("b2ltouch.inf", "b2ltouch.sys", "b2ltouch.cat")
$ufsFiles = @("b2lufs.inf", "b2lufs.sys", "b2lufs.cat")
$packagedSynopsys = $false
if ($Package -in @("Usb", "UsbTouch")) {
    $functionInf = Get-Content -LiteralPath (Join-Path $usbSourceDirectory "b2lusb-function.inf") -Raw
    $packagedSynopsys = ($functionInf -split "`r?`n")[0] -eq "; B2L_PACKAGED_SYNOPSYS=1"
    if ($packagedSynopsys) { $usbFiles += "ufxsynopsys.sys" }
}

$workRoot = Join-Path $env:TEMP ("B2LWinPE-{0}" -f [Guid]::NewGuid().ToString("N"))
$driverRoot = Join-Path $workRoot "drivers"
$script:LogPath = Join-Path $workRoot "offline-driverstore.log"
New-Item -ItemType Directory -Path $driverRoot -Force | Out-Null
try {
    $localScript = Join-Path $workRoot "stage-winpe-driverstore.ps1"
    Copy-Item -LiteralPath $PSCommandPath -Destination $localScript -Force
    $localWim = Join-Path $workRoot "boot.wim"
    Copy-Item -LiteralPath $WimPath -Destination $localWim -Force
    (Get-Item -LiteralPath $localWim).IsReadOnly = $false
    if ($packagedSynopsys) {
        Copy-Item -LiteralPath (Join-Path $usbSourceDirectory "experimental-ufx.json") -Destination $workRoot
    }
    if ($Package -in @("Usb", "UsbTouch")) {
        foreach ($fileName in $usbFiles) {
            $sourceFile = Join-Path $usbSourceDirectory $fileName
            if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
                throw "Driver paket dosyasi bulunamadi: $sourceFile"
            }
            Copy-Item -LiteralPath $sourceFile -Destination (Join-Path $driverRoot $fileName) -Force
        }
    }
    if ($Package -in @("Touch", "UsbTouch", "TouchUfs")) {
        foreach ($fileName in $touchFiles) {
            $sourceFile = Join-Path $touchSourceDirectory $fileName
            if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
                throw "Driver paket dosyasi bulunamadi: $sourceFile"
            }
            Copy-Item -LiteralPath $sourceFile -Destination (Join-Path $driverRoot $fileName) -Force
        }
    }
    if ($Package -in @("Ufs", "TouchUfs")) {
        foreach ($fileName in $ufsFiles) {
            $sourceFile = Join-Path $ufsSourceDirectory $fileName
            if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
                throw "Driver paket dosyasi bulunamadi: $sourceFile"
            }
            Copy-Item -LiteralPath $sourceFile -Destination (Join-Path $driverRoot $fileName) -Force
        }
    }

    if (Test-B2LAdministrator) {
        Invoke-B2LOfflineServicing -WorkRoot $workRoot -SelectedPackage $Package -DisableUsb:$DisableUsb
        $exitCode = 0
    }
    else {
        Write-Host "DISM offline DriverStore icin Windows UAC onayi bekleniyor: $Package"
        $escapedScript = $localScript.Replace("'", "''")
        $escapedRoot = $workRoot.Replace("'", "''")
        $disableUsbSwitch = if ($DisableUsb) { " -DisableUsb" } else { "" }
        $command = "& '$escapedScript' -PreparedRoot '$escapedRoot' -Package '$Package'$disableUsbSwitch"
        $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
        $powershell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
        try {
            $process = Start-Process -FilePath $powershell -Verb RunAs -ArgumentList @(
                "-NoProfile", "-ExecutionPolicy", "Bypass", "-EncodedCommand", $encoded
            ) -Wait -PassThru
            $exitCode = $process.ExitCode
        }
        catch {
            throw "UAC onayi baslatilamadi veya reddedildi: $($_.Exception.Message)"
        }
    }

    if (Test-Path -LiteralPath $script:LogPath) {
        Get-Content -LiteralPath $script:LogPath | Write-Host
    }
    if ($exitCode -ne 0) {
        throw "DISM offline DriverStore servislemesi basarisiz (exit=$exitCode)."
    }

    $servicedWim = Join-Path $workRoot "boot.wim"
    if ((Get-Item -LiteralPath $servicedWim).Length -le 0) {
        throw "Servislenmis boot.wim bos."
    }
    Copy-Item -LiteralPath $servicedWim -Destination $WimPath -Force
    Write-Host "Offline DriverStore boot.wim'e yazildi: $Package"
}
finally {
    # Keep stage hashes even on failure/UAC errors; do not lose the evidence
    # when the native Windows temporary directory is cleaned up.
    $logDestination = if ($Package -eq "Touch") {
        $touchSourceDirectory
    }
    elseif ($Package -in @("Ufs", "TouchUfs")) {
        $ufsSourceDirectory
    }
    else {
        $usbSourceDirectory
    }
    if (Test-Path -LiteralPath $script:LogPath) {
        Copy-Item -LiteralPath $script:LogPath -Destination (Join-Path $logDestination "offline-driverstore.log") -Force
    }
    Remove-Item -LiteralPath $workRoot -Recurse -Force -ErrorAction SilentlyContinue
}
