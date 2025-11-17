# Spark Bare Metal - Lab Experiments

## ⚠️ Disclaimer

This repository contains **experimental configurations** and lab setups used for learning and testing purposes.

**Use at your own risk. No warranty provided.**

This is a collection of experiments and proof-of-concepts - not production-ready code. The configurations may be incomplete, contain bugs, or become outdated.

## Purpose

These experiments are part of the **Framework Cluster Project**, focused on:

- Understanding Proxmox VE configuration for bare metal deployments
- Configuring AMD iGPU passthrough for virtual machines
- Building a Kubernetes cluster for ML/AI workloads
- Running inference and other GPU-accelerated applications

## Project Context

This is a learning project to explore:

- Infrastructure as Code (IaC) with Ansible
- GPU virtualization and passthrough technologies
- Kubernetes cluster management on bare metal
- ML inference workloads on consumer hardware (Framework laptops)

## What's Included

### Ansible Roles

- **`igpu-passthrough`** - Configure AMD iGPU passthrough on Proxmox VE hosts
  - Bootloader configuration (GRUB/systemd-boot)
  - IOMMU and VFIO setup
  - GPU driver blacklisting
  - VBIOS extraction and management
  - Comprehensive verification checks

## Resources

This project uses resources from:

- **[Strix Halo Wiki](https://strixhalo.wiki/Guides/VM-iGPU-Passthrough)** - AMD iGPU passthrough guide and AMDGopDriver.rom file
- **[Framework Community](https://community.frame.work/t/anyone-using-proxmox-ve/74863/6)** - Anyone using Proxmox VE?

## Contributing

Feel free to open issues or submit pull requests if you find bugs or have improvements to suggest. However, keep in mind this is primarily a personal learning project.

## License

Unless otherwise specified, the contents of this repository are provided as-is for educational purposes.
