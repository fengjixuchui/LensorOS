#include <basic_renderer.h>

#include <cstr.h>
#include <debug.h>
#include <integers.h>
#include <math.h>
#include <memory/physical_memory_manager.h>
#include <memory/virtual_memory_manager.h>

// Define global renderer for use anywhere within the kernel.
BasicRenderer gRend;

Framebuffer target;
BasicRenderer::BasicRenderer(Framebuffer* render, PSF1_FONT* f)
    : Render(render), Font(f) {
    // Framebuffer supplied by GOP is in physical memory; map the
    // physical memory dedicated to the framebuffer into virtual memory.
    // Calculate size of framebuffer in pages.
    u64 fbBase = (u64)render->BaseAddress;
    u64 fbSize = render->BufferSize + 0x1000;
    u64 fbPages = fbSize / 0x1000 + 1;
    // Allocate physical pages for Render framebuffer.
    Memory::lock_pages(render->BaseAddress, fbPages);
    // Map active framebuffer physical address to virtual addresses 1:1.
    for (u64 t = fbBase; t < fbBase + fbSize; t += 0x1000) {
        Memory::map((void*)t, (void*)t
                    , (u64)Memory::PageTableFlag::Present
                    | (u64)Memory::PageTableFlag::ReadWrite
                    );
    }
    dbgmsg("  Active GOP framebuffer mapped to %x thru %x\r\n"
           , fbBase
           , fbBase + fbSize
           );
    // Create a new framebuffer. This memory is what will be drawn to.
    // When the screen should be updated, this new framebuffer is copied
    // into the active one. This helps performance as the active framebuffer
    // may be very slow to read/write from.
    // Copy render framebuffer data to target.
    target = *render;
    // Find physical pages for target framebuffer and allocate them.
    target.BaseAddress = Memory::request_pages(fbPages);
    if (target.BaseAddress == nullptr) {
        // If memory allocation fails, pretend there is
        // two buffers but they both point to the same spot.
        // This isn't the most performant, but it does work.
        Target = Render;
    }
    else {
        dbgmsg("  Deferred GOP framebuffer allocated at %x thru %x\r\n"
               , target.BaseAddress
               , (u64)target.BaseAddress + fbSize
               );
        // If memory allocation succeeds, map memory somewhere
        // out of the way in the virtual address range.
        // FIXME: Don't hard code this address.
        constexpr u64 virtualTargetBaseAddress = 0xffffff8000000000;
        u64 physicalTargetBaseAddress = (u64)target.BaseAddress;
        for (u64 t = 0; t < fbSize; t += 0x1000) {
            Memory::map((void*)(virtualTargetBaseAddress + t)
                        , (void*)(physicalTargetBaseAddress + t)
                        , (u64)Memory::PageTableFlag::Present
                        | (u64)Memory::PageTableFlag::ReadWrite
                        );
        }
        target.BaseAddress = (void*)virtualTargetBaseAddress;
        Target = &target;
        dbgmsg("  Deferred GOP framebuffer mapped to %x thru %x\r\n"
               , virtualTargetBaseAddress
               , virtualTargetBaseAddress + fbSize
               );
    }
    clear();
    swap();
}

inline void BasicRenderer::clamp_draw_position(Vector2<u64>& position) {
    if (position.x > Target->PixelWidth)
        position.x = Target->PixelWidth;
    if (position.y > Target->PixelHeight)
        position.y = Target->PixelHeight;
}

void BasicRenderer::swap(Vector2<u64> position, Vector2<u64> size) {
    if (Render->BaseAddress == Target->BaseAddress)
        return;
    // Only swap what is within the bounds of the framebuffer.
    if (position.x > Target->PixelWidth
        || position.y > Target->PixelHeight)
        return;
    // Ensure size doesn't over-run edge of framebuffer.
    u64 diffX = Target->PixelWidth - position.x;
    u64 diffY = Target->PixelHeight - position.y;
    if (diffX < size.x)
        size.x = diffX;
    if (diffY < size.y)
        size.y = diffY;
    // Calculate addresses.
    u64 offset = ((BytesPerPixel * position.x)
                  + (BytesPerPixel * position.y * Target->PixelsPerScanLine));
    u32* targetBaseAddress = (u32*)((u64)Target->BaseAddress + offset);
    u32* renderBaseAddress = (u32*)((u64)Render->BaseAddress + offset);
    // Copy rectangle line-by-line.
    u64 bytesPerLine = BytesPerPixel * size.x;
    for (u64 y = 0; y < size.y; ++y) {
        memcpy(targetBaseAddress, renderBaseAddress, bytesPerLine);
        targetBaseAddress += Target->PixelsPerScanLine;
        renderBaseAddress += Render->PixelsPerScanLine;
    }
}

