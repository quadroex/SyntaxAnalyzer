param([int]$Port = 8765, [switch]$NoBrowser)

$ErrorActionPreference = 'Stop'
$linuxProject = (& wsl.exe --exec wslpath -a $PSScriptRoot | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or -not $linuxProject) {
    throw 'Cannot access the project in WSL.'
}

Write-Host 'Building MiniCpp in WSL...'
& wsl.exe --exec make -C $linuxProject
if ($LASTEXITCODE -ne 0) {
    throw 'Build failed. Check that g++, make, flex and bison are installed in WSL.'
}

$url = "http://127.0.0.1:$Port"
try {
    $health = Invoke-RestMethod "$url/api/health" -TimeoutSec 2
    if ($health.service -eq 'MiniCpp') {
        Write-Host "MiniCpp is already running at $url"
        if (-not $NoBrowser) { Start-Process $url }
        exit 0
    }
    throw "Port $Port is used by another service. Run start.ps1 -Port 8766."
}
catch [System.Net.WebException] {}

$browserJob = $null
if (-not $NoBrowser) {
    $browserJob = Start-Job -ArgumentList $url -ScriptBlock {
        param($Address)
        for ($attempt = 0; $attempt -lt 45; $attempt++) {
            try {
                $health = Invoke-RestMethod "$Address/api/health" -TimeoutSec 1
                if ($health.service -eq 'MiniCpp') {
                    Start-Process $Address
                    return
                }
            }
            catch {}
            Start-Sleep -Seconds 1
        }
    }
}

Write-Host "Open $url in a browser. Keep this window open. Press Ctrl+C to stop."
try {
    & wsl.exe --exec python3 "$linuxProject/server.py" --port $Port
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 130) { throw 'MiniCpp server stopped with an error.' }
}
finally {
    if ($browserJob) {
        Stop-Job $browserJob -ErrorAction SilentlyContinue
        Remove-Job $browserJob -Force -ErrorAction SilentlyContinue
    }
}
