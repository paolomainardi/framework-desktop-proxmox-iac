# igpu-passthrough

Ansible role to configure AMD iGPU passthrough on Proxmox VE hosts.

## Description

This role configures a Proxmox VE host for AMD integrated GPU (iGPU) passthrough to virtual machines. It handles:

- Bootloader configuration (GRUB or systemd-boot)
- IOMMU kernel parameters
- VFIO module loading
- GPU driver blacklisting (including amdgpu)
- VFIO-PCI driver binding with `disable_vga=1`
- VBIOS extraction and management
- AMD GOP driver ROM installation
- Configuration verification

> **Configuration Sources**: This implementation is based on best practices from:

> - [Strix Halo Wiki - VM iGPU Passthrough Guide](https://strixhalo.wiki/Guides/VM-iGPU-Passthrough)
> - [Framework Community - Proxmox VE Configuration](https://community.frame.work/t/anyone-using-proxmox-ve/74863/6)

## Quickstart

Create a file named `igpu-passthrough.yml`:

```yaml
---
- name: Configure iGPU passthrough
  hosts: proxmox_hosts
  become: true
  roles:
    - igpu-passthrough
```

Run the playbook:

```bash
ansible-playbook igpu-passthrough.yml
```

After completion, **reboot the host** and verify:

```bash
reboot
ansible-playbook igpu-passthrough.yml --tags verify
```

With custom GPU PCI IDs:

```yaml
---
- name: Configure iGPU passthrough
  hosts: proxmox_hosts
  become: true
  roles:
    - role: igpu-passthrough
      vars:
        gpu_pci_ids: "1002:XXXX,1002:YYYY"
        vbios_filename: "vbios_1002_XXXX.bin"
```

## Requirements

- Proxmox VE host with AMD CPU and iGPU
- AMD iGPU with supported PCI IDs (default: `1002:1586,1002:1640`)
- Root/sudo access on target host
- GCC compiler (for VBIOS extraction tool)

## Role Variables

All variables are defined in `defaults/main.yml` with sensible defaults:

### GPU Configuration

```yaml
# GPU PCI IDs for passthrough
gpu_pci_ids: "1002:1586,1002:1640"

# ROM file names
vbios_filename: "vbios_1002_1586.bin"
amdgop_rom: "AMDGopDriver.rom"
```

### ROM Checksums

```yaml
rom_checksums:
  vbios_1002_1586.bin:
    md5: "8f2d1e6c0a333d39117849bbfccbb1d7"
    sha256: "35ea302fd1cf5e1f7fbbc34c37e43cccc6e5ffab8fa1f335f4d10ad1d577627d"
    size: 17408
  AMDGopDriver.rom:
    md5: "4c0fee922ddf6afbe4ff6d50c9af211b"
    sha256: "1c2b9dc8bd931b98e0a0097b3169bff8e00636eb6c144a86be9873ebfd6e3331"
    size: 92672
```

### Kernel Parameters

```yaml
# Kernel parameters for GRUB
grub_cmdline: "quiet amd_iommu=on iommu=pt video=efifb:off initcall_blacklist=sysfb_init"

# Kernel parameters for systemd-boot
systemd_boot_cmdline: "root=ZFS=rpool/ROOT/pve-1 boot=zfs quiet amd_iommu=on iommu=pt video=efifb:off initcall_blacklist=sysfb_init"
```

### Modules and Drivers

```yaml
# VFIO modules to load
vfio_modules:
  - vfio
  - vfio_iommu_type1
  - vfio_pci
  - vfio_virqfd

# GPU drivers to blacklist
blacklisted_drivers:
  - amdgpu
  - radeon
  - snd_hda_intel
```

## Dependencies

None

## Files

The role requires the following files in `files/`:

- `vbios-extract.c` - VBIOS extraction tool source code
- `AMDGopDriver.rom` - AMD GOP driver ROM file (from [Strix Halo Wiki](https://strixhalo.wiki/Guides/VM-iGPU-Passthrough))

### Downloading AMDGopDriver.rom

If `AMDGopDriver.rom` is missing, download it from the [Strix Halo Wiki](https://strixhalo.wiki/Guides/VM-iGPU-Passthrough):

```bash
wget https://strixhalo.wiki/Guides/VM-iGPU-Passthrough/AMDGopDriver.rom \
  -O roles/igpu-passthrough/files/AMDGopDriver.rom
```

## Usage

### Full Configuration and Verification

```bash
ansible-playbook igpu-passthrough.yml
```

### Setup Only (without verification)

```bash
ansible-playbook igpu-passthrough.yml --tags setup
```

### Verification Only

```bash
ansible-playbook igpu-passthrough.yml --tags verify
```

## Tags

- `setup` - Run only setup tasks
- `configure` - Alias for setup
- `verify` - Run only verification tasks
- `check` - Alias for verify

## What It Does

### Setup Tasks

1. Detects bootloader type (GRUB or systemd-boot)
2. Configures kernel parameters for IOMMU
3. Configures VFIO module loading
4. Extracts VBIOS from GPU
5. Verifies VBIOS checksums
6. Installs AMD GOP driver ROM
7. Blacklists conflicting GPU drivers
8. Configures VFIO-PCI driver binding

### Verification Tasks

1. Checks bootloader configuration
2. Verifies IOMMU is enabled
3. Checks IOMMU groups exist
4. Verifies GPU is bound to vfio-pci driver
5. Verifies audio device is bound to vfio-pci driver
6. Checks VFIO modules are loaded
7. Verifies VFIO devices exist
8. Checks ROM files are present and valid
7. Verifies kernel parameters are active
8. Checks modprobe configuration files (vfio.conf and blacklist-gpu.conf)
9. Generates comprehensive verification report

## Post-Installation

After running the role, **a system reboot is required** for all changes to take effect. The role will notify you when a reboot is needed.

```bash
# Reboot the system
reboot
```

After reboot, run verification:

```bash
ansible-playbook igpu-passthrough.yml --tags verify
```

## Verification Report

The verification task generates a detailed report showing:

- ✅ Successful checks
- ❌ Failed checks
- ⚠️ Warnings

Example output:

```text
==================================
GPU PASSTHROUGH VERIFICATION REPORT
==================================

0. BOOTLOADER:
   Type: systemd-boot
   Config: root=ZFS=rpool/ROOT/pve-1 boot=zfs quiet amd_iommu=on iommu=pt video=efifb:off

1. IOMMU STATUS:
   ✅ ENABLED
   IOMMU Groups: 42

2. GPU DRIVER BINDING:
   ✅ BOUND TO vfio-pci

3. AUDIO DEVICE BINDING:
   ✅ BOUND TO vfio-pci

4. VFIO MODULES:
   ✅ LOADED

5. VFIO DEVICES:
   ✅ FOUND (2 devices)

6. ROM FILES:
   VBIOS: ✅ EXISTS (17408 bytes)
   AMDGopDriver: ✅ EXISTS (92672 bytes)

7. KERNEL PARAMETERS (Active in /proc/cmdline):
   ✅ amd_iommu=on
   ✅ iommu=pt
   ✅ video=efifb:off
   ✅ initcall_blacklist=sysfb_init

8. MODPROBE CONFIGURATION FILES:
   /etc/modprobe.d/vfio.conf:
     ✅ ids=1002:1586,1002:1640
     ✅ disable_vga=1
     ✅ softdep amdgpu pre: vfio-pci
     ✅ softdep snd_hda_intel pre: vfio-pci

   /etc/modprobe.d/blacklist-gpu.conf:
     ✅ blacklist amdgpu
     ✅ blacklist radeon
     ✅ blacklist snd_hda_intel

==================================
OVERALL STATUS: ✅ READY FOR GPU PASSTHROUGH
==================================
```

## Troubleshooting

### VBIOS Extraction Fails

If VBIOS extraction fails, you may need to:

1. Check if your GPU is supported
2. Try extracting VBIOS manually using GPU-Z (Windows) or other tools
3. Place the extracted VBIOS in `/usr/share/kvm/` manually

### IOMMU Not Enabled

Ensure:

1. IOMMU is enabled in BIOS/UEFI
2. Kernel parameters are correctly applied
3. System has been rebooted after configuration

### GPU Not Bound to vfio-pci

Check:

1. PCI IDs are correct (`lspci -nn | grep VGA`)
2. VFIO modules are loaded (`lsmod | grep vfio`)
3. Kernel parameters include vfio-pci IDs


