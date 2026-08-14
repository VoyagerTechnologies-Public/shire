# Installation

This page lists prerequisites and OS-specific tips for installing SHIRE dependencies.

## Prerequisites
* Docker Engine
* Docker Compose v2 (CLI plugin) or newer
* Make
* Git
* Python3

## Windows notes
For optimal performance on Windows we'd recommend WSL2 and Docker Engine installed.
You can simply follow your specific Linux OS install for docker and docker compose for WSL2.

If you prefer to work in a Virtual Machine, ensure you have Hyper-V disabled for VirtualBox (to avoid the green turtle) by using the official Windows DG Readiness Tool which completely disables Hyper-V and enables the other settings required.

## Linux / WSL 2.0 notes
From a fresh Ubuntu 26.04 installation (WSL2):
* sudo apt update
* sudo apt upgrade
* sudo apt install docker-compose-v2
* sudo apt install make
* sudo usermod -aG docker $USER
* sudo reboot now
* Manually added personal .ssh key to environment and update .bashrc
  * sudo chmod 600 ~/.ssh

## Verifying installation
```bash
docker --version
docker compose version
make --version
git --version
python3 --version
groups
```


----
Last updated: 20260511
