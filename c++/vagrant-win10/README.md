# Windows 10 Vagrantfile for Remote Development with VSCode

Quick start:

```bash
vagrant up

# Go do something else for a while.

vagrant ssh-config > windev-ssh.conf
```

In vscode's command pallete, run "Remote-SSH: Open Configuration File". Select the windev-ssh.conf file we just created.

The host repository (the one you ran the `vagrant` commands in) is exposed in the guest under C:\capnproto.

## MSVC

Install and reload the "CMake Tools" extension in the remote vscode.
# TODO(now):
#   - Support arbitrary public key.
#   - Reduce choco install output
#   - Support Cygwin
#   - Support MinGW
#   - Support clang/C2
#   - Support MSVC
