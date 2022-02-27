SET BuildDirectory=%0/../bin
SET OVMFbin=%0/../../OVMFbin
SET BuildDirectory=%BuildDirectory:"=%
SET OVMFbin=%OVMFbin:"=%
IF NOT EXIST %OVMFbin%/OVMF_VARS_LensorOS.fd cp %OVMFbin%/OVMF_VARS-pure-efi.fd %OVMFbin%/OVMF_VARS_LensorOS.fd
qemu-system-x86_64 -cpu qemu64 -m 100M -rtc base=localtime,clock=host,driftfix=none -machine q35 -serial stdio -vga cirrus -soundhw pcspk -d cpu_reset -drive format=raw,file=%BuildDirectory%/LensorOS.img -drive if=pflash,format=raw,unit=0,file=%OVMFbin%/OVMF_CODE-pure-efi.fd,readonly=on -drive if=pflash,format=raw,unit=1,file=%OVMFbin%/OVMF_VARS_LensorOS.fd -net none
