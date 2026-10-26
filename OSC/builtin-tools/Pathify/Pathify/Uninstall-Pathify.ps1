$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT') {
    throw 'Pathify can only be uninstalled on Windows.'
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') `
        -Verb RunAs -ArgumentList @(
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', "`"$PSCommandPath`""
        )
    exit
}

$installDirectory = $PSScriptRoot
$startMenuDirectory = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Pathify'
$desktopShortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Pathify GUI.lnk'

if (Test-Path -LiteralPath $desktopShortcut) {
    Remove-Item -LiteralPath $desktopShortcut -Force
}
if (Test-Path -LiteralPath $startMenuDirectory) {
    $startMenuShortcut = Join-Path $startMenuDirectory 'Pathify GUI.lnk'
    if (Test-Path -LiteralPath $startMenuShortcut) {
        Remove-Item -LiteralPath $startMenuShortcut -Force
    }
    $remainingStartMenuFiles = Get-ChildItem -LiteralPath $startMenuDirectory -Force
    if ($remainingStartMenuFiles.Count -eq 0) {
        Remove-Item -LiteralPath $startMenuDirectory -Force
    }
}
$environmentKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)

if ($null -ne $environmentKey) {
    try {
        $existingPath = [string]$environmentKey.GetValue(
            'Path',
            '',
            [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames
        )
        $remainingEntries = [System.Collections.Generic.List[string]]::new()
        foreach ($entry in $existingPath.Split([char]';')) {
            $normalizedEntry = $entry.Trim().Trim('"').TrimEnd('\')
            if (-not [string]::Equals(
                $normalizedEntry,
                $installDirectory.TrimEnd('\'),
                [StringComparison]::OrdinalIgnoreCase
            )) {
                $remainingEntries.Add($entry)
            }
        }

        if ($remainingEntries.Count -ne $existingPath.Split([char]';').Length) {
            if ($remainingEntries.Count -eq 0) {
                $environmentKey.DeleteValue('Path', $false)
            }
            else {
                $pathKind = $environmentKey.GetValueKind('Path')
                $environmentKey.SetValue('Path', [string]::Join(';', $remainingEntries), $pathKind)
            }
        }
    }
    finally {
        $environmentKey.Dispose()
    }
}

$executable = Join-Path $installDirectory 'Pathify.exe'
if (Test-Path -LiteralPath $executable) {
    Remove-Item -LiteralPath $executable -Force
}
$guiExecutable = Join-Path $installDirectory 'PathifyGui.exe'
if (Test-Path -LiteralPath $guiExecutable) {
    Remove-Item -LiteralPath $guiExecutable -Force
}
foreach ($moduleName in @('hello-go', 'hello-rust')) {
    $moduleDirectory = Join-Path $installDirectory "modules\$moduleName"
    foreach ($moduleFile in @('module.dll', 'module.json')) {
        $modulePath = Join-Path $moduleDirectory $moduleFile
        if (Test-Path -LiteralPath $modulePath) {
            Remove-Item -LiteralPath $modulePath -Force
        }
    }
    if (Test-Path -LiteralPath $moduleDirectory) {
        $remainingModuleFiles = Get-ChildItem -LiteralPath $moduleDirectory -Force
        if ($remainingModuleFiles.Count -eq 0) {
            Remove-Item -LiteralPath $moduleDirectory -Force
        }
        else {
            Write-Host "Left other files in $moduleDirectory untouched."
        }
    }
}
$modulesDirectory = Join-Path $installDirectory 'modules'
if (Test-Path -LiteralPath $modulesDirectory) {
    $remainingModules = Get-ChildItem -LiteralPath $modulesDirectory -Force
    if ($remainingModules.Count -eq 0) {
        Remove-Item -LiteralPath $modulesDirectory -Force
    }
}
$uninstaller = Join-Path $installDirectory 'Uninstall-Pathify.ps1'
if (Test-Path -LiteralPath $uninstaller) {
    Remove-Item -LiteralPath $uninstaller -Force
}
if (Test-Path -LiteralPath $installDirectory) {
    $remainingFiles = Get-ChildItem -LiteralPath $installDirectory -Force
    if ($remainingFiles.Count -eq 0) {
        Remove-Item -LiteralPath $installDirectory -Force
    }
    else {
        Write-Host "Left other files in $installDirectory untouched."
    }
}
$uninstallKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey(
    'Software\Microsoft\Windows\CurrentVersion\Uninstall',
    $true
)
if ($null -ne $uninstallKey) {
    try {
        $uninstallKey.DeleteSubKeyTree('Pathify', $false)
    }
    finally {
        $uninstallKey.Dispose()
    }
}

Write-Host 'Pathify was uninstalled. Other user PATH entries were left unchanged.'
