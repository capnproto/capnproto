# Install Chocolatey, and then a few Chocolatey packages.

$ErrorActionPreference = "Stop"

# Install and configure Choco.
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))

# Imply --confirm.
choco feature enable -n=allowGlobalConfirmation
# Imply --no-progress.
choco feature disable -n=showDownloadProgress

# Install various other packages.
choco install git --params "/GitOnlyOnPath"
choco install cmake --installargs "ADD_CMAKE_TO_PATH=System"
choco install ninja
choco install openssl
