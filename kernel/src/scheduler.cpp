#include "scheduler.h"

#include "cstr.h"
#include "interrupts/idt.h"
#include "memory.h"
#include "memory/paging.h"
#include "memory/virtual_memory_manager.h"
#include "pit.h"
#include "uart.h"

// External symbols for `scheduler.asm`
void* scheduler_switch_func;
void(*timer_tick)();

KernelProcess StartupProcess;

KernelProcess* Scheduler::ProcessQueue   { nullptr };
KernelProcess* Scheduler::CurrentProcess { nullptr };

void Scheduler::add_kernel_process(KernelProcess* process) {
    KernelProcess* last = ProcessQueue;
    while (CurrentProcess->Next != nullptr)
        last = CurrentProcess->Next;

    last->Next = process;
}

// Called from `irq0_handler` in `scheduler.asm`
void Scheduler::switch_process(CPUState* cpu) {
    memcpy(&cpu, &CurrentProcess->CPU, sizeof(CPUState));
    if (CurrentProcess->Next == nullptr)
        CurrentProcess = ProcessQueue;
    else CurrentProcess = CurrentProcess->Next;
    memcpy(&CurrentProcess->CPU, &cpu, sizeof(CPUState));
    Memory::flush_page_map(CurrentProcess->CR3);
}

void Scheduler::initialize() {
    ProcessQueue = &StartupProcess;

    CurrentProcess = &StartupProcess;
    // IRQ handler in assembly needs to increment PIT ticks counter.
    timer_tick = &pit_tick;
    // IRQ handler in assembly needs to call a function to switch process.
    scheduler_switch_func = (void*)switch_process;
    // Setup start process as current executing code.
    StartupProcess.CR3 = Memory::get_active_page_map();
    StartupProcess.Next = nullptr;
    // Install IRQ0 handler found in `scheduler.asm` (over-write default system timer handler).

    // FIXME: SOMETHING IN THIS HANDLER DOESN'T WORK ON VIRTUAL BOX :^<
    gIDT.install_handler((u64)&irq0_handler, PIC_IRQ0);
}
