/* Copyright 2022, Contributors To LensorOS.
 * All rights reserved.
 *
 * This file is part of LensorOS.
 *
 * LensorOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LensorOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LensorOS. If not, see <https://www.gnu.org/licenses
 */


#include <acpi.h>
#include <ahci.h>
#include <basic_renderer.h>
#include <boot.h>
#include <cpu.h>
#include <cpuid.h>
#include <devices/devices.h>
#include <e1000.h>
#include <efi_memory.h>
#include <elf_loader.h>
#include <gdt.h>
#include <gpt.h>
#include <gpt_partition_type_guids.h>
#include <guid.h>
#include <hpet.h>
#include <interrupts/idt.h>
#include <interrupts/interrupts.h>
#include <interrupts/syscalls.h>
#include <keyboard.h>
#include <kstage1.h>
#include <link_definitions.h>
#include <memory/heap.h>
#include <memory/paging.h>
#include <memory/physical_memory_manager.h>
#include <memory/virtual_memory_manager.h>
#include <mouse.h>
#include <pci.h>
#include <pit.h>
#include <random_lcg.h>
#include <random_lfsr.h>
#include <rtc.h>
#include <scheduler.h>
#include <storage/filesystem_drivers/file_allocation_table.h>
#include <storage/storage_device_driver.h>
#include <system.h>
#include <tss.h>
#include <uart.h>

#include <format>

u8 idt_storage[0x1000];
void prepare_interrupts() {
    // REMAP PIC CHIP IRQs OUT OF THE WAY OF GENERAL SOFTWARE EXCEPTIONS.
    remap_pic();
    // CREATE INTERRUPT DESCRIPTOR TABLE.
    gIDT = IDTR(0x0fff, (u64)&idt_storage[0]);
    // POPULATE TABLE.
    // NOTE: IRQ0 uses this handler by default, but scheduler over-rides this!
    //gIDT.install_handler((u64)system_timer_handler,             PIC_IRQ0);
    gIDT.install_handler((u64)keyboard_handler,                 PIC_IRQ1);
    gIDT.install_handler((u64)uart_com1_handler,                PIC_IRQ4);
    gIDT.install_handler((u64)rtc_handler,                      PIC_IRQ8);
    gIDT.install_handler((u64)mouse_handler,                    PIC_IRQ12);
    gIDT.install_handler((u64)divide_by_zero_handler,           0x00);
    gIDT.install_handler((u64)double_fault_handler,             0x08);
    gIDT.install_handler((u64)stack_segment_fault_handler,      0x0c);
    gIDT.install_handler((u64)general_protection_fault_handler, 0x0d);
    gIDT.install_handler((u64)page_fault_handler,               0x0e);
    gIDT.install_handler((u64)simd_exception_handler,           0x13);
    gIDT.install_handler((u64)system_call_handler_asm,          0x80
                         , IDT_TA_UserInterruptGate);
    gIDT.flush();
}

void draw_boot_gfx() {
    Vector2<u64> drawPosition = { 0, 0 };
    gRend.puts(drawPosition, "<<<!===--- You are now booting into LensorOS ---===!>>>");
    // DRAW A FACE :)
    drawPosition = {420, 420};
    // left eye
    gRend.drawrect(drawPosition, {42, 42}, 0xff00ffff);
    // left pupil
    drawPosition = {440, 440};
    gRend.drawrect(drawPosition, {20, 20}, 0xffff0000);
    // right eye
    drawPosition = {520, 420};
    gRend.drawrect(drawPosition, {42, 42}, 0xff00ffff);
    // right pupil
    drawPosition = {540, 440};
    gRend.drawrect(drawPosition, {20, 20}, 0xffff0000);
    // mouth
    drawPosition = {400, 520};
    gRend.drawrect(drawPosition, {182, 20}, 0xff00ffff);
    gRend.swap();
}

// FXSAVE/FXRSTOR instructions require a pointer to a
//   512-byte region of memory before use.
u8 fxsave_region[512] __attribute__((aligned(16)));