// Carriage return ('\r')
void BasicRenderer::cret(Vector2<u64>& position) {
    position = {
        0,
        position.y
    };
}
/// Newline ('\n') or LineFeed (LF)
void BasicRenderer::newl(Vector2<u64>& position) {
    position = {
        position.x,
        position.y + Font->PSF1_Header->CharacterSize
    };
}
/// Carriage return line feed; CRLF ('\r' + '\n')
void BasicRenderer::crlf(Vector2<u64>& position) {
    position = {
        0,
        position.y + Font->PSF1_Header->CharacterSize
    };
}

/// Carriage return, offset by given argument value pixels, then newline.
void BasicRenderer::crlf(Vector2<u64>& position, u32 offset) {
    position = {
        offset,
        position.y + Font->PSF1_Header->CharacterSize
    };
}

void BasicRenderer::drawrect(Vector2<u64>& position, Vector2<u64> size, u32 color) {
    clamp_draw_position(position);
    u32 diffX = Target->PixelWidth - position.x;
    u32 diffY = Target->PixelHeight - position.y;
    if (diffX < size.x)
        size.x = diffX;
    if (diffY < size.y)
        size.y = diffY;
    u32* pixel_ptr = (u32*)Target->BaseAddress;
    for (u64 y = position.y; y < position.y + size.y; y++) {
        for (u64 x = position.x; x < position.x + size.x; x++) {
            *(u32*)(pixel_ptr + x + (y * Target->PixelsPerScanLine)) = color;
        }
    }
}

/// Read `size` of pixel framebuffer starting at `DrawPos` into `buffer`.
void BasicRenderer::readpix(Vector2<u64>& position, Vector2<u64> size, u32* buffer) {
    if (buffer == nullptr)
        return;
    clamp_draw_position(position);
    u32 initX = size.x;
    u32 diffX = Target->PixelWidth - position.x;
    u32 diffY = Target->PixelHeight - position.y;
    if (diffX < size.x)
        size.x = diffX;
    if (diffY < size.y)
        size.y = diffY;
    u32* pixel_ptr = (u32*)Target->BaseAddress;
    for (u64 y = position.y; y < position.y + size.y; y++) {
        for (u64 x = position.x; x < position.x + size.x; x++) {
            *(u32*)(buffer + (x - position.x) + ((y - position.y) * initX))
                = *(u32*)(pixel_ptr + x + (y * Target->PixelsPerScanLine));
        }
    }
}

/// Draw `size` of `pixels` linear buffer starting at `DrawPos`.
void BasicRenderer::drawpix(Vector2<u64>& position, Vector2<u64> size, u32* pixels) {
    if (pixels == nullptr)
        return;
    clamp_draw_position(position);
    u32 initX = size.x;
    u32 diffX = Target->PixelWidth - position.x;
    u32 diffY = Target->PixelHeight - position.y;
    if (diffX < size.x)
        size.x = diffX;
    if (diffY < size.y)
        size.y = diffY;
    u32* pixel_ptr = (u32*)Target->BaseAddress;
    for (u64 y = position.y; y < position.y + size.y; y++) {
        for (u64 x = position.x; x < position.x + size.x; x++) {
            *(u32*)(pixel_ptr + x + (y * Target->PixelsPerScanLine))
                = *(u32*)(pixels + (x - position.x) + ((y - position.y) * initX));
        }
    }
}

/// Draw `size` of a bitmap `bitmap`, using passed color `color`
///   where bitmap is `1` and `BackgroundColor` where it is `0`.
void BasicRenderer::drawbmp(Vector2<u64>& position, Vector2<u64> size, const u8* bitmap, u32 color) {
    if (bitmap == nullptr)
        return;
    clamp_draw_position(position);
    u32 initX = size.x;
    u32 diffX = Target->PixelWidth - position.x;
    u32 diffY = Target->PixelHeight - position.y;
    if (diffX < size.x)
        size.x = diffX;
    if (diffY < size.y)
        size.y = diffY;
    u32* pixel_ptr = (u32*)Target->BaseAddress;
    for (u64 y = position.y; y < position.y + size.y; y++) {
        for (u64 x = position.x; x < position.x + size.x; x++) {
            s32 byte = ((x - position.x) + ((y - position.y) * initX)) / 8;
            if ((bitmap[byte] & (0b10000000 >> ((x - position.x) % 8))) > 0)
                *(u32*)(pixel_ptr + x + (y * Target->PixelsPerScanLine)) = color;
            else *(u32*)(pixel_ptr + x + (y * Target->PixelsPerScanLine)) = BackgroundColor;
        }
    }
}

