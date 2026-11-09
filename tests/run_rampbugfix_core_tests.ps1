# Engine-independent algorithm tests only; not a CS2 runtime or ABI check.
# CS2Fixes, GPL-3.0. Docker remains the repository's full-build workflow.
param(
    [ValidateSet('Debug', 'Release', 'ASan')]
    [string]$Configuration = 'Debug',
    [string]$VsDevShell = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root "build\rampbugfix-core-tests\$Configuration"

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if (-not (Test-Path -LiteralPath $VsDevShell)) {
        throw 'MSVC not found. Run from an x64 VS developer shell or provide -VsDevShell.'
    }
    & $VsDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}

New-Item -ItemType Directory -Path $output -Force | Out-Null
Push-Location $root
try {
    $flags = @('/nologo', '/std:c++17', '/EHsc', '/W4', '/WX', '/Zi', '/Isrc')
    if ($Configuration -eq 'Release') { $flags += '/O2' } else { $flags += '/Od' }
    if ($Configuration -eq 'ASan') { $flags += '/fsanitize=address' }
    & cl.exe @flags src\rampbugfix\core.cpp tests\rampbugfix_core_tests.cpp "/Fo:$output\" "/Fd:$output\tests.pdb" "/Fe:$output\tests.exe" /link /INCREMENTAL:NO
    if ($LASTEXITCODE -ne 0) { throw "Core test compilation failed with exit code $LASTEXITCODE." }
    & (Join-Path $output 'tests.exe')
    if ($LASTEXITCODE -ne 0) { throw "Core tests failed with exit code $LASTEXITCODE." }
} finally {
    Pop-Location
}
