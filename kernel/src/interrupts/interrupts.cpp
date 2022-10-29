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

#include <interrupts/interrupts.h>

#include <basic_renderer.h>
#include <cstr.h>
#include <io.h>
#include <keyboard.h>
#include <mouse.h>
#include <panic.h>
#include <pit.h>
#include <rtc.h>
#include <uart.h>

void enable_interrupt(u8 irq) {
    if (irq > 15)
        return;

    u16 port = PIC2_DATA;
    if (irq < 8)
        port = PIC1_DATA;
    else
        irq -= 8;
    u8 value = in8(port) & ~IRQ_BIT(irq);
    out8(port, value);
}

void disable_interrupt(u8 irq) {
    if (irq > 15)
        return;

    u16 port = PIC2_DATA;
    if (irq < 8)
        port = PIC1_DATA;
    else irq -= 8;
    u8 value = in8(port) | IRQ_BIT(irq);
    out8(port, value);
}

void disable_all_interrupts() {
    out8(PIC1_DATA, 0);
    out8(PIC2_DATA, 0);
}

__attribute__((no_caller_saved_registers))
inline void end_of_interrupt(u8 IRQx) {
    if (IRQx >= 8)
        out8(PIC2_COMMAND, PIC_EOI);
    out8(PIC1_COMMAND, PIC_EOI);
}

void cause_div_by_zero(volatile u8 one) {
    one /= one - 1;
}

void cause_page_not_present() {
    u8* badAddr = (u8*)0xdeadc0de;
    volatile u8 faultHere = *badAddr;
    (void)faultHere;
}

void cause_nullptr_dereference() {
    u8* badAddr = (u8*)nullptr;
    volatile u8 faultHere = *badAddr;
    (void)faultHere;
}

void cause_general_protection() {
    u8* badAddr = (u8*)0xdeadbeefb00bface;
    volatile u8 faultHere = *badAddr;
    (void)faultHere;
}

// HARDWARE INTERRUPT HANDLERS (IRQs)
/// IRQ0: SYSTEM TIMER
__attribute__((interrupt))
void system_timer_handler(InterruptFrame* frame) {
    gPIT.tick();
    end_of_interrupt(0);
}

/// IRQ1: PS/2 KEYBOARD
__attribute__((interrupt))
void keyboard_handler(InterruptFrame* frame) {
    // TODO: Write to stdin of focused process. Well, not necessarily.
    // Register a key input in the event system...
    Keyboard::gText.handle_scancode(in8(0x60));
    end_of_interrupt(1);
}

/// IRQ4: COM1/COM3 Serial Communications Recieved
__attribute__((interrupt))
void uart_com1_handler(InterruptFrame* frame) {
    u8 data = UART::read();
    end_of_interrupt(4);
    Keyboard::gText.handle_character(data);
}

/// IRQ8: Real Time Clock
/// NOTE: If register 'C' is not read from inside this handler,
///         no further interrupts of this type will be sent.
/// Status Register `C`:
///   Bits 0-3: Reserved (do not touch)
///          4: Update-ended interrupt
///          5: Alarm interrupt
///          6: Periodic Interrupt
///          7: Interrupt Request (IRQ)
__attribute__((interrupt))
void rtc_handler(InterruptFrame* frame) {
    u8 statusC = gRTC.read_register(0x0C);
    if (statusC & 0b01000000)
        gRTC.Ticks += 1;
    end_of_interrupt(8);
}

/// IRQ12: PS/2 MOUSE
__attribute__((interrupt))
void mouse_handler(InterruptFrame* frame) {
    u8 data = in8(0x60);
    handle_ps2_mouse_interrupt(data);
    // End interrupt
    end_of_interrupt(12);
}

/// FAULT INTERRUPT HANDLERS

__attribute__((interrupt))
void divide_by_zero_handler(InterruptFrame* frame) {
    panic(frame, "Divide by zero detected!");
    while (true)
        asm ("hlt");
}

