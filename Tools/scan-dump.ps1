<#
    scan-dump.ps1 -- does a given secret occur in a memory dump?

        powershell -ExecutionPolicy Bypass -File Tools\scan-dump.ps1 -Dump "C:\...\Vault.DMP"

    Counts occurrences of the string, narrow (UTF-8) and wide (UTF-16), and
    prints only the counts. The string is read with -AsSecureString and wiped
    afterwards; the dump is only read. A hit while the vault was unlocked is
    expected; a hit while it was locked is a bug.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Dump
)

$ErrorActionPreference = 'Stop'

function Note($text) { Write-Host "  $text" }
function Fail($text) { Write-Host "  $text" -ForegroundColor Red; exit 1 }

# Chunks overlap by the needle length; a hit lying entirely inside the
# carried-over tail was already counted in the previous chunk.
function Count-Occurrences([string] $hay, [string] $needle, [int] $carryLen) {
    $count = 0
    $n = $needle.Length
    if ($n -eq 0) { return 0 }
    $idx = 0
    while ($true) {
        $idx = $hay.IndexOf($needle, $idx, [StringComparison]::Ordinal)
        if ($idx -lt 0) { break }
        if (($idx + $n) -gt $carryLen) { $count++ }
        $idx++
    }
    return $count
}

Write-Host ""
Write-Host "  Searching a memory dump"
Write-Host ("  " + ("-" * 52))

if (-not (Test-Path -LiteralPath $Dump)) { Fail "No dump found: $Dump" }

$file = Get-Item -LiteralPath $Dump
Note ("Dump   : " + $file.FullName)
Note ("         {0:N1} MiB" -f ($file.Length / 1MB))
Write-Host ""

$secure = Read-Host "  String to search for (not shown)" -AsSecureString
if (-not $secure -or $secure.Length -eq 0) { Fail "Nothing entered." }

$bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
try {
    $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
    if ($plain.Length -lt 8) {
        Note "Warning: below eight characters, 300 MB will contain almost anything."
    }

    $narrow = [Text.Encoding]::UTF8.GetBytes($plain)
    $wide   = [Text.Encoding]::Unicode.GetBytes($plain)

    # Latin-1 maps bytes to characters 1:1, so IndexOf stays a byte search.
    $latin1 = [Text.Encoding]::GetEncoding(28591)
    $needleNarrow = $latin1.GetString($narrow)
    $needleWide   = $latin1.GetString($wide)

    $hitsNarrow = 0
    $hitsWide   = 0
    $overlap = [Math]::Max($narrow.Length, $wide.Length)
    $chunk = 16MB

    $stream = [IO.File]::OpenRead($file.FullName)
    try {
        $carry = [byte[]]::new(0)
        $buffer = [byte[]]::new($chunk)
        $read = 0
        $done = 0
        while (($read = $stream.Read($buffer, 0, $chunk)) -gt 0) {
            $data = [byte[]]::new($carry.Length + $read)
            [Array]::Copy($carry, 0, $data, 0, $carry.Length)
            [Array]::Copy($buffer, 0, $data, $carry.Length, $read)

            $hay = $latin1.GetString($data)
            $carryLen = $data.Length - $read

            $hitsNarrow += Count-Occurrences $hay $needleNarrow $carryLen
            $hitsWide   += Count-Occurrences $hay $needleWide   $carryLen

            if ($data.Length -gt $overlap) {
                $carry = [byte[]]::new($overlap)
                [Array]::Copy($data, $data.Length - $overlap, $carry, 0, $overlap)
            } else {
                $carry = $data
            }
            $done += $read
            Write-Progress -Activity "Searching" -PercentComplete (100 * $done / $file.Length)
        }
    } finally {
        $stream.Dispose()
        Write-Progress -Activity "Searching" -Completed
    }

    [Array]::Clear($narrow, 0, $narrow.Length)
    [Array]::Clear($wide, 0, $wide.Length)

    Write-Host ""
    Note ("Hits narrow (UTF-8) : " + $hitsNarrow)
    Note ("Hits wide  (UTF-16) : " + $hitsWide)
    Write-Host ""
    if (($hitsNarrow + $hitsWide) -eq 0) {
        Write-Host "  Not found." -ForegroundColor Green
    } else {
        Write-Host "  FOUND. If the vault was UNLOCKED when the dump was taken, this is the" -ForegroundColor Yellow
        Write-Host "  documented limit (see README). If it was LOCKED, it is a bug." -ForegroundColor Yellow
    }
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
}
