#include "panic.h"

#include "cstr.h"
#include "basic_renderer.h"
#include "uart.h"
#include "interrupts/interrupts.h"

void panic(const char* panicMessage) {
    srl->writestr("\r\n\033[1;37;41mLensorOS PANIC\033[0m\r\n");
    srl->writestr("  ");
    srl->writestr(panicMessage);
    srl->writestr("\r\n");
    gRend.BackgroundColor = 0xffff0000;
    gRend.DrawPos = {PanicStartX, PanicStartY};
    gRend.puts("LensorOS PANIC MODE");
    gRend.crlf(PanicStartX);
    gRend.crlf(PanicStartX);
    gRend.puts(panicMessage, 0x00000000);
    gRend.crlf(PanicStartX);
    // Update entire bottom-right of screen starting at (PanicStartX, PanicStartY).
    gRend.swap({PanicStartX, PanicStartY}, {80000, 80000});
}

void panic(InterruptFrame* frame, const char* panicMessage) {
    panic(panicMessage);
    srl->writestr("  Instruction Address: 0x");
    srl->writestr(to_hexstring(frame->ip));
    srl->writestr("\r\n");
    srl->writestr("  Stack Pointer: 0x");
    srl->writestr(to_hexstring(frame->sp));
    srl->writestr("\r\n");
    gRend.puts("Instruction Address: 0x", 0x00000000);
    gRend.puts(to_hexstring(frame->ip), 0x00000000);
    gRend.crlf(PanicStartX);
    gRend.puts("Stack Pointer: 0x", 0x00000000);
    gRend.puts(to_hexstring(frame->sp), 0x00000000);
    gRend.crlf(PanicStartX);
    // Update entire bottom-right of screen starting at (PanicStartX, PanicStartY).
    gRend.swap({PanicStartX, PanicStartY}, {80000, 80000});
}