/// Draw `size` of a bitmap `bitmap`, using passed color `color` where bitmap is `1`.
void BasicRenderer::drawbmpover(Vector2<u64>& position, Vector2<u64> size, const u8* bitmap, u32 color) {
    if (bitmap == nullptr)
        return;
    clamp_draw_position(position);
    u32 initX = size.x;
    u32 diffX = Target->PixelWidth - position.x;
    u32 diffY = Target->PixelHeight - position.y;
    if (diffX < size.x) { size.x = diffX; }
    if (diffY < size.y) { size.y = diffY; }
    u32* pixel_ptr = (u32*)Target->BaseAddress;
    for (u64 y = position.y; y < position.y + size.y; y++) {
        for (u64 x = position.x; x < position.x + size.x; x++) {
            s32 byte = ((x - position.x) + ((y - position.y) * initX)) / 8;
            if ((bitmap[byte] & (0b10000000 >> ((x - position.x) % 8))) > 0)
                *(u32*)(pixel_ptr + x + (y * Target->PixelsPerScanLine)) = color;
        }
    }
}

/// Draw a character at `position` using the renderer's bitmap font.
void BasicRenderer::drawchar(Vector2<u64>& position, char c, u32 color) {
    // Draw character.
    drawbmp(position,
            {8, Font->PSF1_Header->CharacterSize},
            (u8*)Font->GlyphBuffer + (c * Font->PSF1_Header->CharacterSize),
            color);
}

/// Draw a character at `position` using the renderer's bitmap font,
/// without clearing what's behind the character.
void BasicRenderer::drawcharover(Vector2<u64>& position, char c, u32 color) {
    // Draw character.
    drawbmpover(position,
                {8, Font->PSF1_Header->CharacterSize},
                (u8*)Font->GlyphBuffer + (c * Font->PSF1_Header->CharacterSize),
                color);
}

/// Draw a character using the renderer's bitmap font, then increment `DrawPos`
///   as such that another character would not overlap with the previous (ie. typing).
void BasicRenderer::putchar(Vector2<u64>& position, char c, u32 color) {
    gRend.drawchar(position, c, color);
    // Increment pixel position horizontally by one character.
    position.x += 8;
    // Newline if next character would be off-screen.
    if (position.x + 8 > Target->PixelWidth)
        crlf(position);
}

void BasicRenderer::clear(Vector2<u64> position, Vector2<u64> size) {
    // Only clear what is within the bounds of the framebuffer.
    if (position.x > Target->PixelWidth
        || position.y > Target->PixelHeight) { return; }
    // Ensure size doesn't over-run edge of framebuffer.
    u64 diffX = Target->PixelWidth - position.x;
    u64 diffY = Target->PixelHeight - position.y;
    if (diffX < size.x)
        size.x = diffX;
    if (diffY < size.y)
        size.y = diffY;
    // Calculate addresses.
    u32* renderBaseAddress = (u32*)((u64)Render->BaseAddress
                                    + (BytesPerPixel * position.x)
                                    + (BytesPerPixel * position.y * Render->PixelsPerScanLine));
    // Copy rectangle line-by-line.
    for (u64 y = 0; y < size.y; ++y) {
        for (u64 x = 0; x < size.x; ++x) {
            *(renderBaseAddress + x) = BackgroundColor;
        }
        renderBaseAddress += Render->PixelsPerScanLine;
    }
}
void BasicRenderer::clear(Vector2<u64> position, Vector2<u64> size, u32 color) {
    BackgroundColor = color;
    clear(position, size);
}

/// Clear a single character to the background color behind the POSITION.
/// Effectively 'backspace'.
void BasicRenderer::clearchar(Vector2<u64>& position) {
    // Move up line if necessary.
    if (position.x < 8) {
        position.x = Target->PixelWidth;
        if (position.y >= Font->PSF1_Header->CharacterSize)
            position.y -= Font->PSF1_Header->CharacterSize;
        else position = {8, 0};
    }
    position.x -= 8;
    drawrect(position, {8, Font->PSF1_Header->CharacterSize}, BackgroundColor);
}

/// Put a string of characters `str` (null terminated) to the screen
/// with color `color` at `position`.
void BasicRenderer::puts(Vector2<u64>& position, const char* str, u32 color) {
    // Set current character to first character in string.
    char* c = (char*)str;
    // Loop over string until null-terminator.
    while (*c != 0) {
        // put current character of string at current pixel position.
        putchar(position, *c, color);
        c++;
    }
}
