# Install Visual Studio.
#
# This is initially a two-step process: first we create an offline installer in the host filesystem,
# then we install onto the guest system from the offline installer. Once the offline installer
# exists, all subsequent provisioning will use it, making it relatively cheap to destroy and
# recreate VMs as needed.

$ErrorActionPreference = "Stop"

$layout = "C:\vagrant\vs-layout"
$workload = "Microsoft.VisualStudio.Workload.NativeDesktop;includeRecommended;includeOptional"

if (Test-Path "$layout\vs_community.exe" -PathType Leaf) {
  Write-Output "Offline Visual Studio installer found."
} else {
  # I pulled this URL out of Chrome's devtools.
  $installerUrl = "https://download.visualstudio.microsoft.com/download/pr/befdb1f9-8676-4693-b031-65ee44835915/c541feeaa77b97681f7693fc5bed2ff82b331b168c678af1a95bdb1138a99802/vs_Community.exe"
  $installer = "$env:TEMP\vs_community.exe"

  Write-Output "Downloading vs_community.exe."
  Invoke-WebRequest -Uri $installerUrl -OutFile $installer

  Write-Output "Creating/updating offline installation of $workload."
  $installerProc = Start-Process `
      -FilePath $installer `
      -ArgumentList "--layout", $layout, "--add", $workload, "--passive", "--norestart", "--wait" `
      -Wait -PassThru
  $exitCode = $installerProc.ExitCode
  if ($exitCode -ne 0) {
    Write-Error "vs_community.exe returned $exitCode."
    exit 1
  }
  Write-Output "Successfully created/updated offline installation Visual Studio."
}

Write-Output "Creating/updating local installation of $workload."
$installerProc = Start-Process `
    -FilePath "$layout\vs_community.exe" `
    -ArgumentList "--noWeb", "--noUpdateInstaller", "--add", $workload, "--passive", "--norestart", "--wait" `
    -Wait -PassThru
$exitCode = $installerProc.ExitCode
if ($exitCode -ne 0) {
  Write-Error "vs_community.exe returned $exitCode."
  exit 1
}
Write-Output "Successfully created/updated local installation Visual Studio."
