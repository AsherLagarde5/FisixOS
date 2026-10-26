param(
    [switch]$NoPath
)

$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT') {
    throw 'Pathify can only be installed on Windows.'
}

$sourceExecutable = Join-Path $PSScriptRoot 'Pathify.exe'
if (-not (Test-Path -LiteralPath $sourceExecutable -PathType Leaf)) {
    throw "Pathify.exe was not found beside this installer: $sourceExecutable"
}

$installDirectory = Join-Path $env:LOCALAPPDATA 'Programs\Pathify'
New-Item -ItemType Directory -Force -Path $installDirectory | Out-Null
Copy-Item -LiteralPath $sourceExecutable -Destination (Join-Path $installDirectory 'Pathify.exe') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Uninstall-Pathify.ps1') -Destination $installDirectory -Force

$startMenuDirectory = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Pathify'
New-Item -ItemType Directory -Force -Path $startMenuDirectory | Out-Null
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut((Join-Path $startMenuDirectory 'Pathify GUI.lnk'))
$shortcut.TargetPath = Join-Path $installDirectory 'Pathify.exe'
$shortcut.Arguments = 'gui'
$shortcut.WorkingDirectory = $installDirectory
$shortcut.Description = 'Open Pathify PATH manager'
$shortcut.Save()
$desktopShortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Pathify GUI.lnk'))
$desktopShortcut.TargetPath = Join-Path $installDirectory 'Pathify.exe'
$desktopShortcut.Arguments = 'gui'
$desktopShortcut.WorkingDirectory = $installDirectory
$desktopShortcut.Description = 'Open Pathify PATH manager'
$desktopShortcut.Save()

if (-not $NoPath) {
    $environmentKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)
    if ($null -eq $environmentKey) {
        $environmentKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment')
    }
    if ($null -eq $environmentKey) {
        throw 'Could not open the current user environment registry key.'
    }

    try {
        $existingPath = [string]$environmentKey.GetValue(
            'Path',
            '',
            [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames
        )
        $containsInstallDirectory = $false
        foreach ($entry in $existingPath.Split([char]';')) {
            $normalizedEntry = $entry.Trim().Trim('"').TrimEnd('\')
            if ([string]::Equals(
                $normalizedEntry,
                $installDirectory.TrimEnd('\'),
                [StringComparison]::OrdinalIgnoreCase
            )) {
                $containsInstallDirectory = $true
                break
            }
        }

        if (-not $containsInstallDirectory) {
            $pathKind = [Microsoft.Win32.RegistryValueKind]::ExpandString
            try {
                $pathKind = $environmentKey.GetValueKind('Path')
            }
            catch [System.ArgumentException] {
            }

            $updatedPath = if ([string]::IsNullOrEmpty($existingPath)) {
                $installDirectory
            }
            else {
                "$existingPath;$installDirectory"
            }
            $environmentKey.SetValue('Path', $updatedPath, $pathKind)
        }
    }
    finally {
        $environmentKey.Dispose()
    }
}

Write-Host "Pathify installed to $installDirectory"
Write-Host 'Pathify GUI shortcuts were added to the Start menu and desktop.'
if ($NoPath) {
    Write-Host 'PATH was not changed. Add the install directory to PATH manually to invoke Pathify by name.'
}
else {
    Write-Host 'Pathify was added to your user PATH. Open a new terminal to use the Pathify command.'
}
