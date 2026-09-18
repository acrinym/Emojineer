param(
    [string]$Destination = "$env:LOCALAPPDATA\Programs\Emojineer",
    [switch]$RemoveFromPath
)
$ErrorActionPreference = "Stop"
$installBin = Join-Path $Destination "bin"
if ($RemoveFromPath) {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $parts = @($userPath -split ";" | Where-Object { $_ -and $_ -ne $installBin })
    [Environment]::SetEnvironmentVariable("Path", ($parts -join ";"), "User")
}
if (Test-Path $Destination) {
    Remove-Item -LiteralPath $Destination -Recurse -Force
}
Write-Output "Removed Emojineer from $Destination"
