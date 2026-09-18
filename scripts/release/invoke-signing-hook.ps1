param([Parameter(Mandatory=$true)][string[]]$Path)
$ErrorActionPreference = "Stop"
$hook = $env:EMOJINEER_SIGN_SCRIPT
if (-not $hook) {
    Write-Output "No EMOJINEER_SIGN_SCRIPT configured; signing hook skipped."
    exit 0
}
if (-not (Test-Path $hook)) {
    throw "EMOJINEER_SIGN_SCRIPT does not exist: $hook"
}
foreach ($item in $Path) {
    & $hook $item
    if ($LASTEXITCODE -ne 0) { throw "Signing hook failed for $item" }
}
