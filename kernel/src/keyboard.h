#ifndef LENSOR_OS_KEYBOARD_H
#define LENSOR_OS_KEYBOARD_H

#include "keyboard_scancode_translation.h"
#include "uart.h"
#include "cstr.h"

#include "integers.h"
#include "math.h"
#include "basic_renderer.h"

namespace Keyboard {
    const u8 KBCursorSizeX = 8;
    const u8 KBCursorSizeY = 2;

    extern u8 KeyboardCursor[KBCursorSizeX * KBCursorSizeY];
    extern u32 PixelsUnderKBCursor[KBCursorSizeX * KBCursorSizeY + 1];

    struct KeyboardState {
        bool LeftShift  { false };
        bool RightShift { false };
        bool CapsLock   { false };
    };

    class BasicTextRenderer {
    public:
        uVector2 SizeInCharacters;
        uVector2 DrawPosition;
        uVector2 CachedDrawPosition;
        uVector2 CursorPosition;
        uVector2 LastCursorPosition;
        KeyboardState State;
        bool GotE0 { false };

        BasicTextRenderer() {
            SizeInCharacters.x = gRend.Target->PixelWidth / 8;
            SizeInCharacters.y = gRend.Target->PixelHeight / gRend.Font->PSF1_Header->CharacterSize;
        }

        void newline() {
            CursorPosition.x = 0;
            cursor_down();
        }

        void cursor_up(u64 amt = 1) {
            if (amt > CursorPosition.y)
                CursorPosition.y = 0;
            else CursorPosition.y -= amt;
        }

        void cursor_down(u64 amt = 1) {
            if (SizeInCharacters.y - amt < CursorPosition.y)
                CursorPosition.y = SizeInCharacters.y - 1;
            else CursorPosition.y += amt;
        }

        void cursor_left(u64 amt = 1) {
            if (amt > CursorPosition.x)
                CursorPosition.x = 0;
            else CursorPosition.x -= amt;
        }

        void cursor_right(u64 amt = 1) {
            if (SizeInCharacters.x - amt <= CursorPosition.x)
                newline();
            else CursorPosition.x += amt;
        }

        void putc(char character) {
            CachedDrawPosition = gRend.DrawPos;
            update_draw_position();
            gRend.DrawPos = DrawPosition;
            gRend.putchar(character);
            gRend.swap(DrawPosition, {8, gRend.Font->PSF1_Header->CharacterSize});
            cursor_right();
            gRend.DrawPos = CachedDrawPosition;
        }

        void backspace() {
            CachedDrawPosition = gRend.DrawPos;
            update_draw_position();
            gRend.DrawPos = DrawPosition;
            gRend.clearchar();
            gRend.swap(gRend.DrawPos, {8, gRend.Font->PSF1_Header->CharacterSize});
            cursor_left();
            gRend.DrawPos = CachedDrawPosition;
        }

        void update_draw_position() {
            DrawPosition = uVector2(CursorPosition.x * 8,
                                    CursorPosition.y * gRend.Font->PSF1_Header->CharacterSize);
        }

