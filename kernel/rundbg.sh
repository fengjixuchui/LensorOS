#!/bin/sh
OSName="LensorOS"
BuildDirectory="$0/../bin"
OVMFDirectory="$0/../../OVMFbin"
qemu-system-x86_64 -s -S -cpu qemu64 -m 100M -rtc base=localtime,clock=host,driftfix=none -machine q35 -serial stdio -vga cirrus -drive format=raw,file=$BuildDirectory/$OSName.img -drive if=pflash,format=raw,unit=0,file="$OVMFDirectory/OVMF_CODE-pure-efi.fd",readonly=on -drive if=pflash,format=raw,unit=1,file="$OVMFDirectory/OVMF_VARS-pure-efi.fd" -net none
