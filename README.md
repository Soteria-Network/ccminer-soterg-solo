# Soteria — Solo Mining on a Local Node (Draft)

> Hint: This is a draft version. We will add more details and instructions later.

## Overview
This guide shows how to mine Soteria solo on a local node (Linux). It covers NVIDIA CUDA Toolkit installation (tested on Debian 13), GLIBC requirements for ccminer-soterg-solo, soteria.conf configuration, and making soteriad/soteria-cli accessible system-wide so you don't need to cd into the binaries' folder every time.

---

## Requirements
- Debian 13 and any Linux OS with GLIBC ≥ 2.38 (instructions tested; CUDA package for Debian 12 works on Debian 13)
- NVIDIA GPU with proprietary driver
- CUDA Toolkit 13.0
- ccminer-soterg-solo (Linux build requires GLIBC ≥ 2.38)
- soteriad and soteria-cli executables
- Basic shell (bash, zsh, etc.)

---

## CUDA Toolkit 13.0 (installer used / tested)

Note: CUDA Toolkit 13.0 for other Linux OS, follow the instructions on Nvidia website (https://developer.nvidia.com/cuda-13-0-0-download-archive?target_os=Linux&target_arch=x86_64).

Official CUDA 13.0 download archive:
https://developer.nvidia.com/cuda-13-0-0-download-archive?target_os=Linux&target_arch=x86_64&Distribution=Debian&target_version=12&target_type=deb_local

Example installer used (Debian 12 package that works on Debian 13):
wget https://developer.download.nvidia.com/compute/cuda/13.0.0/local_installers/cuda-repo-debian12-13-0-local_13.0.0-580.65.06-1_amd64.deb

Install steps:
```
sudo dpkg -i cuda-repo-debian12-13-0-local_13.0.0-580.65.06-1_amd64.deb
sudo cp /var/cuda-repo-debian12-13-0-local/cuda-*-keyring.gpg /usr/share/keyrings/
sudo apt-get update
sudo apt-get -y install cuda-toolkit-13-0
sudo apt-get install -y cuda-drivers
```
Note: For better performance install the proprietary kernel module flavor not the open kernel module flavor from official Nvidia website

Note: CUDA Toolkit 13.0 officially targets Debian 12 in the archive, but it was tested and works on Debian 13.

---

## GLIBC requirement for ccminer-soterg-solo
The current Linux ccminer-soterg-solo requires GLIBC ≥ 2.38.

Check GLIBC version:
```
ldd --version
# Example output: ldd (GLIBC 2.38) 2.38
```

This build should work on Ubuntu 24, Debian 13, and other distributions with GLIBC 2.38 or newer. A build for older distributions may be released later.

---

## Example ccminer command (local node)
If miner and node run on the same machine:
```
./ccminer -a soterg -o http://127.0.0.1:8323 -u your_rpcuser -p your_rpcpassword --coinbase-addr=your_wallet_address
```

If miner and node run on different machines, adjust rpcbind and rpcallowip in soteria.conf (see below).

---

## soteria.conf (example)
Replace placeholders (your_rpcuser, your_rpcpassword, your_wallet_address, 192.168.0.XX) with real values.
```
addnode=178.72.89.199
addnode=69.42.222.20:8323
addnode=27.254.39.27:8323
addnode=145.239.3.70:8323
addnode=159.195.61.39:8323
addnode=45.10.160.253:8323
addnode=89.105.213.189:8323
addnode=178.72.89.199:8323
addnode=149.102.156.62:8323

rpcuser=your_rpcuser
rpcpassword=your_rpcpassword
rpcport=8323
rpcbind=127.0.0.1
#rpcbind=192.168.0.XX # Your internal IP address if using remote miner
rpcallowip=127.0.0.1
#rpcallowip=192.168.0.1/24

miningaddress=your_wallet_address

daemon=1
server=1
listen=1
```

Notes:
- Use rpcbind=127.0.0.1 and rpcallowip=127.0.0.1 if miner and node are on the same machine.
- Use rpcbind=192.168.0.XX and rpcallowip=192.168.0.1/24 if miner and node are on different machines on the LAN.

---

## Running the node and miner — quick methods

Method 1 — Run from same folder
1. Put soteriad, soteria-cli, and ccminer-soterg-solo in one folder.
2. In that folder:
```
./soteriad -daemon
./ccminer -a soterg -o http://127.0.0.1:8323 -u your_rpcuser -p your_rpcpassword --coinbase-addr=your_wallet_address
```
RPC credentials must match soteria.conf.

Method 2 — Make soteriad and soteria-cli accessible system-wide (recommended)
Choose one option below.

Option A — Move binaries to /usr/local/bin (recommended)
```
sudo cp /home/you/soteriad /usr/local/bin/
sudo cp /home/you/soteria-cli /usr/local/bin/
sudo chmod +x /usr/local/bin/soteriad /usr/local/bin/soteria-cli
```

Option B — Add directory to PATH (per-user)
Edit your shell profile (~/.bashrc, ~/.profile, or ~/.zshrc) and add:
```
export PATH="$PATH:/home/you/soteria"
```
Then reload:
```
source ~/.bashrc
```

Option C — Create symlinks in /usr/local/bin
```
sudo ln -s /home/you/path/to/soteriad /usr/local/bin/soteriad
sudo ln -s /home/you/path/to/soteria-cli /usr/local/bin/soteria-cli
```

Verify:
```
which soteriad    # e.g., /usr/local/bin/soteriad
which soteria-cli
soteriad --version   # e.g., Soteria Core Daemon version v1.1.0.0
soteria-cli --help   # e.g., Soteria Core RPC client version v1.1.0.0
```

Notes:
- Use /usr/local/bin for manual installs to avoid package-manager conflicts.
- If using a shell other than bash, update the appropriate profile file.
- To run soteriad as a system service, create a systemd unit and enable it:
  sudo systemctl enable --now <service>

---

## Example systemd service (optional)
### systemd (fill in your ExecStart and ExecStop datadir)

Create /etc/systemd/system/soteriad.service (replace /usr/local/bin/soteriad and /home/you/.soteria with your paths):
```
[Unit]
Description=Soteria Daemon
After=network.target

[Service]
User=your_user
Group=your_user
Type=forking
ExecStart=/usr/local/bin/soteriad -daemon -datadir=/home/your_user/.soteria
ExecStop=/usr/local/bin/soteria-cli -datadir=/home/your_user/.soteria stop
Restart=on-failure
TimeoutStopSec=30

[Install]
WantedBy=multi-user.target
```
Enable and start:
```
sudo systemctl daemon-reload
sudo systemctl enable --now soteriad
```
---

## Troubleshooting tips (brief)
- Ensure NVIDIA driver is installed and compatible with CUDA 13.0.
- Confirm GLIBC version with ldd --version.
- Check soteriad logs (datadir/debug.log) for RPC bind/listen errors.
- Verify firewall/iptables if using remote miner RPC connections.

---
