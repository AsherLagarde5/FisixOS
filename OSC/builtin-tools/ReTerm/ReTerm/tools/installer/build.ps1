$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$mingwBin = "C:\msys64\mingw64\bin"
if (Test-Path (Join-Path $mingwBin "dlltool.exe")) {
    $env:PATH = "$mingwBin;$env:PATH"
}

Push-Location $repoRoot
try {
    function Invoke-CargoBuild([string[]]$Arguments) {
        & cargo @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "cargo $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
        }
    }

    Invoke-CargoBuild -Arguments @("build", "-p", "reterm", "--release", "--bins", "--jobs", "1", "--target-dir", "target\bundle-build")
    Invoke-CargoBuild -Arguments @("build", "-p", "reterm-compmaker-gui", "--release", "--bin", "ReTermCompMakerGui", "--jobs", "1", "--target-dir", "target\bundle-build")
    Invoke-CargoBuild -Arguments @("build", "-p", "reterm-modmaker-gui", "--release", "--bin", "ReTermModMakerGui", "--jobs", "1", "--target-dir", "target\bundle-build")
    Invoke-CargoBuild -Arguments @("build", "-p", "reterm-modlauncher-gui", "--release", "--bin", "ReTermModLauncher", "--jobs", "1", "--target-dir", "target\bundle-build")
    Invoke-CargoBuild -Arguments @("build", "-p", "reterm-installer", "--release", "--bin", "ReTermSetup", "--jobs", "1", "--target-dir", "target")

    $installer = Join-Path $repoRoot "target\release\ReTermSetup.exe"
    if (-not (Test-Path -LiteralPath $installer)) {
        throw "Installer build did not produce $installer"
    }

    Get-Item -LiteralPath $installer | Select-Object FullName, Length, LastWriteTime
}
finally {
    Pop-Location
}