enum class PageFaultErrorCode {
    Present                                   = 1 << 0,
    ReadWrite                                 = 1 << 1,
    UserSuper                                 = 1 << 2,
    Reserved                                  = 1 << 3,
    InstructionFetch                          = 1 << 4,
    ProtectionKeyViolation                    = 1 << 5,
    ShadowStackAccess                         = 1 << 6,
    HypervisorManagedLinearAddressTranslation = 1 << 7,
    SoftwareGaurdExtensions                   = 1 << 15,
};

__attribute__((interrupt))
void page_fault_handler(InterruptFrameError* frame) {
    // Collect faulty address as soon as possible (it may be lost quickly).
    u64 address;
    asm volatile ("mov %%cr2, %0" : "=r" (address));
    /* US RW P - Description
     * 0  0  0 - Supervisory process tried to read a non-present page entry
     * 0  0  1 - Supervisory process tried to read a page and caused a protection fault
     * 0  1  0 - Supervisory process tried to write to a non-present page entry
     * 0  1  1 - Supervisory process tried to write a page and caused a protection fault
     * 1  0  0 - User process tried to read a non-present page entry
     * 1  0  1 - User process tried to read a page and caused a protection fault
     * 1  1  0 - User process tried to write to a non-present page entry
     * 1  1  1 - User process tried to write a page and caused a protection fault
     */
    bool notPresent = (frame->error & (u64)PageFaultErrorCode::Present) == 0;
    if ((frame->error & (u64)PageFaultErrorCode::UserSuper) > 0) {
        if ((frame->error & (u64)PageFaultErrorCode::ReadWrite) > 0) {
            if (notPresent)
                panic(frame, "#PF: User process attempted to write to a page that is not present");
            else panic(frame, "#PF: User process attempted to write to a page and caused a protection fault");
        }
        else {
            if (notPresent)
                panic(frame, "#PF: User process attempted to read from a page that is not present");
            else panic(frame, "#PF: User process attempted to read from a page and caused a protection fault");
        }
    }
    else {
        if ((frame->error & (u64)PageFaultErrorCode::ReadWrite) > 0) {
            if (notPresent)
                panic(frame, "#PF: Supervisor process attempted to write to a page that is not present");
            else panic(frame, "#PF: Supervisor process attempted to write to a page and caused a protection fault");
        }
        else {
            if (notPresent)
                panic(frame, "#PF: Supervisor process attempted to read from a page that is not present");
            else panic(frame, "#PF: Supervisor process attempted to read from a page and caused a protection fault");
        }
    }
    if ((frame->error & (u64)PageFaultErrorCode::InstructionFetch) > 0)
        UART::out("  Instruction fetch\r\n");
    if ((frame->error & (u64)PageFaultErrorCode::ShadowStackAccess) > 0)
        UART::out("  Shadow stack access\r\n");
    if ((frame->error & (u64)PageFaultErrorCode::HypervisorManagedLinearAddressTranslation) > 0)
        UART::out("  Hypvervisor-managed linear address translation\r\n");
    if ((frame->error & (u64)PageFaultErrorCode::SoftwareGaurdExtensions) > 0)
        UART::out("  Software gaurd extensions\r\n");

    UART::out("  Faulty Address: 0x");
    UART::out(to_hexstring(address));
    UART::out("\r\n");
    Vector2<u64> drawPosition = { PanicStartX, PanicStartY };
    gRend.puts(drawPosition, "Faulty Address: 0x", 0x00000000);
    gRend.puts(drawPosition, to_hexstring(address), 0x00000000);
    gRend.swap({ PanicStartX, PanicStartY }, { 1024, 128 } );
    while (true)
        asm ("hlt");
}

__attribute__((interrupt))
void double_fault_handler(InterruptFrameError* frame) {
    panic(frame, "Double fault detected!");
    while (true) {
        asm ("hlt");
    }
}

__attribute__((interrupt))
void stack_segment_fault_handler(InterruptFrameError* frame) {
    if (frame->error == 0)
        panic(frame, "Stack segment fault detected (0)");
    else panic(frame, "Stack segment fault detected (selector)!");

    if (frame->error & 0b1)
        UART::out("  External\r\n");

    u8 table = (frame->error & 0b110) >> 1;
    if (table == 0b00)
        UART::out("  GDT");
    else if (table == 0b01 || table == 0b11)
        UART::out("  IDT");
    else if (table == 0b10)
        UART::out("  LDT");
    
    UART::out(" Selector Index: ");
    UART::out(to_hexstring(((frame->error & 0b1111111111111000) >> 3)));
    UART::out("\r\n");
}

