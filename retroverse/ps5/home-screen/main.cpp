/* RetroVerse PS5 Home Screen Foreground Display Test
 * Based on ps5-native-app-boilerplate (GPL-3.0-or-later).
 * This is an app-context VideoOut proof, not the complete downloadable storefront.
 */
#include "demo_renderer.hpp"
#include <cstdint>

namespace {
using ps5::demo::Canvas;
using ps5::demo::Color;
constexpr Color ink = static_cast<Color>(UINT32_C(0xff160d0b));
constexpr Color ink2 = static_cast<Color>(UINT32_C(0xff29202a));
constexpr Color navy = static_cast<Color>(UINT32_C(0xff41211c));
constexpr Color purple = static_cast<Color>(UINT32_C(0xfff4aabb));
constexpr Color cyan = static_cast<Color>(UINT32_C(0xffefd66b));
constexpr Color offwhite = static_cast<Color>(UINT32_C(0xfff9f4ed));

void draw_scene(Canvas &c) noexcept {
    c.clear(ink);
    c.rectangle(0, 0, 1920, 164, ink2);
    c.rectangle(0, 160, 1920, 7, purple);
    c.text(90, 43, "RETROVERSE", 14, offwhite);
    c.text(1080, 68, "PS5  /  NATIVE APP", 6, cyan);

    c.rectangle(66, 224, 1788, 676, ink2);
    c.rectangle(73, 231, 1780, 5, purple);

    // Native TV display test composition: television + system selection.
    c.rectangle(140, 278, 780, 543, navy);
    c.rectangle(155, 293, 748, 504, offwhite);
    c.rectangle(175, 313, 708, 464, ink);
    c.rectangle(214, 353, 634, 379, ink2);
    c.circle(534, 541, 139, purple);
    c.circle(534, 541, 96, cyan);
    c.rectangle(467, 520, 132, 43, ink2);
    c.text(392, 763, "CRT  TV", 5, offwhite);

    c.text(1006, 298, "WELCOME TO", 7, cyan);
    c.text(1006, 383, "RETROVERSE", 10, offwhite);
    c.text(1006, 520, "NATIVE DISPLAY TEST", 5, purple);
    c.text(1006, 603, "THE APP OPENED ON YOUR TV", 4, offwhite);
    c.text(1006, 668, "FOREGROUND VIDEO IS WORKING", 4, cyan);

    c.rectangle(1006, 754, 242, 55, purple);
    c.rectangle(1268, 754, 240, 55, cyan);
    c.rectangle(1528, 754, 244, 55, navy);
    c.text(1045, 769, "STORE", 4, ink);
    c.text(1312, 769, "LIBRARY", 4, ink);
    c.text(1555, 769, "DOWNLOADS", 4, offwhite);

    c.text(80, 958, "RETROVERSE TV ALPHA", 5, purple);
    c.text(880, 963, "FOREGROUND APP VIDEO TEST ONLY", 4, offwhite);
}
} // namespace

int main() {
    ps5::demo::run(draw_scene, "RetroVerse home-screen app display test started");
}