void kstage1(BootInfo* bInfo) {
    /* This function is monstrous, so the functionality is outlined here.
     *     - Disable interrupts (if they weren't already)
     *     - Ensure BootInfo pointer is valid (non-null)
     * x86 - Load Global Descriptor Table
     * x86 - Load Interrupt Descriptor Table
     *     - Prepare UART serial communications driver
     *     - Prepare physical/virtual memory
     *       - Initialize Physical Memory Manager
     *       - Initialize Virtual Memory Manager
     *       - Prepare the heap (`new` and `delete`)
     *     - Prepare Real Time Clock (RTC)
     *     - Setup graphical renderers  -- these will change, and soon
     *       - BasicRenderer      -- drawing pixels to linear framebuffer
     *       - BasicTextRenderer  -- draw keyboard input on screen,
     *                               keep track of text cursor, etc
     *     - Determine and cache information about CPU(s)
     *     - Initialize ACPI
     *     - Enumerate PCI
     *     - Prepare non-PCI devices
     *       - High Precision Event Timer (HPET)
     *       - PS2 Mouse
     *     - Prepare Programmable Interval Timer (PIT)
     * x86 - Setup TSS
     *     - Setup scheduler
     * x86 - Clear (IRQ) interrupt masks in PIC for used interrupts
     *     - Print information about the system to serial output
     *     - Enable interrupts
     *
     * x86 = The step is inherently x86-only (not implementation based).
     *
     * TODO:
     * |-- Update the above list: it's close, but not exact anymore.
     * `-- A lot of hardware is just assumed to be there;
     *     figure out how to better probe for their existence,
     *     and gracefully handle the case that they aren't there.
     *
     *
     */

    // Disable interrupts while doing sensitive
    //   operations (like setting up interrupts :^).
    asm ("cli");

    // Don't even attempt to boot unless boot info exists.
    if (bInfo == nullptr)
        while (true)
            asm ("hlt");

    /* Tell x86_64 CPU where the GDT is located by
     * populating and loading a GDT descriptor.
     * The global descriptor table contains information about
     * memory segments (like privilege level of executing code,
     * or privilege level needed to access data).
     */
    setup_gdt();
    gGDTD.Size = sizeof(GDT) - 1;
    gGDTD.Offset = V2P((u64)&gGDT);
    LoadGDT((GDTDescriptor*)V2P(&gGDTD));

    // Prepare Interrupt Descriptor Table.
    prepare_interrupts();

    // Setup serial communications chip to allow for debug messages as soon as possible.
    UART::initialize();
    std::print("\n"
             "!===--- You are now booting into \033[1;33mLensorOS\033[0m ---===!\n"
             "\n");

    // Setup physical memory allocator from EFI memory map.
    Memory::init_physical(bInfo->map, bInfo->mapSize, bInfo->mapDescSize);
    // Setup virtual memory (map entire address space as well as kernel).
    Memory::init_virtual();
    // Setup dynamic memory allocation (`new`, `delete`).
    init_heap();

    Memory::print_physmem();

    SYSTEM = new System();

    {// Initialize the Real Time Clock.
        gRTC = RTC();
        gRTC.set_periodic_int_enabled(true);
        std::print("[kstage1]: \033[32mReal Time Clock (RTC) initialized\033[0m\n\033[1;33m"
                   "Now is {}:{}:{} on {}-{}-{}"
                   "\033[0m\n\n"
                   , gRTC.Time.hour
                   , gRTC.Time.minute
                   , gRTC.Time.second
                   , gRTC.Time.year
                   , gRTC.Time.month
                   , gRTC.Time.date
                   );
    }

    // Create basic framebuffer renderer.
    std::print("[kstage1]: Setting up Graphics Output Protocol Renderer\n");
    gRend = BasicRenderer(bInfo->framebuffer, bInfo->font);
    std::print("  \033[32mSetup Successful\033[0m\n\n");
    draw_boot_gfx();
    // Create basic text renderer for the keyboard.
    Keyboard::gText = Keyboard::BasicTextRenderer();

    {// Setup random number generators.
        const RTCData& tm = gRTC.Time;
        u64 someNumber =
            tm.century + tm.year
            + tm.month   + tm.date
            + tm.weekday + tm.hour
            + tm.minute  + tm.second;
        gRandomLCG = LCG();
        gRandomLCG.seed(someNumber);
        gRandomLFSR = LFSR();
        gRandomLFSR.seed(gRandomLCG.get(), gRandomLCG.get());
    }

    // Store feature set of CPU (capabilities).
    CPUDescription* SystemCPU = &SYSTEM->cpu();
    // Check for CPUID availability ('ID' bit in rflags register modifiable)
    bool supportCPUID = static_cast<bool>(cpuid_support());
    if (supportCPUID) {
        SystemCPU->set_cpuid_capable();
        std::print("[kstage1]: \033[32mCPUID is supported\033[0m\n");
        char* cpuVendorID = cpuid_string(0);
        SystemCPU->set_vendor_id(cpuVendorID);
        std::print("  CPU Vendor ID: {}\n", std::string_view{SystemCPU->get_vendor_id(), 12});

        CPUIDRegisters regs;
        cpuid(1, regs);

        /* TODO:
         * |-- Get logical/physical core bits from CPUID
         * |   |- 0x0000000b -- Intel
         * |   |- 0x80000008 -- AMD
         * |   `- Otherwise: bits are zero, assume single core.
         * `-- Rewrite task switching code to save/load
         *     all supported registers in CPUState.
         *
         * FIXME: My CPUID definition and implementation needs a lot of work.
         *        A system that doesn't use nested if statements would be great.
         *
         * Enable FXSAVE/FXRSTOR instructions if CPU supports it.
         * If it is not supported, don't bother trying to support
         * FPU, SSE, etc. as there would be no mechanism to
         * save/load the registers on context switch.
         *
         * Current functionality of giant `if` statemnt:
         * |- Setup FXSAVE/FXRSTOR
         * |  |- Setup FPU
         * |  `- Setup SSE
         * `- Setup XSAVE
         *    `- Setup AVX
         *
         * If a feature is present, it's feature flag is set in SystemCPU.
         *
         * To peek further down the rabbit hole, check out the following link:
         *   https://wiki.osdev.org/Detecting_CPU_Topology_(80x86)#Using_CPUID
         */
        if (regs.D & static_cast<u32>(CPUID_FEATURE::EDX_FXSR)) {
            SystemCPU->set_fxsr_capable();
            asm volatile ("fxsave %0" :: "m"(fxsave_region));
            SystemCPU->set_fxsr_enabled();
            // If FXSAVE/FXRSTOR is supported, setup FPU.
            if (regs.D & static_cast<u32>(CPUID_FEATURE::EDX_FPU)) {
                SystemCPU->set_fpu_capable();
                /* FPU supported, ensure it is enabled.
                 * FPU Relevant Control Register Bits
                 * |- CR0.EM (bit 02) -- If set, FPU and vector operations will cause a #UD.
                 * `- CR0.TS (bit 03) -- Task switched. If set, all FPU and vector ops will cause a #NM.
                 */
                asm volatile ("mov %%cr0, %%rdx\n"
                              "mov $0b1100, %%ax\n"
                              "not %%ax\n"
                              "and %%ax, %%dx\n"
                              "mov %%rdx, %%cr0\n"
                              "fninit\n"
                              ::: "rax", "rdx");
                SystemCPU->set_fpu_enabled();
            }
            else {
                // FPU not supported, ensure it is disabled.
                asm volatile ("mov %%cr0, %%rdx\n"
                              "or $0b1100, %%dx\n"
                              "mov %%rdx, %%cr0\n"
                              ::: "rdx");
            }
            // If FXSAVE/FXRSTOR are supported and present, setup SSE.
            if (regs.D & static_cast<u32>(CPUID_FEATURE::EDX_SSE)) {
                SystemCPU->set_sse_capable();
                /* Enable SSE
                 * |- Clear CR0.EM bit   (bit 2  -- coprocessor emulation)
                 * |- Set CR0.MP bit     (bit 1  -- coprocessor monitoring)
                 * |- Set CR4.OSFXSR bit (bit 9  -- OS provides FXSAVE/FXRSTOR functionality)
                 * `- Set CR4.OSXMMEXCPT (bit 10 -- OS provides #XM exception handler)
                 */
                asm volatile ("mov %%cr0, %%rax\n"
                              "and $0b1111111111110011, %%ax\n"
                              "or $0b10, %%ax\n"
                              "mov %%rax, %%cr0\n"
                              "mov %%cr4, %%rax\n"
                              "or $0b11000000000, %%rax\n"
                              "mov %%rax, %%cr4\n"
                              ::: "rax");
                SystemCPU->set_sse_enabled();
            }
        }
        // Enable XSAVE feature set if CPU supports it.
        if (regs.C & static_cast<u32>(CPUID_FEATURE::ECX_XSAVE)) {
            SystemCPU->set_xsave_capable();
            // Enable XSAVE feature set
            // `- Set CR4.OSXSAVE bit (bit 18  -- OS provides )
            asm volatile ("mov %cr4, %rax\n"
                          "or $0b1000000000000000000, %rax\n"
                          "mov %rax, %cr4\n");
            SystemCPU->set_xsave_enabled();
            // If SSE, AND XSAVE are supported, setup AVX feature set.
            if (regs.D & static_cast<u32>(CPUID_FEATURE::EDX_SSE)
                && regs.C & static_cast<u32>(CPUID_FEATURE::ECX_AVX))
            {
                SystemCPU->set_avx_capable();
                asm volatile ("xor %%rcx, %%rcx\n"
                              "xgetbv\n"
                              "or $0b111, %%eax\n"
                              "xsetbv\n"
                              ::: "rax", "rcx", "rdx");
                SystemCPU->set_avx_enabled();
            }
        }
    }
    std::print("\n");

    // Make sure SSE is enabled.
#ifdef __SSE__
    if (!SystemCPU->sse_enabled()) {
        std::print("[kstage1]: \033[31mSSE is not enabled!\033[0m\n");
        std::print("[kstage1]: \033[31mYour CPU doesn’t support SSE. Please recompile the kernel without SSE support.\033[0m\n");
        for (;;) asm volatile ("hlt");
    }
#endif

    // TODO: Parse CPUs from ACPI MADT table.
    //       For now we only support single core.
    CPU cpu = CPU(SystemCPU);
    SystemCPU->add_cpu(cpu);
    SystemCPU->print_debug();

    // Initialize Advanced Configuration and Power Management Interface.
    ACPI::initialize(bInfo->rsdp);

    // Find Memory-mapped ConFiguration Table in order to find PCI devices.
    // Storage devices like AHCIs will be detected here.
    auto* mcfg = (ACPI::MCFGHeader*)ACPI::find_table("MCFG");
    if (mcfg) {
        std::print("[kstage1]: Found Memory-mapped Configuration Space (MCFG) ACPI Table\n"
                   "  Address: {}\n\n", static_cast<void*>(mcfg));
        PCI::enumerate_pci(mcfg);
    }

    /* Probe storage devices
     *
     * Most storage devices handle multiple
     * storage media hardware devices; for example,
     * a single AHCI controller has multiple ports,
     * each one referring to its own device.
     */
    for (usz i = 0; i < SYSTEM->Devices.size(); ++i) {
        auto dev = SYSTEM->Devices[i];
        if (dev->major() == SYSDEV_MAJOR_STORAGE
            && dev->minor() == SYSDEV_MINOR_AHCI_CONTROLLER
            && dev->flag(SYSDEV_MAJOR_STORAGE_SEARCH) != 0)
        {
            std::print("[kstage1]: Probing AHCI Controller\n");
            auto controller = static_cast<Devices::AHCIController*>(dev.get());
            auto* ABAR = reinterpret_cast<AHCI::HBAMemory*>(u64(controller->Header->BAR5));

            // TODO: Better MMIO!! It should be separate from regular virtual mappings, I think.

            // Okay, this bug just showed me how stupid the current system is.
            // VIRTUAL MEMORY MANAGER NEEDS ANOTHER LEVEL OF ABSTRACTION TO THE API
            // I need to add something like `map_sized` that takes in a
            // page table, virtual address, physical address, size of
            // thing at those addresses, and then flags... THEN DO ALL THIS
            // MULTI-PAGE CALCULATION IN THAT FUNCTION. Because having to deal
            // with this everywhere there *might* be a misaligned
            // pointer or a large struct is really hard to do correctly.

            void* containing_page = (void*)((usz)ABAR - ((usz)ABAR % PAGE_SIZE));
            Memory::map(containing_page, containing_page
                        , (u64)Memory::PageTableFlag::Present
                          | (u64)Memory::PageTableFlag::ReadWrite
                        );

            // Handle case where ABAR spans two pages, in which case we have to map both.
            void* next_page = (void*)((usz)containing_page + PAGE_SIZE);
            if (((usz)ABAR + sizeof(AHCI::HBAMemory)) >= (u64)next_page)
                Memory::map(next_page, next_page
                            , (u64)Memory::PageTableFlag::Present
                              | (u64)Memory::PageTableFlag::ReadWrite
                            );

            // Handle case where ABAR spans three pages, in which case we have to map a third.
            next_page = (void*)((usz)next_page + PAGE_SIZE);
            if (((usz)ABAR + sizeof(AHCI::HBAMemory)) >= (u64)next_page)
                Memory::map(next_page, next_page
                            , (u64)Memory::PageTableFlag::Present
                              | (u64)Memory::PageTableFlag::ReadWrite
                            );

            u32 ports = ABAR->PortsImplemented;
            for (uint i = 0; i < 32; ++i) {
                if (ports & (1u << i)) {
                    AHCI::HBAPort* port = &ABAR->Ports[i];
                    AHCI::PortType type = get_port_type(port);
                    if (type != AHCI::PortType::None) {
                        SYSTEM->create_device<Devices::AHCIPort>(std::static_pointer_cast<Devices::AHCIController>(dev), type, i, port);
                    }
                } else break;
            }
            // Don't search AHCI controller any further, already found all ports.
            dev->set_flag(SYSDEV_MAJOR_STORAGE_SEARCH, false);
        }
    }

    /* Find partitions
     * A storage device may be partitioned (i.e. GUID Partition Table).
     * These partitions are to be detected and new system devices created.
     */
    for (usz i = 0; i < SYSTEM->Devices.size(); ++i) {
        auto dev = SYSTEM->Devices[i];
        if (dev->major() == SYSDEV_MAJOR_STORAGE
            && dev->minor() == SYSDEV_MINOR_AHCI_PORT
            && dev->flag(SYSDEV_MAJOR_STORAGE_SEARCH) != 0)
        {
            auto port = static_cast<Devices::AHCIPort*>(dev.get());
            std::print("[kstage1]: Searching AHCI port {} for a GPT\n", port->Driver->port_number());
            if (GPT::is_gpt_present(port->Driver.get())) {
                std::print("  GPT is present!\n");
                GPT::Header gptHeader;
                u8 sector[512];
                port->Driver->read_raw(512, sizeof gptHeader, &gptHeader);
                for (u32 i = 0; i < gptHeader.NumberOfPartitionsTableEntries; ++i) {
                    u64 byteOffset = gptHeader.PartitionsTableEntrySize * i;
                    u32 partSector = gptHeader.PartitionsTableLBA + (byteOffset / 512);
                    byteOffset %= 512;
                    port->Driver->read_raw(partSector * 512, 512, sector);
                    auto* part = reinterpret_cast<GPT::PartitionEntry*>(sector + byteOffset);
                    if (part->should_ignore())
                        continue;

                    if (part->TypeGUID == GPT::NullGUID)
                        continue;

                    if (part->EndLBA < part->StartLBA)
                        continue;

                    std::print("      Partition {}: {}:\n"
                               "        Type GUID: {}\n"
                               "        Unique GUID: {}\n"
                               "        Sector Offset: {}\n"
                               "        Sector Count: {}\n"
                               "        Attributes: {}\n",
                               i, std::string_view((const char *)part->Name, sizeof(GPT::PartitionEntry) - 0x38),
                               GUID(part->TypeGUID),
                               GUID(part->UniqueGUID),
                               u64(part->StartLBA),
                               part->size_in_sectors(),
                               u64(part->Attributes));


                    // Don't touch partitions with known GUIDs, except for a select few.
                    bool known = false;
                    GUID known_guid;
                    for (auto* reserved_guid = &GPT::ReservedPartitionGUIDs[0]; *reserved_guid != GPT::NullGUID; reserved_guid++) {
                        if (part->TypeGUID == *reserved_guid) {
                            known_guid = *reserved_guid;
                            known = true;
                            break;
                        }
                    }
                    if (!known) SYSTEM->create_device<Devices::GPTPartition>(std::static_pointer_cast<Devices::AHCIPort>(dev), *part);
                }

                /* Don't search port any further, we figured
                 * out it's storage media that is GPT partitioned
                 * and devices have been created for those
                 * (that will themselves be searched for filesystems).
                 */
                dev->set_flag(SYSDEV_MAJOR_STORAGE_SEARCH, false);
            }
        }
    }

    /* Detect filesystems
     * For every storage device we know how to read/write from,
     * check if a recognized filesystem resides on it.
     */
    VFS& vfs = SYSTEM->virtual_filesystem();
    for (usz i = 0; i < SYSTEM->Devices.size(); ++i) {
        auto dev = SYSTEM->Devices[i];
        if (dev->major() == SYSDEV_MAJOR_STORAGE
            && dev->flag(SYSDEV_MAJOR_STORAGE_SEARCH) != 0)
        {
            if (dev->minor() == SYSDEV_MINOR_GPT_PARTITION) {
                auto* partition = static_cast<Devices::GPTPartition*>(dev.get());
                if (partition) {
                    std::print("[kstage1]: GPT Partition:\n"
                               "  Type GUID: {}\n"
                               "  Unique GUID: {}\n",
                               partition->Driver->type_guid(),
                               partition->Driver->unique_guid());
                    if (auto FAT = FileAllocationTableDriver::try_create(sdd(partition->Driver))) {
                        std::print("  Found valid File Allocation Table filesystem\n");
                        static bool foundEFI = false;
                        std::string mountPath;
                        if (!foundEFI && partition->Partition.TypeGUID == GPT::PartitionType$EFISystem) {
                            mountPath = "/efi";
                            foundEFI = true;
                        } else mountPath = std::format("/fs{}", vfs.mounts().size());

                        vfs.mount(mountPath, std::move(FAT));

                        // Done searching GPT partition, found valid filesystem.
                        dev->set_flag(SYSDEV_MAJOR_STORAGE_SEARCH, false);
                    }
                }
            } else if (dev->minor() == SYSDEV_MINOR_AHCI_PORT) {
                auto* controller = static_cast<Devices::AHCIPort*>(dev.get());
                if (controller->Driver) {
                    std::print("[kstage1]: AHCI port {}:\n", controller->Driver->port_number());
                    std::print("  Checking for valid File Allocation Table filesystem\n");
                    if (auto FAT = FileAllocationTableDriver::try_create(sdd(controller->Driver))) {
                        std::print("  Found valid File Allocation Table filesystem\n");
                        // TODO: Name EFI SYSTEM partition something else to make it separate from
                        // regular partitions. Eventually should probably also disallow writing to
                        // EFI SYSTEM mount path unless in very specific circumstances controlled
                        // by the kernel.
                        vfs.mount(std::format("/fs{}", vfs.mounts().size()), std::move(FAT));

                        // Done searching AHCI port, found valid filesystem.
                        dev->set_flag(SYSDEV_MAJOR_STORAGE_SEARCH, false);
                    }
                }
            }
        }
    }

    vfs.print_debug();

    // Initialize the Programmable Interval Timer.
    gPIT = PIT();
    std::print("[kstage1]: \033[32mProgrammable Interval Timer Initialized\033[0m\n"
           "  Channel 0, H/L Bit Access\n"
           "  Rate Generator, BCD Disabled\n"
           "  Periodic interrupts at \033[33m{}hz\033[0m.\n"
           "\n", static_cast<double>(PIT_FREQUENCY));


    for (auto& dev : SYSTEM->Devices) {
        if (dev->major() == SYSDEV_MAJOR_NETWORK
            && dev->minor() == SYSDEV_MINOR_E1000) {
                Devices::E1000Device* e1000Device = static_cast<Devices::E1000Device*>(dev.get());
                gE1000 = {e1000Device->Header};
            }
    }


    // The Task State Segment in x86_64 is used
    // for switches between privilege levels.
    TSS::initialize();
    Scheduler::initialize();

    if (!vfs.mounts().empty()) {
        constexpr const char* filePath = "/fs0/bin/blazeit";
        std::print("Opening {} with VFS\n", filePath);
        auto fds = vfs.open(filePath);

        std::print("  Got FileDescriptors. {}, {}\n", fds.Process, fds.Global);
        vfs.print_debug();

        std::print("  Reading first few bytes: ");
        char tmpBuffer[11]{};
        vfs.read(fds.Process, reinterpret_cast<u8*>(tmpBuffer), 11);
        std::print("{}\n", std::string_view{tmpBuffer, 11});
        if (fds.valid()) {
            std::vector<std::string_view> argv;
            argv.push_back(filePath);
            if (ELF::CreateUserspaceElf64Process(fds.Process, argv))
                std::print("Successfully created new process from `{}`\n", filePath);

            std::print("Closing FileDescriptor {}\n", fds.Process);
            vfs.close(fds.Process);
            std::print("FileDescriptor {} closed\n", fds.Process);
            vfs.print_debug();
        }

        // Another userspace program
        constexpr const char *const programTwoFilePath = "/fs0/bin/stdout";

        // Userspace Framebuffer
        usz fb_phys_addr = (usz)bInfo->framebuffer->BaseAddress;
        usz fb_virt_addr = 0x7f000000;

        std::vector<std::string_view> argv;
        argv.push_back(std::format("{}", programTwoFilePath));
        argv.push_back(std::format("{:x}", fb_virt_addr));
        argv.push_back(std::format("{:x}", (usz)bInfo->framebuffer->BufferSize));
        argv.push_back(std::format("{:x}", (usz)bInfo->framebuffer->PixelWidth));
        argv.push_back(std::format("{:x}", (usz)bInfo->framebuffer->PixelHeight));
        argv.push_back(std::format("{:x}", (usz)bInfo->framebuffer->PixelsPerScanLine));

        std::print("Opening {} with VFS\n", programTwoFilePath);
        fds = vfs.open(programTwoFilePath);
        std::print("  Got FileDescriptors. {}, {}\n", fds.Process, fds.Global);
        if (fds.valid()) {
            if (ELF::CreateUserspaceElf64Process(fds.Process, argv))
                std::print("Sucessfully created new process from `{}`\n", programTwoFilePath);
            vfs.close(fds.Process);
        }
        // Get last process in queue from scheduler.
        Process *process = Scheduler::last_process();

        usz flags = 0;
        flags |= (usz)Memory::PageTableFlag::Present;
        flags |= (usz)Memory::PageTableFlag::UserSuper;
        flags |= (usz)Memory::PageTableFlag::ReadWrite;

        // TODO: We should probably pick this more betterer :Þ
        for (usz t = 0; t < bInfo->framebuffer->BufferSize; t += PAGE_SIZE) {
            Memory::map(process->CR3
                        , (void*)(fb_virt_addr + t)
                        , (void*)(fb_phys_addr + t)
                        , flags
                        , Memory::ShowDebug::No
                        );
        }
        process->add_memory_region((void*)fb_virt_addr
                                   , (void*)fb_phys_addr
                                   , bInfo->framebuffer->BufferSize
                                   , flags
                                   );


        SYSTEM->set_init(process);

        constexpr const char* programTestFilePath = "/fs0/notexist.ing";
        std::print("Opening {} just for fun\n", programTestFilePath);
        fds = vfs.open(programTestFilePath);
        if (fds.valid()) vfs.close(fds.Process);
    }

    // Initialize High Precision Event Timer.
    (void)gHPET.initialize();
    // Prepare PS2 mouse.
    init_ps2_mouse();

    // Enable IRQ interrupts that will be used.
    disable_all_interrupts();
    enable_interrupt(IRQ_SYSTEM_TIMER);
    enable_interrupt(IRQ_PS2_KEYBOARD);
    enable_interrupt(IRQ_CASCADED_PIC);
    enable_interrupt(IRQ_UART_COM1);
    enable_interrupt(IRQ_REAL_TIMER);
    enable_interrupt(IRQ_PS2_MOUSE);

    //Memory::print_efi_memory_map(bInfo->map, bInfo->mapSize, bInfo->mapDescSize);
    Memory::print_efi_memory_map_summed(bInfo->map, bInfo->mapSize, bInfo->mapDescSize);
    //heap_print_debug();
    heap_print_debug_summed();
    Memory::print_debug();

    SYSTEM->print();

    // Allow interrupts to trigger.
    std::print("[kstage1]: Enabling interrupts\n");
    asm ("sti");
    //std::print("[kstage1]: \033[32mInterrupts enabled\033[0m\n");
}
