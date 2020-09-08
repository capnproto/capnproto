# Install and configure OpenSSH, following these directions:
#
#   https://docs.microsoft.com/en-us/windows-server/administration/openssh/openssh_install_firstuse
#   https://docs.microsoft.com/en-us/windows-server/administration/openssh/openssh_server_configuration

$ErrorActionPreference = "Stop"

Add-WindowsCapability -Online -Name OpenSSH.Client~~~~0.0.1.0
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Set-Service -Name sshd -StartupType 'Automatic'
Start-Service sshd

# Make sure the default shell is cmd.exe. It's a bit more ergonomic for development, since it can
# see the environment variables changes by vcvarsall.bat.
New-ItemProperty -Path "HKLM:\SOFTWARE\OpenSSH" -Name DefaultShell -Value "C:\Windows\System32\cmd.exe" -PropertyType String -Force

# TODO(soon): Just ship a damn sshd_config.
$sshdConfig = "$env:ProgramData\ssh\sshd_config"
(Get-Content $sshdConfig).replace('#PubkeyAuthentication yes', 'PubkeyAuthentication yes') | Set-Content $sshdConfig
#(Get-Content $sshdConfig).replace('#SyslogFacility AUTH', 'SyslogFacility LOCAL0') | Set-Content $sshdConfig
#(Get-Content $sshdConfig).replace('#LogLevel INFO', 'LogLevel DEBUG3') | Set-Content $sshdConfig
(Get-Content $sshdConfig).replace('Match Group administrators', '') | Set-Content $sshdConfig
(Get-Content $sshdConfig).replace('       AuthorizedKeysFile __PROGRAMDATA__/ssh/administrators_authorized_keys', '') | Set-Content $sshdConfig
Restart-Service sshd
