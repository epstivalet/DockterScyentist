#!/usr/bin/env python3
"""Extend the pinned GPL-3.0 native boilerplate renderer, without patching kernel APIs."""
from pathlib import Path
import sys
root=Path(sys.argv[1])
hpp=root/"src/demo_renderer.hpp"; cpp=root/"src/demo_renderer.cpp"
h=hpp.read_text();s=cpp.read_text()
def swap(txt,a,b):
    if a not in txt:raise RuntimeError("Patch anchor missing: "+repr(a[:85]))
    return txt.replace(a,b,1)
h=swap(h,"    void text(unsigned x, unsigned y, std::string_view value, unsigned scale, Color color) noexcept;",
       "    void text(unsigned x, unsigned y, std::string_view value, unsigned scale, Color color) noexcept;\n"
       "    void image(unsigned x, unsigned y, unsigned w, unsigned h, const std::uint32_t* abgr, unsigned iw, unsigned ih) noexcept;")
s=swap(s,"extern \"C\"\n{\n","extern \"C\"\n{\n    bool retroverse_process_input();\n")
after="""void read_asset_text(const char *path, std::span<char> destination,"""
extra="""void Canvas::image(unsigned x, unsigned y, unsigned w, unsigned h,
                   const std::uint32_t *abgr, unsigned iw, unsigned ih) noexcept
{
    if (!abgr || !iw || !ih || !w || !h) return;
    if (x >= 1920 || y >= 1080) return;
    if (w > 1920-x) w = 1920-x;
    if (h > 1080-y) h = 1080-y;
    for (unsigned row=0;row<h;row++) {
        unsigned sy = (std::uint64_t(row)*ih)/h;
        for (unsigned col=0;col<w;col++) {
            unsigned sx=(std::uint64_t(col)*iw)/w;
            put_pixel_unchecked(pixels_,x+col,y+row,static_cast<Color>(abgr[sy*iw+sx]));
        }
    }
}
"""
s=swap(s,after,extra+"\n"+after)
old="""    // Returning from main or calling exit crashes this launch context.
    for (;;)
        (void)sceKernelUsleep(1000000);"""
new="""    // Background-helper bootstrap happens in main(); each input change redraws
    // exactly one PS5 tiled video buffer. The foreground title remains alive.
    int frame = 1;
    for (;;) {
        if (retroverse_process_input()) {
            if (frame == 0) draw(first);
            else draw(second);
            auto *addr=static_cast<std::uint8_t*>(mapped)+std::size_t(frame)*frame_bytes;
            flush_range(addr,frame_bytes);
            (void)sceVideoOutSubmitFlip(video,frame,1,1);
            (void)sceVideoOutWaitVblank(video);
            frame=1-frame;
        }
        (void)sceKernelUsleep(33000);
    }"""
s=swap(s,old,new)
hpp.write_text(h);cpp.write_text(s)
print("Extended VideoOut renderer for native storefront + image + controller loop")
