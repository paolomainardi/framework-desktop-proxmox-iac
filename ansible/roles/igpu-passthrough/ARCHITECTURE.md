# GPU Passthrough Architecture Documentation

This document explains the technical architecture and implementation details of AMD GPU passthrough on Proxmox VE.

> **Configuration Sources**: This implementation is based on a combination of best practices from:
> 1. [Strix Halo Wiki - VM iGPU Passthrough Guide](https://strixhalo.wiki/Guides/VM-iGPU-Passthrough)
> 2. [Framework Community - Proxmox VE Configuration](https://community.frame.work/t/anyone-using-proxmox-ve/74863/6)
>
> The configuration has been adapted and extended for our specific hardware and use cases.

## Table of Contents

- [Overview](#overview)
- [Architecture Diagram](#architecture-diagram)
- [Component Breakdown](#component-breakdown)
- [Boot Sequence](#boot-sequence)
- [How It Works](#how-it-works)
- [Troubleshooting Guide](#troubleshooting-guide)

## Overview

GPU passthrough (also known as PCI passthrough) allows a virtual machine to directly access a physical GPU, bypassing the hypervisor's virtualization layer. This provides near-native performance for GPU-accelerated workloads.

### Key Concepts

- **IOMMU (Input-Output Memory Management Unit)**: Hardware feature that allows the hypervisor to safely pass through hardware devices to VMs by providing memory protection and address translation.
- **VFIO (Virtual Function I/O)**: Linux kernel driver framework for safely exposing direct device access to userspace (QEMU/KVM VMs).
- **VBIOS (Video BIOS)**: Firmware that initializes the GPU during boot. Required for proper VM GPU initialization.
- **GOP Driver (Graphics Output Protocol)**: UEFI driver that enables early graphics output before OS loads.

## Architecture Diagram

```
┌───────────────────────────────────────────────────────────────┐
│                    Proxmox Host (Bare Metal)                  │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌────────────────────────────────────────────────────────┐   │
│  │ Linux Kernel Boot                                      │   │
│  │  - Kernel cmdline: amd_iommu=on iommu=pt               │   │
│  │  - IOMMU groups created                                │   │
│  │  - VFIO modules loaded early (initramfs)               │   │
│  └────────────────────────────────────────────────────────┘   │
│                          ↓                                    │
│  ┌────────────────────────────────────────────────────────┐   │
│  │ Module Loading Phase                                   │   │
│  │  ┌──────────────────┐  ┌──────────────────┐            │   │
│  │  │ /etc/modules-    │  │ /etc/modprobe.d/ │            │   │
│  │  │ load.d/vfio.conf │  │ vfio.conf        │            │   │
│  │  │                  │  │ blacklist-       │            │   │
│  │  │ - vfio           │  │ gpu.conf         │            │   │
│  │  │ - vfio_iommu_    │  │                  │            │   │
│  │  │   type1          │  │ GPU → vfio-pci   │            │   │
│  │  │ - vfio_pci       │  │ (early binding)  │            │   │
│  │  │ - vfio_virqfd    │  │                  │            │   │
│  │  └──────────────────┘  └──────────────────┘            │   │
│  └────────────────────────────────────────────────────────┘   │
│                          ↓                                    │
│  ┌────────────────────────────────────────────────────────┐   │
│  │ GPU Bound to VFIO Driver                               │   │
│  │                                                        │   │
│  │  AMD GPU (1002:1586) ──→ vfio-pci (not amdgpu)         │   │
│  │  Audio (1002:1640)   ──→ vfio-pci (not snd_hda_intel)  │   │
│  │                                                        │   │
│  │  /usr/share/kvm/vbios_1002_1586.bin ✓                  │   │
│  │  /usr/share/kvm/AMDGopDriver.rom ✓                     │   │
│  └────────────────────────────────────────────────────────┘   │
│                          ↓                                    │
│  ┌────────────────────────────────────────────────────────┐   │
│  │ QEMU/KVM VM Configuration                              │   │
│  │                                                        │   │
│  │  <hostpci0: 03:00,pcie=1,romfile=vbios_1002_1586.bin>  │   │
│  │  <hostpci1: 03:00.1,pcie=1>  (Audio)                   │   │
│  │                                                        │   │
│  │  VM uses AMDGopDriver.rom for UEFI boot                │   │
│  └────────────────────────────────────────────────────────┘   │
│                          ↓                                    │
│  ┌────────────────────────────────────────────────────────┐   │
│  │ Virtual Machine                                        │   │
│  │                                                        │   │
│  │  ┌───────────────────────────────────────────────┐     │   │
│  │  │ Guest OS sees AMD GPU as physical hardware    │     │   │
│  │  │ - Native AMD driver loads                     │     │   │
│  │  │ - Full GPU acceleration                       │     │   │
│  │  │ - Display output (if connected)               │     │   │
│  │  └───────────────────────────────────────────────┘     │   │
│  └────────────────────────────────────────────────────────┘   │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

## Component Breakdown

### 1. Kernel Command Line Configuration

Proxmox VE supports two bootloaders, and the kernel command line configuration method differs for each:

#### Systemd-boot (Default on newer installs)

**Configuration file:** `/etc/kernel/cmdline`

```bash
root=ZFS=rpool/ROOT/pve-1 boot=zfs quiet amd_iommu=on iommu=pt video=efifb:off initcall_blacklist=sysfb_init
```

#### GRUB

**Configuration file:** `/etc/default/grub`

```bash
GRUB_CMDLINE_LINUX_DEFAULT="quiet amd_iommu=on iommu=pt video=efifb:off"
```

**When is GRUB used:**

- All installations except: EFI systems with ZFS root and Secure Boot disabled
- Systems installed on top of Debian
- Systems with Secure Boot enabled (uses GRUB with signed shim)

**Note:** The Ansible playbook automatically detects which bootloader is in use and configures accordingly.

#### Apply Changes (Both Bootloaders)

After modifying the configuration file for your bootloader, apply the changes with:

```bash
proxmox-boot-tool refresh
update-initramfs -u -k all
reboot
```

**Note:** `proxmox-boot-tool refresh` works for both GRUB and systemd-boot configurations.

See [Proxmox Boot Configuration Documentation](https://pve.proxmox.com/pve-docs/chapter-sysadmin.html#sysboot) for complete details on bootloader configuration.

#### Parameters Explained

| Parameter | Purpose | Why Needed |
|-----------|---------|------------|
| `amd_iommu=on` | Enables AMD IOMMU in the CPU | **Note: Enabled by default on modern kernels (5.x+)**. Explicitly setting it ensures IOMMU is active on older systems or when BIOS settings are ambiguous. Can be omitted on recent systems. |
| `iommu=pt` | Sets IOMMU to passthrough mode | Improves performance by only using IOMMU for passed-through devices, not all devices. Host devices use direct DMA, reducing overhead. |
| `video=efifb:off` | Disables EFI framebuffer | Prevents the host from claiming the GPU's display output. **Note: Has minimal effect on modern kernels**, but included for compatibility with older systems. |
| `initcall_blacklist=sysfb_init` | Prevents sysfb (simple framebuffer) initialization | **Systemd-boot only**. Blocks the kernel's simple framebuffer driver from claiming the GPU during early boot. More effective than `video=efifb:off` on modern kernels. Ensures clean GPU handoff to VFIO. |

**How it works:**

- These parameters are read by the kernel during boot
- IOMMU hardware is initialized early in boot process
- Creates IOMMU groups that isolate PCI devices
- **Proxmox systemd-boot**: Requires `proxmox-boot-tool refresh` → `update-initramfs -u` → reboot
- **Proxmox GRUB**: Requires `update-grub` → `update-initramfs -u` → reboot

**Modern Kernel Behavior (Linux 5.x+):**
- `amd_iommu=on` and `intel_iommu=on` are **enabled by default** if IOMMU is enabled in BIOS
- You can verify IOMMU is active without these flags by checking `dmesg | grep -i iommu`
- The flags are still commonly used for:
  - Ensuring compatibility with older systems
  - Making configuration explicit and self-documenting
  - Troubleshooting when BIOS settings are unclear

**Verification:**
```bash
# Check IOMMU is enabled (works with or without amd_iommu=on on modern kernels)
dmesg | grep -i "AMD-Vi"
# Expected output:
# AMD-Vi: IOMMU performance counters supported
# AMD-Vi: Found IOMMU at 0000:00:00.2 cap 0x40
# AMD-Vi: Interrupt remapping enabled

# Check IOMMU groups exist
find /sys/kernel/iommu_groups/ -type l | wc -l
# Should return a number > 0

# List all IOMMU groups with devices
for d in /sys/kernel/iommu_groups/*/devices/*; do
    n=${d#*/iommu_groups/*}; n=${n%%/*}
    printf 'IOMMU Group %s ' "$n"
    lspci -nns "${d##*/}"
done

# Verify passthrough mode is active
dmesg | grep -i "iommu.*passthrough"
# Expected: "iommu: Default domain type: Passthrough"
```

---

### 2. VFIO Kernel Modules (`/etc/modules-load.d/vfio.conf`)

```shell
vfio
vfio_iommu_type1
vfio_pci
vfio_virqfd
```

#### Modules Explained

| Module | Function | Technical Details |
|--------|----------|-------------------|
| `vfio` | Core VFIO framework | Base infrastructure for device passthrough. Provides `/dev/vfio/vfio` character device. |
| `vfio_iommu_type1` | IOMMU backend for x86/AMD64 | Handles memory mapping between VM (guest physical addresses) and host (machine addresses). Ensures memory isolation. |
| `vfio_pci` | PCI device driver for VFIO | Binds to PCI devices and exposes them to userspace (QEMU). Implements PCI config space access, BAR mapping, and interrupt handling. |
| `vfio_virqfd` | Virtual IRQ file descriptor support | Allows efficient interrupt injection into VMs using eventfd mechanism. Reduces latency for device interrupts. |

**Module Loading Order:**
1. `vfio` (base framework)
2. `vfio_iommu_type1` (IOMMU backend)
3. `vfio_pci` (PCI device driver)
4. `vfio_virqfd` (interrupt handling)

**How it works:**
- Loaded automatically during boot by systemd-modules-load.service
- Must be loaded BEFORE native GPU drivers try to claim the device
- Creates `/dev/vfio/` directory with device groups

**Verification:**
```bash
# Check modules are loaded
lsmod | grep vfio

# Check VFIO devices
ls -la /dev/vfio/
```

---

### 3. VFIO Driver Binding (`/etc/modprobe.d/vfio.conf`)

```bash
options vfio-pci ids=1002:1586,1002:1640 disable_vga=1
softdep amdgpu pre: vfio-pci
softdep snd_hda_intel pre: vfio-pci
```

#### Configuration Explained

| Directive | Purpose | How It Works |
|-----------|---------|--------------|
| `options vfio-pci ids=...` | Tells vfio-pci which devices to bind | When vfio-pci module loads, it automatically claims devices with these PCI vendor:device IDs. Alternative to manual driver_override. |
| `disable_vga=1` | Disables VGA legacy routing on host | Prevents the host firmware/BIOS from using the GPU for VGA legacy mode. Essential when using `x-vga=1` in VM config - allows clean VGA handoff from host to guest. Prevents conflicts over VGA resources. |
| `softdep amdgpu pre: vfio-pci` | Dependency ordering | Ensures vfio-pci loads BEFORE amdgpu driver. Prevents amdgpu from claiming the GPU first. |
| `softdep snd_hda_intel pre: vfio-pci` | Dependency ordering for audio | Ensures vfio-pci binds to GPU's audio device before snd_hda_intel. |

**PCI IDs Format:**
- `1002:1586` = AMD GPU (Vendor: 1002 = AMD, Device: 1586 = specific GPU model)
- `1002:1640` = AMD GPU Audio Controller

**How to find your PCI IDs:**
```bash
lspci -nn | grep -i vga
# Output: 03:00.0 VGA compatible controller [0300]: Advanced Micro Devices, Inc. [1002:1586]

lspci -nn | grep -i audio | grep -i amd
# Output: 03:00.1 Audio device [0403]: Advanced Micro Devices, Inc. [1002:1640]
```

**Why use softdep:**
- `softdep` controls module load order to ensure vfio-pci loads first
- Works in conjunction with blacklisting for a robust configuration
- Provides explicit dependency declarations for the module loading system

---

### 4. Driver Blacklisting (`/etc/modprobe.d/blacklist-gpu.conf`)

```bash
blacklist amdgpu
blacklist radeon
blacklist snd_hda_intel
```

#### Blacklist Explained

| Module | Why Blacklisted | Details |
|--------|-----------------|---------|
| `amdgpu` | AMD GPU driver | **Now blacklisted** to prevent any possibility of the host driver claiming the GPU. Ensures vfio-pci has exclusive control. |
| `radeon` | Legacy AMD GPU driver | Might interfere with newer AMD GPUs. Safe to blacklist. |
| `snd_hda_intel` | Generic Intel HDA audio driver | Tries to claim GPU's audio device. Must be blocked. |

**Why blacklist amdgpu:**
- Provides the strongest guarantee that vfio-pci binds to the GPU
- Eliminates any timing issues during module loading
- The softdep directive alone may not be sufficient in all boot scenarios
- Host doesn't need the AMD GPU driver since the GPU is dedicated to VMs

**VFCT Extraction Method:**
Modern AMD systems provide a VFCT (VBIOS Fetch Table) ACPI table that contains pre-extracted VBIOS images:
1. VFCT table available at `/sys/firmware/acpi/tables/VFCT`
2. Extraction tool reads VBIOS directly from firmware table
3. No need for the amdgpu driver to be active on the host

---

### 5. VBIOS ROM File (`vbios_1002_1586.bin`)

#### What is VBIOS?

**VBIOS (Video BIOS)** is firmware that:
- Initializes GPU hardware registers
- Sets up memory timings and clocks
- Configures display outputs
- Provides basic display functionality before OS loads

**Why needed for passthrough:**
- VMs can't access physical VBIOS ROM chip on GPU
- GPU needs initialization code during VM boot
- Without VBIOS, GPU may not function or boot at all in VM

#### Extraction Process

The VFCT (VBIOS Fetch Table) ACPI table extraction method:

```c
// VFCT-based extraction (simplified)
// Read VFCT table from firmware
fp = fopen("/sys/firmware/acpi/tables/VFCT", "r");
fread(&vfct_header, sizeof(UEFI_ACPI_VFCT), 1, fp);

// Parse VBIOS images from table
while (offset < table_size) {
    GOP_VBIOS_CONTENT* vbios = (GOP_VBIOS_CONTENT*)((char*)pvfct + offset);

    // Extract VBIOS to file
    sprintf(filename, "vbios_%04x_%04x.bin", vendor_id, device_id);
    fwrite(&vbios->VbiosContent, 1, vbios->ImageLength, output_file);

    offset += sizeof(VFCT_IMAGE_HEADER) + vbios->ImageLength;
}
```

**Technical details:**
- VFCT table structure defined by AMD ACPI specification
- Contains multiple VBIOS images (one per GPU)
- Includes PCI bus/device/function information
- Size typically 32KB-128KB per VBIOS (ours: 17,408 bytes)
- Table location: `/sys/firmware/acpi/tables/VFCT`

**Extraction tool:**
Our implementation is in `scripts/vbios-extract.c`:
```bash
# Compile
gcc -O2 -Wall -Wextra scripts/vbios-extract.c -o vbios-extract

# Run (works even with vfio-pci driver)
sudo ./vbios-extract
```

**Usage in VM:**
```bash
# Proxmox VM configuration
hostpci0: 03:00,pcie=1,romfile=vbios_1002_1586.bin
```

QEMU loads this file and presents it to VM's firmware as if reading from physical ROM chip.

---

### 6. AMDGopDriver.rom (UEFI GOP Driver)

#### What is GOP?

**GOP (Graphics Output Protocol)** is a UEFI interface that:
- Provides basic graphics output before OS loads
- Enables UEFI firmware to display boot menus
- Required for UEFI boot with GPU passthrough
- Standard defined by UEFI specification

**Without GOP driver:**
- UEFI boot screen is blank
- Can't see BIOS/UEFI menus
- Can't see bootloader (GRUB) output
- OS still works once loaded

**With GOP driver:**
- Full display output from power-on
- UEFI menus visible
- Bootloader visible
- Proper UEFI graphics initialization

#### How it works

```
VM Boot Sequence with GOP:
┌──────────────────────────────────────────────────┐
│ 1. QEMU starts VM with OVMF UEFI firmware        │
│    ↓                                              │
│ 2. OVMF loads AMDGopDriver.rom                   │
│    ↓                                              │
│ 3. GOP driver initializes passed-through GPU     │
│    ↓                                              │
│ 4. UEFI displays boot menu on GPU output         │
│    ↓                                              │
│ 5. OS bootloader (GRUB) displays menu            │
│    ↓                                              │
│ 6. OS loads and native AMD driver takes over     │
└──────────────────────────────────────────────────┘
```

**File details:**
- Size: 92,672 bytes
- Format: UEFI PE32+ executable
- Contains: AMD-specific GOP implementation
- Loaded by: OVMF UEFI firmware

**VM Configuration:**
```bash
# Option 1: System-wide
cp AMDGopDriver.rom /usr/share/kvm/

# Option 2: VM-specific
args: -device vfio-pci,host=03:00.0,romfile=/usr/share/kvm/AMDGopDriver.rom
```

---

## Boot Sequence

### Host Boot Flow

```shell
1. Bootloader (GRUB or systemd-boot) loads kernel with parameters
   └─ amd_iommu=on iommu=pt video=efifb:off initcall_blacklist=sysfb_init
   └─ Configuration from /etc/kernel/cmdline (systemd-boot) or /etc/default/grub (GRUB)
   └─ Must run proxmox-boot-tool refresh to apply changes

2. Kernel initializes IOMMU hardware
   └─ Creates IOMMU groups for device isolation
   └─ sysfb_init blocked (systemd-boot) - prevents simple framebuffer from claiming GPU

3. initramfs loads VFIO modules early
   └─ vfio → vfio_iommu_type1 → vfio_pci → vfio_virqfd

4. vfio-pci binds to GPU (via ids= parameter)
   └─ Happens BEFORE amdgpu can claim device

5. Remaining host drivers load
   └─ amdgpu loads but finds no devices to claim

6. System ready for VM with GPU passthrough
```

### VM Boot Flow with Passthrough GPU

```shell
1. QEMU/KVM starts VM
   └─ Opens /dev/vfio/X for GPU access

2. OVMF (UEFI firmware) initializes
   └─ Loads AMDGopDriver.rom
   └─ GOP driver initializes GPU via VFIO

3. UEFI displays boot menu
   └─ Output visible on GPU display

4. Bootloader (GRUB) loads
   └─ Uses GOP for display

5. Guest OS kernel boots
   └─ Loads native AMD GPU driver
   └─ Driver sees GPU as physical hardware

6. GPU fully functional in guest
   └─ DirectX/Vulkan/OpenGL work natively
```

---

## How It Works: Deep Dive

### Memory Isolation with IOMMU

```shell
Without IOMMU:
┌─────────┐
│   VM    │ ──→ DMA Request ──→ ANY PHYSICAL MEMORY (Security Risk!)
└─────────┘

With IOMMU:
┌─────────┐
│   VM    │ ──→ DMA Request ──→ IOMMU ──→ Authorized VM Memory Only
└─────────┘                      (Translation + Protection)
```

**IOMMU provides:**
1. **Address Translation**: Guest Physical Address → Host Machine Address
2. **Access Control**: Device can only access VM's allocated memory
3. **Interrupt Remapping**: Securely delivers device interrupts to correct VM

### VFIO Architecture

```shell
┌──────────────────────────────────────────────────────────┐
│                         Userspace                        │
│  ┌─────────────────────────────────────────────────┐     │
│  │              QEMU/KVM Process                   │     │
│  │  ┌────────────────────────────────────────┐     │     │
│  │  │  Device Emulation (PCI Config, BARs)   │     │     │
│  │  └────────────────────────────────────────┘     │     │
│  │              ↕ ioctl()                          │     │
│  └─────────────────────────────────────────────────┘     │
├──────────────────────────────────────────────────────────┤
│                      Kernel Space                        │
│  ┌────────────────────────────────────────────────┐      │
│  │            VFIO Subsystem                      │      │
│  │  - /dev/vfio/vfio (container)                  │      │
│  │  - /dev/vfio/1 (IOMMU group)                   │      │
│  │                                                │      │
│  │  ┌──────────────┐  ┌─────────────────────┐     │      │
│  │  │  vfio-pci    │  │  vfio_iommu_type1   │     │      │
│  │  │  (Device)    │  │  (IOMMU Backend)    │     │      │
│  │  └──────────────┘  └─────────────────────┘     │      │
│  └────────────────────────────────────────────────┘      │
│              ↕                    ↕                      │
│  ┌──────────────────────┐  ┌──────────────────────┐      │
│  │   PCI Subsystem      │  │   IOMMU Driver       │      │
│  └──────────────────────┘  └──────────────────────┘      │
├──────────────────────────────────────────────────────────┤
│                        Hardware                          │
│  ┌──────────────────────┐  ┌──────────────────────┐      │
│  │   AMD GPU (03:00.0)  │  │   AMD IOMMU          │      │
│  └──────────────────────┘  └──────────────────────┘      │
└──────────────────────────────────────────────────────────┘
```

### Driver Binding Priority

```shell
Boot Priority Order (via softdep):

1. vfio-pci loads (softdep places it first)
   └─ Checks ids= parameter
   └─ Binds to 1002:1586 and 1002:1640

2. amdgpu loads (but devices already claimed)
   └─ Finds no devices with its supported IDs
   └─ Stays loaded but inactive

VBIOS Extraction:
# VFCT table is always available from firmware
sudo ./vbios-extract

# Extracts directly from /sys/firmware/acpi/tables/VFCT
# Works regardless of which driver is bound to GPU
```

## References

- [Proxmox Boot Configuration Documentation](https://pve.proxmox.com/pve-docs/chapter-sysadmin.html#sysboot) - Official bootloader configuration guide
- [Proxmox PCI Passthrough](https://pve.proxmox.com/wiki/PCI_Passthrough) - Official PCI passthrough documentation
- [Linux VFIO Documentation](https://www.kernel.org/doc/html/latest/driver-api/vfio.html) - Kernel VFIO framework
- [AMD IOMMU Specification](https://www.amd.com/en/support/tech-docs) - AMD virtualization technology
- [UEFI GOP Specification](https://uefi.org/specifications) - Graphics Output Protocol specification
- [QEMU Documentation](https://www.qemu.org/docs/master/system/devices/vfio-pci.html) - QEMU VFIO-PCI device
- [Ryzen GPU Passthrough Guide (VFCT Method)](https://github.com/isc30/ryzen-gpu-passthrough-proxmox) - VFCT extraction method
- [Strix Halo Wiki - VM iGPU Passthrough](https://strixhalo.wiki/Guides/VM-iGPU-Passthrough/) - Framework 13 specific guide
