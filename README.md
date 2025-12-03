
# ccminer (Soteria Variant)

This variant of **ccminer** was tested and built on **Linux (Debian Bookworm)**.  
The recommended **CUDA Toolkit version** is **11.8**.
Other new versions have been released that should work with CUDA above 11.8.

---

## 🚀 Installation

### Install CUDA 11.8
Download and install CUDA Toolkit 11.8 from NVIDIA:  
👉 [CUDA 11.8 Download Archive](https://developer.nvidia.com/cuda-11-8-0-download-archive)

Alternatively, you can try:
```bash
sudo apt install nvidia-cuda-toolkit
```
⚠️ Check the version before proceeding — if it’s not 11.8, you must install from NVIDIA’s archive.

### Verify CUDA installation
```bash
nvcc --version
```
If `nvcc` is missing, CUDA is not installed correctly.

### Check for CUDA runtime library
```bash
ls /usr/lib/x86_64-linux-gnu/ | grep libcudart
```

If ccminer complains about `libcudart.so.11.0`, but you only have another version (e.g. `libcudart.so.10.2`), create a symlink:
```bash
sudo ln -s /usr/lib/x86_64-linux-gnu/libcudart.so.10.2 /usr/lib/x86_64-linux-gnu/libcudart.so.11.0
```
*(Adjust the path to match your system.)*

---

## ⚙️ Usage

To make ccminer available globally:
```bash
sudo cp ./ccminer /usr/local/bin/
sudo chmod +x /usr/local/bin/ccminer
```

Now you can run ccminer directly:
```bash
ccminer -a soterg -o stratum+tcp://pool_address:port -u wallet_address.worker -p x
```

### Parameters
- `-a soterg` → algorithm name.  
- `-o stratum+tcp://127.0.0.1:3333` → pool or proxy address and port.  
- `-u SMy5NT6Qzfwsb6chSkstyhugJfcWGhQU7.worker` → wallet address + worker name.  
- `-p x` → password (often unused, just set to `x`).  

---

## 🛠️ Troubleshooting

**Error: `bash: ccminer: command not found`**  
Cause: ccminer binary is not in your PATH.  
Fix:
```bash
sudo cp ./ccminer /usr/local/bin/
sudo chmod +x /usr/local/bin/ccminer
```
---

## Stratum proxy

To use it with stratum:

```bash
ccminer -a soterg -o stratum+tcp://<VPS_IP>:3333 -u wallet_address.worker -p x
```

- Replace `<VPS_IP>` with the IP of stratum proxy server.  

---

## 📦 Source Code Dependencies

To build ccminer from source, the following libraries are required:
- **OpenSSL**  
- **Curl**  
- **pthreads**

Prebuilt libraries for Windows (x86 and x64) are included.  
To rebuild them, clone the repository and submodules:
```bash
git clone https://github.com/peters/curl-for-windows.git compat/curl-for-windows
```

---

## 🔨 Compile on Linux

See the [INSTALL](https://github.com/tpruvot/ccminer/blob/linux/INSTALL) file or the [project Wiki](https://github.com/tpruvot/ccminer/wiki/Compatibility) for detailed build instructions.

---

### ✅ Final Notes
This README is designed for **users**, not just developers. It explains:
- How to install CUDA 11.8.  
- How to verify and fix library issues.  
- How to run ccminer with correct parameters.  
- How to troubleshoot common errors.  

---
