# LensorOS

A 64-bit all-inclusive operating system, from bootloader to userspace.

## Migration

LensorOS is migrating away from GitHub, in favor of the FOSS Codeberg.

[LensorOS on Codeberg](https://codeberg.org/LensPlaysGames/LensorOS)

---

## Table of Contents

- [Running LensorOS](#run)
- [Building LensorOS](#build)

---

## Running LensorOS <a name="run"></a>

Free, compatible virtual machines:
- [VirtualBox (VBOX)](https://www.virtualbox.org/wiki/Downloads)
- [VMWare Workstation Player](https://www.vmware.com/products/workstation-player.html)
- [Quick Emulator (QEMU)](https://www.qemu.org/download/)

---

### I just want to open LensorOS!

If you are just interested in poking around LensorOS, and not editing
code, I recommend a pre-built release from the
[releases page](https://codeberg.org/LensPlaysGames/LensorOS/releases).
It will include all the necessary resources and instructions on how
to run LensorOS. Keep in mind that this will be missing a lot of
features to ensure maximum compatibility across systems. By building
from source, you are able to build for your exact system and get every
possible feature enabled.

---

#### Launching LensorOS using the Build System

NOTE: There is no automation for anything except QEMU for now. There
are, however, instructions on how to setup a virtual machine in
VirtualBox and VMWare Workstation Player.

When the CMake build system is generated, it looks for QEMU on your
system; if it finds it, it will add the following targets to the
project. Invoke them to launch QEMU from the corresponding LensorOS
boot media.

The targets:
- `run_qemu`    -- Straight from directory that mimics LensorOS.img (fastest)
- `runimg_qemu` -- LensorOS.img
- `runhda_qemu` -- LensorOS.bin
- `runiso_qemu` -- LensorOS.iso

Assuming the CMake build system was generated in the `kernel/bld/`
subdirectory, invoke like:
```sh
cmake --build kernel/bld --target <name of target>
```

#### VirtualBox

1. Open VirtualBox.
2. Click the `New` button to create a new virtual machine (VM).
3. Give the VM a name and a file path you are comfortable with.
4. Select Type of `Other` and Version of `Other/Unknown (64-bit)`.
5. Leave the memory size how it is; 64MB is plenty at this time.
6. Select the `Do not add a virtual hard disk` option.
7. Click the `Create` button to create the new virtual machine.
8. Select the new VM in the list on the left, then click the `Settings` button.
9. Navigate to `System` within the list on the left.
    1. Change Chipset to `ICH9`.
    2. Enable Extended Feature `Enable EFI (special OSes only)`.
10. Navigate to `Storage` within the list on the left.
    1. Right click the default controller (`IDE`), and select `Remove Controller`.
    2. Right click the area labeled `Storage Devices`, and select `AHCI (SATA)`.
    3. Right click the new AHCI storage controller, and select either `Optical Drive` or `Hard Disk` depending on whether you'd like to boot from the `.iso` or `.bin`, respectively.
    4. Click `Add` in the new Virtual Media Selector window that pops up.
    5. Browse to this folder and, depending on whether `Optical Drive` or `Hard Disk` was selected, choose either `LensorOS.iso` or `LensorOS.bin`.
11. Navigate to `Network` within the list on the left.
    1. Disable all network adapters.
12. Navigate to `Serial Ports` within the list on the left.
    1. Ensure `Enable Serial Port` is checked, `COM1` is selected, `IRQ` is `4`, and `I/O Port` is `0x3F8`.

#### VMWare

1. Open VMWare Workstation Player
2. Select `Home` in the list on the left side. Click `Create a New Virtual Machine` on the right.
3. Select the `I will install the operating system later.` option.
4. Select a guest OS of `Other`, and a Version of `Other 64-bit`.
5. Give the VM a name and path you are comfortable with. Keep note of the path.
6. It will ask about a disk, but the disk it's asking about won't be used. Click next.
7. The next screen should be an overview of the virtual machine hardware. Click `Customize Hardware...`.
    1. Select `New CD/DVD` on the left, then click `Advanced...` on the right.
    2. Select `SATA`, then click `OK`.
    3. On the right, select `Use ISO image file`, and then click `Browse...`.
    4. Select the `LensorOS.iso` image file (located in `kernel/bin/`).
    5. Select the hard drive that we skipped configuring in the list on the left.
    6. Remove the hard drive using the `Remove` button near the bottom center.
    7. Remove any and all network adapters and sound cards in the same manner.
    5. Click `Close` in the bottom right to close the hardware configuration window.
8. Click `Finish`.
9. Navigate to the path specified in step #5, where the virtual machine is located.
    1. Open the file ending with `.vmx` in a text editor.
    2. Add the following line of text: `firmware="efi"`.
    3. Save the file, then close it.

You will have to select `UEFI Shell` once VMware Workstation
boots into LensorOS (even if it says something like `Unsupported`).

---

## Building LensorOS  <a name="build"></a>

There are multiple steps in the LensorOS build process, outlined here.
1. [Dependencies](#dependencies)
2. [Bootloader](#bootloader)
3. [LensorOS Toolchain + Kernel](#toolchain-kernel)
4. [Boot Media Generation](#boot-media-generation)

NOTE: All blocks of shell commands given are expected to start
with the working directory at the root of the repository.

---

### Dependencies <a name="dependencies"></a>

Download and install the following project-wide dependencies if you
don't have them already, or if the version you have isn't up to date.

- A 64-bit version of the GNU toolset for your host OS.
  - Debian distros: `sudo apt install build-essential make`
  - Windows:
    - [Cygwin](https://cygwin.com/install.html)
    - [MinGW-w64](https://sourceforge.net/projects/mingw-w64/)
    - [MSYS2](https://www.msys2.org/)
    - [TDM64](https://jmeubank.github.io/tdm-gcc/download/)
    - [Windows Subsystem for Linux](https://docs.microsoft.com/en-us/windows/wsl/about)
- [CMake >= 3.20](https://www.cmake.org/download/)
- [Git](https://git-scm.com/downloads)
- [Netwide Assembler (nasm)](https://www.nasm.us)

Next, clone the source code from the repository. If you would like to
edit the code and make contributions, be sure to fork first and clone
from that repository.
```sh
git clone https://codeberg.org/LensPlaysGames/LensorOS.git
```

This will create a subdirectory titled `LensorOS` with the contents of
this repository in the current working directory.

---

### Bootloader <a name="bootloader"></a>

NOTE: This section **is going to change**, and any information here may
become incorrect or out of date at any moment. This is due to being in
the middle of migrating bootloaders to the self-created RADII bootloader.

The bootloader is an EFI application; specifically an OS loader written
for the [UEFI spec. (currently V2.9)](https://uefi.org/specifications/).
That specification outlines the use of PE32+ executables with a
specific subsystem. As you may know, the PE32+ format is also used by
Windows as it's executable format. This means that a compiler that
generates Windows executables will generate the proper format of
executable for an EFI application, given the subsystem modification.
However, twenty or so years ago, GNU decided to write custom relocation
linker scripts that create PE32+ executables from ELF executables. This
means that a compiler that generates ELF executables is used, then that
executable is transformed into a PE32+ executable with the proper
subsystem for an EFI application. Luckily, all of this is handled by a
Makefile.

Build the dependencies for the bootloader:
```sh
cd gnu-efi
make
```
That only ever has to be done once, to generate `libgnuefi.a`.

From here, the bootloader executable can be built using the
`bootloader` make target:
```sh
cd gnu-efi
make bootloader
```

---

### Toolchain + Kernel <a name="toolchain-kernel"></a>

[See the toolchain README](toolchain/README.md)

Once the toolchain is usable, continue on here.

I recommend taking a look at `toolchain/config.cmake` and seeing what
there is to fiddle with, but going with the defaults is just as well.

First, generate a build system using CMake. If you choose a different
build system, keep in mind not all build systems honour our request to
use a custom toolchain. I recommend Ninja, as it can speed up build
times. Another tip to speed up build times; install `ccache`. The CMake
scripts in this project detect and use it automatically.
```shell
cmake -G Ninja -B bld
```

**PLEASE BE ADVISED** that there are critical messages in the output of
the above command that may detail critical targets haven't been created
due to programs not being found on the system.


To build libc, userspace programs, and finally the kernel executable,
invoke the build system generated by CMake:
```shell
cmake --build bld
```

CMake will create certain targets if the proper dependencies are
detected on the system.

To see a list of all available targets, use the following command:
```shell
cmake --build bld --target help
```

Lens' most-used targets:
- `runhda_qemu`
- `image_gpt`
- `image_vdi`
- `userspace`
- `kernel`

---

### Boot Media Generation <a name="boot-media-generation"></a>

Here is a list of the current build targets relating to boot media
generation, as well as their dependencies listed underneath each.
- `image_raw` --
  Combine built executables and resources to generate UEFI-compatible FAT32 boot media.
  - The built bootloader EFI application at `gnu-efi/x86_64/bootloader/main.efi`.
  - `Kernel` build target (relies on `kernel/bin/kernel.elf`).
  - dd -- Native command on Unix
    - On Windows, use one of the following options:
      - [MinGW installer to get MSYS coreutils ext package](https://osdn.net/projects/mingw/)
      - [Cygwin](https://www.cygwin.com/)
      - Windows Subsystem for Linux
  - GNU mtools
    - [Home Page](https://www.gnu.org/software/mtools/)
    - Debian distros: `sudo apt install mtools`
    - [Pre-built binaries for Windows](https://github.com/LensPlaysGames/mtools/releases)
- `image_gpt` --
  Create GPT-partitioned, bootable hard drive image from FAT32 boot media.
  - `image_raw`
  - One of the following:
    - fdisk -- Native command on Unix
    - createGPT
      - [Repository](https://github.com/LensPlaysGames/createGPT)
      - [Pre-built binaries for Linux + Windows](https://github.com/LensPlaysGames/createGPT/releases)
      - I was annoyed that there wasn't a (working) cross platform tool for
        creating GPT disk images that was open source, and easy to use, so I made one.
    - mkgpt
      - [Repository](https://github.com/jncronin/mkgpt)
      - On Unix, use the automatic build + install script in the `scripts` subdirectory.
- `image_iso` --
  Create ISO-9660 "El-Torito" bootable CD-ROM image from FAT32 boot media.
  - `image_raw`
  - GNU xorriso
    - [Home Page](https://www.gnu.org/software/xorriso/)
    - Debian distros: `sudo apt install xorriso`
    - [Pre-built binaries for Windows](https://github.com/PeyTy/xorriso-exe-for-windows)
- `image_vdi` --
  Convert GPT-partitioned raw disk image into Virtual Disk Image format (`.vdi`).
  - `image_gpt`
  - [qemu-img](https://www.qemu.org/download/)

As an example, a FAT32 formatted UEFI-compatible boot image may be
generated using the following command:
```sh
cmake --build kernel/bld --target image_raw
```

It takes just one command to build the LensorOS kernel, generate new
boot media, and then launch the QEMU virtual machine into LensorOS.
```sh
cmake --build kernel/bld --target runimg_qemu
```

---

### Birthday

Work on LensorOS began on January 9th, 2022.

---
