$ErrorActionPreference = 'Stop'
$linuxProject = (& wsl.exe --exec wslpath -a $PSScriptRoot | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot access the project in WSL.' }
& wsl.exe --exec make -C $linuxProject test
if ($LASTEXITCODE -ne 0) { throw 'Verification failed.' }
Write-Host 'All checks passed.'
