param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $IdfArgs
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$idfExport = Join-Path $env:USERPROFILE ".platformio\packages\framework-espidf\export.ps1"
$idfPy = Join-Path $env:USERPROFILE ".platformio\packages\framework-espidf\tools\idf.py"

Set-Location $projectRoot
& $idfExport
& $idfPy @IdfArgs
