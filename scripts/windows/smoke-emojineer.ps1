param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference = "Stop"
$bin = Join-Path (Resolve-Path $Root).Path "bin"
$emojineer = Join-Path $bin "emojineer.exe"
$emji = Join-Path $bin "emji.exe"
$lsp = Join-Path $bin "emojineer-lsp.exe"
foreach ($tool in @($emojineer,$emji,$lsp)) {
    if (-not (Test-Path $tool)) { throw "Missing installed tool: $tool" }
}
& $emojineer --version
& $emji --version
& $lsp --version
$temp = Join-Path $env:TEMP ("emojineer-smoke-" + [guid]::NewGuid())
& $emji init $temp --name smoke
& $emojineer run (Join-Path $temp "src\main.emoji")
Remove-Item -LiteralPath $temp -Recurse -Force
Write-Output "Emojineer installed-tool smoke passed"
