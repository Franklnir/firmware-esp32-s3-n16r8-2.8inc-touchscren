param(
    [string]$Port,
    [switch]$DryRun
)

# Use the build manifest so assets are always flashed together with firmware.
$flashScript = Join-Path (Split-Path $PSScriptRoot -Parent) 'flash.py'
$flashArguments = @($flashScript)
if ($DryRun) {
    $flashArguments += '--dry-run'
} elseif ($Port) {
    $flashArguments += $Port
}
& python @flashArguments
exit $LASTEXITCODE
