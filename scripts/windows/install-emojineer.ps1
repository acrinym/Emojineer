param(
    [Parameter(Mandatory=$true)][string]$Source,
    [string]$Destination = "$env:LOCALAPPDATA\Programs\Emojineer",
    [switch]$AddToPath
)
$ErrorActionPreference = "Stop"
$sourceRoot = (Resolve-Path $Source).Path
$bin = Join-Path $sourceRoot "bin"
if (-not (Test-Path (Join-Path $bin "emojineer.exe"))) {
    throw "Source must contain bin\emojineer.exe"
}
if (Test-Path $Destination) {
    Remove-Item -LiteralPath $Destination -Recurse -Force
}
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
Copy-Item -Path (Join-Path $sourceRoot "*") -Destination $Destination -Recurse -Force
if ($AddToPath) {
    $installBin = Join-Path $Destination "bin"
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $parts = @($userPath -split ";" | Where-Object { $_ })
    if ($parts -notcontains $installBin) {
        [Environment]::SetEnvironmentVariable("Path", (($parts + $installBin) -join ";"), "User")
    }
}
Write-Output "Installed Emojineer to $Destination"
