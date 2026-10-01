<#
    deploy-debug.ps1 -- copy the Debug build to its deployment folder.

        powershell -ExecutionPolicy Bypass -File Tools\deploy-debug.ps1

    The destination comes from -Destination or the VAULT_DEPLOY_DIR
    environment variable; it is never stored in the repository.
    Writes exactly one file (written next to the target, then renamed) and
    never deletes anything else in the destination folder.
#>
[CmdletBinding()]
param(
    [string] $Configuration = 'Debug',
    [string] $Destination   = '',
    [string] $Name          = 'Vault-Debug.exe',
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo "x64\$Configuration\Vault.exe"

function Fail($text) { Write-Host "  $text" -ForegroundColor Red; exit 1 }
function Warn($text) { Write-Host "  $text" -ForegroundColor Yellow }
function Note($text) { Write-Host "  $text" }

Write-Host ""
Write-Host "  Manifold Vault -- deploy $Configuration"
Write-Host ("  " + ("-" * 52))

if (-not $Destination) { $Destination = $env:VAULT_DEPLOY_DIR }
if (-not $Destination) {
    Fail (
        "No destination set. Set it once:" +
        "`n`n  Permanently (applies to NEW windows):" +
        "`n      setx VAULT_DEPLOY_DIR `"D:\somewhere\AccountManager`"" +
        "`n`n  For this window only (applies now):" +
        "`n      `$env:VAULT_DEPLOY_DIR = `"D:\somewhere\AccountManager`"" +
        "`n`n  Or for this run only:" +
        "`n      -Destination `"D:\somewhere\AccountManager`"")
}

if (-not (Test-Path -LiteralPath $source)) {
    Fail "No build found: $source`n  Build first, then deploy."
}

$exe = Get-Item -LiteralPath $source

$newest = Get-ChildItem -LiteralPath (Join-Path $repo 'Manifold-Vault\Source') -Recurse -File |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($newest -and $newest.LastWriteTime -gt $exe.LastWriteTime) {
    $text = "The build is OLDER than the sources ($($newest.Name), $($newest.LastWriteTime.ToString('yyyy-MM-dd HH:mm')))."
    if (-not $Force) {
        Fail "$text`n  Rebuild first -- or copy anyway with -Force."
    }
    Warn $text
    Warn "Copying anyway (-Force)."
}

$version = $exe.VersionInfo
Note "Source : $source"
Note "         $($exe.Length) bytes, $($exe.LastWriteTime.ToString('yyyy-MM-dd HH:mm'))"
Note "         $($version.ProductName) $($version.FileVersion)"

if (-not (Test-Path -LiteralPath $Destination)) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Note "Target : $Destination  (created)"
} else {
    Note "Target : $Destination"
}

$target = Join-Path $Destination $Name
$staging = "$target.new"

Copy-Item -LiteralPath $source -Destination $staging -Force
try {
    Move-Item -LiteralPath $staging -Destination $target -Force
} catch {
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging }
    Fail "Replacing failed: $($_.Exception.Message)`n  The existing file at the destination is untouched."
}

$a = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$b = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
if ($a -ne $b) { Fail "The copy differs from the source." }

Write-Host ""
Write-Host "  Done   : $target" -ForegroundColor Green
Note "SHA-256: $a"
Write-Host ""

Read-Host -Prompt "Press Enter to continue..."