__attribute__((interrupt))
void general_protection_fault_handler(InterruptFrameError* frame) {
    if (frame->error == 0)
        panic(frame, "General protection fault detected (0)!");
    else panic(frame, "General protection fault detected (selector)!");

    if (frame->error & 0b1)
        UART::out("  External\r\n");
    
    u8 table = (frame->error & 0b110) >> 1;
    if (table == 0b00)
        UART::out("  GDT");
    else if (table == 0b01 || table == 0b11)
        UART::out("  IDT");
    else if (table == 0b10)
        UART::out("  LDT");
    
    UART::out(" Selector Index: ");
    UART::out(to_hexstring(((frame->error & 0b1111111111111000) >> 3)));
    UART::out("\r\n");
    while (true)
        asm ("hlt");
}

__attribute__((interrupt))
void simd_exception_handler(InterruptFrame* frame) {
    /* NOTE: Data about why exception occurred can be found in MXCSR register.
     * MXCSR Register breakdown:
     * 0b00000000
     *          =   -- invalid operation flag
     *         =    -- denormal flag
     *        =     -- divide-by-zero flag
     *       =      -- overflow flag
     *      =       -- underflow flag
     *     =        -- precision flag
     *    =         -- denormals are zeros flag
     */
    u32 mxcsr { 0 };
    asm volatile ("ldmxcsr %0"::"m"(mxcsr));
    if (mxcsr & 0b00000001)
        panic(frame, "SIMD fault detected (Invalid Operation)!");
    else if (mxcsr & 0b00000010)
        panic(frame, "SIMD fault detected (Denormal)!");
    else if (mxcsr & 0b00000100)
        panic(frame, "SIMD fault detected (Divide by Zero)!");
    else if (mxcsr & 0b00001000)
        panic(frame, "SIMD fault detected (Overflow)!");
    else if (mxcsr & 0b00010000)
        panic(frame, "SIMD fault detected (Underflow)!");
    else if (mxcsr & 0b00100000)
        panic(frame, "SIMD fault detected (Precision)!");
    else if (mxcsr & 0b01000000)
        panic(frame, "SIMD fault detected (Denormals are Zero)!");
    else panic(frame, "Unknown SIMD fault");
    while (true)
        asm ("hlt");
}

void remap_pic() {
    // SAVE INTERRUPT MASKS.
    u8 parentMasks;
    u8 childMasks;
    parentMasks = in8(PIC1_DATA);
    io_wait();
    childMasks = in8(PIC2_DATA);
    io_wait();
    // INITIALIZE BOTH CHIPS IN CASCADE MODE.
    out8(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out8(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    // SET VECTOR OFFSET OF MASTER PIC.
    //   This allows software to throw low interrupts as normal (0-32)
    //     without triggering an IRQ that would normally be attributed to hardware.
    out8(PIC1_DATA, PIC_IRQ_VECTOR_OFFSET);
    io_wait();
    // SET VECTOR OFFSET OF SLAVE PIC.
    out8(PIC2_DATA, PIC_IRQ_VECTOR_OFFSET + 8);
    io_wait();
    // TELL MASTER THERE IS A SLAVE ON IRQ2.
    out8(PIC1_DATA, 4);
    io_wait();
    // TELL SLAVE IT'S CASCADE IDENTITY.
    out8(PIC2_DATA, 2);
    io_wait();
    // NOT QUITE SURE WHAT THIS DOES YET.
    out8(PIC1_DATA, ICW4_8086);
    io_wait();
    out8(PIC2_DATA, ICW4_8086);
    io_wait();
    // LOAD INTERRUPT MASKS.
    out8(PIC1_DATA, parentMasks);
    io_wait();
    out8(PIC2_DATA, childMasks);
    io_wait();
}

#include <scheduler.h>
__attribute__((no_caller_saved_registers))
void scheduler_switch(CPUState* cpu) {
    Scheduler::switch_process(cpu);
};
