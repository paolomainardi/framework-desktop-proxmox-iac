/*
 * VBIOS Extraction Tool for AMD GPUs
 *
 * This tool extracts the Video BIOS ROM from AMD GPUs using the VFCT ACPI table.
 * The VFCT (VBIOS Fetch Table) contains pre-extracted VBIOS images from the firmware.
 *
 * This method is more reliable than direct ROM reading and works even when the GPU
 * is bound to vfio-pci driver.
 *
 * Based on: https://github.com/isc30/ryzen-gpu-passthrough-proxmox
 *
 * Requirements:
 *   - VFCT ACPI table must be available (/sys/firmware/acpi/tables/VFCT)
 *   - Root/sudo privileges required
 *   - gcc compiler to build
 *
 * Usage:
 *   gcc vbios-extract.c -o vbios-extract
 *   sudo ./vbios-extract
 *
 * Output:
 *   Creates vbios_<vendor>_<device>.bin files in current directory
 *   Example: vbios_1002_1586.bin
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef uint32_t ULONG;
typedef uint8_t UCHAR;
typedef uint16_t USHORT;

typedef struct {
    ULONG Signature;
    ULONG TableLength; // Length
    UCHAR Revision;
    UCHAR Checksum;
    UCHAR OemId[6];
    UCHAR OemTableId[8]; // UINT64  OemTableId;
    ULONG OemRevision;
    ULONG CreatorId;
    ULONG CreatorRevision;
} AMD_ACPI_DESCRIPTION_HEADER;

typedef struct {
    AMD_ACPI_DESCRIPTION_HEADER SHeader;
    UCHAR TableUUID[16]; // 0x24
    ULONG VBIOSImageOffset; // 0x34. Offset to the first GOP_VBIOS_CONTENT block from the beginning of the structure.
    ULONG Lib1ImageOffset; // 0x38. Offset to the first GOP_LIB1_CONTENT block from the beginning of the structure.
    ULONG Reserved[4]; // 0x3C
} UEFI_ACPI_VFCT;

typedef struct {
    ULONG PCIBus; // 0x4C
    ULONG PCIDevice; // 0x50
    ULONG PCIFunction; // 0x54
    USHORT VendorID; // 0x58
    USHORT DeviceID; // 0x5A
    USHORT SSVID; // 0x5C
    USHORT SSID; // 0x5E
    ULONG Revision; // 0x60
    ULONG ImageLength; // 0x64
} VFCT_IMAGE_HEADER;

typedef struct {
    VFCT_IMAGE_HEADER VbiosHeader;
    UCHAR VbiosContent[1];
} GOP_VBIOS_CONTENT;

int main(int argc, char** argv)
{
    FILE* fp_vfct;
    FILE* fp_vbios;
    UEFI_ACPI_VFCT* pvfct;
    char vbios_name[0x400];
    int extracted_count = 0;

    printf("AMD VBIOS Extraction Tool (VFCT Method)\n");
    printf("========================================\n\n");

    // Open VFCT ACPI table
    if (!(fp_vfct = fopen("/sys/firmware/acpi/tables/VFCT", "r"))) {
        fprintf(stderr, "Error: Cannot open /sys/firmware/acpi/tables/VFCT: %s\n", strerror(errno));
        fprintf(stderr, "\nPossible reasons:\n");
        fprintf(stderr, "  - Not running with root/sudo privileges\n");
        fprintf(stderr, "  - VFCT table not available on this system\n");
        fprintf(stderr, "  - System firmware doesn't provide VFCT table\n");
        fprintf(stderr, "  - No AMD GPU with VFCT support present\n");
        fprintf(stderr, "\nPlease run with sudo: sudo %s\n", argv[0]);
        return 1;
    }

    // Allocate memory for VFCT header
    if (!(pvfct = malloc(sizeof(UEFI_ACPI_VFCT)))) {
        fprintf(stderr, "Error: Failed to allocate memory: %s\n", strerror(errno));
        fclose(fp_vfct);
        return 1;
    }

    // Read VFCT header
    if (sizeof(UEFI_ACPI_VFCT) != fread(pvfct, 1, sizeof(UEFI_ACPI_VFCT), fp_vfct)) {
        fprintf(stderr, "Error: Failed to read VFCT header\n");
        free(pvfct);
        fclose(fp_vfct);
        return 1;
    }

    ULONG offset = pvfct->VBIOSImageOffset;
    ULONG tbl_size = pvfct->SHeader.TableLength;

    printf("VFCT Table Information:\n");
    printf("  Table Length: %u bytes\n", tbl_size);
    printf("  VBIOS Image Offset: 0x%x\n", offset);
    printf("\n");

    // Reallocate memory for full VFCT table
    if (!(pvfct = realloc(pvfct, tbl_size))) {
        fprintf(stderr, "Error: Failed to reallocate memory: %s\n", strerror(errno));
        fclose(fp_vfct);
        return 1;
    }

    // Read remaining VFCT data
    if (tbl_size - sizeof(UEFI_ACPI_VFCT) != fread(pvfct + 1, 1, tbl_size - sizeof(UEFI_ACPI_VFCT), fp_vfct)) {
        fprintf(stderr, "Error: Failed to read VFCT body\n");
        free(pvfct);
        fclose(fp_vfct);
        return 1;
    }

    fclose(fp_vfct);

    printf("Extracting VBIOS images...\n\n");

    // Parse VBIOS images from VFCT table
    while (offset < tbl_size) {
        GOP_VBIOS_CONTENT* vbios = (GOP_VBIOS_CONTENT*)((char*)pvfct + offset);
        VFCT_IMAGE_HEADER* vhdr = &vbios->VbiosHeader;

        // End of VBIOS images
        if (!vhdr->ImageLength)
            break;

        printf("Found VBIOS:\n");
        printf("  Vendor ID: 0x%04x\n", vhdr->VendorID);
        printf("  Device ID: 0x%04x\n", vhdr->DeviceID);
        printf("  PCI Bus: %u, Device: %u, Function: %u\n", vhdr->PCIBus, vhdr->PCIDevice, vhdr->PCIFunction);
        printf("  Image Length: %u bytes\n", vhdr->ImageLength);

        snprintf(vbios_name, sizeof(vbios_name), "vbios_%x_%x.bin", vhdr->VendorID, vhdr->DeviceID);

        if (!(fp_vbios = fopen(vbios_name, "wb"))) {
            fprintf(stderr, "  Error: Failed to create %s: %s\n", vbios_name, strerror(errno));
            offset += sizeof(VFCT_IMAGE_HEADER);
            offset += vhdr->ImageLength;
            continue;
        }

        if (vhdr->ImageLength != fwrite(&vbios->VbiosContent, 1, vhdr->ImageLength, fp_vbios)) {
            fprintf(stderr, "  Error: Failed to write VBIOS data to %s\n", vbios_name);
            fclose(fp_vbios);
            offset += sizeof(VFCT_IMAGE_HEADER);
            offset += vhdr->ImageLength;
            continue;
        }

        fclose(fp_vbios);
        extracted_count++;

        printf("  ✓ Extracted to: %s\n\n", vbios_name);

        offset += sizeof(VFCT_IMAGE_HEADER);
        offset += vhdr->ImageLength;
    }

    free(pvfct);

    // Print summary
    printf("========================================\n");
    printf("Summary:\n");
    printf("  Successfully extracted: %d VBIOS file(s)\n", extracted_count);

    if (extracted_count == 0) {
        printf("\nNo VBIOS files extracted.\n");
        printf("The VFCT table may be empty or corrupted.\n");
        return 1;
    }

    printf("\nVBIOS files have been created in the current directory.\n");
    printf("Copy them to /usr/share/kvm/ on your Proxmox host for VM usage.\n");

    return 0;
}
