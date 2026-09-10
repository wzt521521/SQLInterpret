param([switch]$Debug)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
$compiler = (Get-Command g++ -ErrorAction Stop).Source
$archiver = (Get-Command ar -ErrorAction Stop).Source
$flags = @('-std=c++17', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I', (Join-Path $projectRoot 'include'))
if ($Debug) { $flags += @('-O0', '-g') } else { $flags += '-O2' }
$objects = @()
foreach ($source in (Get-ChildItem -LiteralPath (Join-Path $projectRoot 'src') -Filter '*.cc' | Sort-Object Name)) {
    $object = Join-Path $buildDirectory ($source.BaseName + '.o')
    & $compiler @flags '-c' $source.FullName '-o' $object
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $($source.Name)" }
    $objects += $object
}
$library = Join-Path $buildDirectory 'libminisql.a'
& $archiver 'rcs' $library @objects
if ($LASTEXITCODE -ne 0) { throw 'Static library creation failed.' }
& $compiler @flags (Join-Path $projectRoot 'app/main.cc') $library '-lshell32' '-static' '-o' (Join-Path $buildDirectory 'minisql_cli.exe')
if ($LASTEXITCODE -ne 0) { throw 'Linking failed: minisql_cli' }
& $compiler @flags (Join-Path $projectRoot 'app/bridge.cc') $library '-static' '-o' (Join-Path $buildDirectory 'minisql_bridge.exe')
if ($LASTEXITCODE -ne 0) { throw 'Linking failed: minisql_bridge' }
Write-Output "Built C++17 library and compiler CLI in $buildDirectory"