        void draw_cursor() {
            CachedDrawPosition = gRend.DrawPos;

            // Calculate rectangle that needs to be updated (in characters).
            uVector2 RefreshPosition = LastCursorPosition + uVector2(0, 1);
            uVector2 RefreshSize { 1, 1 };
            if (CursorPosition.x > RefreshPosition.x)
                RefreshSize.x += CursorPosition.x - RefreshPosition.x;
            else if (CursorPosition.x < RefreshPosition.x) {
                RefreshSize.x += RefreshPosition.x - CursorPosition.x;
                RefreshPosition.x = CursorPosition.x;
            }
            if (CursorPosition.y > RefreshPosition.y)
                RefreshSize.y += CursorPosition.y - RefreshPosition.y;
            else if (CursorPosition.y < RefreshPosition.y) {
                RefreshSize.y += RefreshPosition.y - CursorPosition.y;
                RefreshPosition.y = CursorPosition.y;
            }
            // Convert characters to pixels.
            RefreshPosition = RefreshPosition * uVector2(8, gRend.Font->PSF1_Header->CharacterSize);
            RefreshSize = RefreshSize * uVector2(8, gRend.Font->PSF1_Header->CharacterSize);

            // Skip first iteration in order to accurately read what is under the cursor before it is drawn.
            static bool skip = true;
            if (skip == false) {
                gRend.DrawPos = uVector2(LastCursorPosition.x * 8, LastCursorPosition.y * gRend.Font->PSF1_Header->CharacterSize);
                gRend.DrawPos.y = gRend.DrawPos.y + gRend.Font->PSF1_Header->CharacterSize;
                gRend.drawpix({KBCursorSizeX, KBCursorSizeY}, &PixelsUnderKBCursor[0]);
            }
            else skip = false;

            update_draw_position();
            DrawPosition.y = DrawPosition.y + gRend.Font->PSF1_Header->CharacterSize;
            gRend.DrawPos = DrawPosition;
            // READ PIXELS UNDER NEW POSITION INTO BUFFER.
            gRend.readpix({KBCursorSizeX, KBCursorSizeY}, &PixelsUnderKBCursor[0]);
            // DRAW CURSOR AT NEW POSITION.
            gRend.drawbmpover({KBCursorSizeX, KBCursorSizeY}, &KeyboardCursor[0], 0xffffffff);
            gRend.swap(RefreshPosition, RefreshSize);
            // RETURN GLOBAL DRAW POSITION.
            gRend.DrawPos = CachedDrawPosition;

            LastCursorPosition = CursorPosition;
        }

        void parse_scancode(u8 code) {
            if (GotE0) {
                switch (code) {
                case ARROW_UP:
                    cursor_up();
                    break;
                case ARROW_DOWN:
                    cursor_down();
                    break;
                case ARROW_LEFT:
                    cursor_left();
                    break;
                case ARROW_RIGHT:
                    cursor_right();
                    break;
                default:
                    break;
                }
            
                GotE0 = false;
            }
            else {
                switch (code) {
                case 0xe0:
                    GotE0 = true;
                    return;
                case LSHIFT:
                    State.LeftShift = true;
                    return;
                case LSHIFT + 0x80:
                    State.LeftShift = false;
                    return;
                case RSHIFT:
                    State.RightShift = true;
                    return;
                case RSHIFT + 0x80:
                    State.RightShift = false;
                    return;
                case CAPSLOCK:
                    State.CapsLock = !State.CapsLock;
                    return;
                case ENTER:
                    newline();
                    return;
                case BACKSPACE:
                    backspace();
                    return;
                default:
                    break;
                }
                handle_character(QWERTY::Translate(code, (State.LeftShift || State.RightShift || State.CapsLock)));
            }
        }

        void parse_character(char c) {
            switch (c) {
            case 0x8:
                // BS
                backspace();
                return;
            case 0xd:
                // CR
                newline();
                return;
            default:
                break;
            }

            // Non-printable characters.
            if (c < 32)
                return;

            putc(c);
        }

        void set_cursor_from_pixel_position(uVector2 pos) {
            CursorPosition.x = pos.x / 8;
            CursorPosition.y = pos.y / gRend.Font->PSF1_Header->CharacterSize;
        }

        void handle_scancode(u8 code) {
            //srl->writestr("[Text]: Handling scancode: 0x");
            //srl->writestr(to_hexstring(code));
            //srl->writestr("\r\n");
        
            parse_scancode(code);
            draw_cursor();
        }

        void handle_character(char c) {
            //srl->writestr("[Text]: Handling character: ");
            //srl->writestr(to_string(code));
            //srl->writestr("\r\n");

            parse_character(c);
            draw_cursor();
        }
    };

    extern BasicTextRenderer gText;
}

#endif
