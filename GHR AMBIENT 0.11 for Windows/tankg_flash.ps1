# TANK-G FLASH - back up, install, restore, read the report. See tankg_flash.cs for exactly what it does.
param([string]$mode = "")
try {
  Add-Type -Path (Join-Path $PSScriptRoot 'tankg_flash.cs')
  $rc = [TankFlash]::Run($PSScriptRoot, $mode)
} catch {
  Write-Host ("ERROR: " + $_.Exception.Message)
  $rc = 1
}
exit $rc
