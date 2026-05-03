$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$listener = Join-Path $scriptDir "pc_power_listener.py"
$python = "C:\Program Files\Python313\python.exe"
$tokenFile = Join-Path $scriptDir "pc_listener_token.txt"
$logDir = Join-Path $scriptDir "logs"
$outLog = Join-Path $logDir "pc_power_listener.out.log"
$errLog = Join-Path $logDir "pc_power_listener.err.log"

New-Item -ItemType Directory -Force -Path $logDir | Out-Null
Set-Location $scriptDir

$token = ""
if (Test-Path $tokenFile) {
    $token = (Get-Content -Raw -Path $tokenFile).Trim()
}

& $python $listener --host 0.0.0.0 --port 8787 --token $token *> $outLog
