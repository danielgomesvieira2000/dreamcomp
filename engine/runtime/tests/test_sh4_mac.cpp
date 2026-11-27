#include "dream/runtime/sh4/ops.h"

#include "doctest.h"

TEST_CASE("SH-4 MAC.L and MAC.W wrap the full accumulator with SR.S clear") {
    for (bool word : {false, true}) {
        CAPTURE(word);
        for (const bool subtract : {false, true}) {
            CAPTURE(subtract);
            dream::sh4::Ctx c{};
            c.sr = 0;
            c.mach = subtract ? 0x80000000u : 0x7fffffffu;
            c.macl = subtract ? 0u : 0xffffffffu;
            if (word)
                dream::sh4::mac_w(c, subtract ? -1 : 1, 1);
            else
                dream::sh4::mac_l(c, subtract ? -1 : 1, 1);
            CHECK(c.mach == (subtract ? 0x7fffffffu : 0x80000000u));
            CHECK(c.macl == (subtract ? 0xffffffffu : 0u));
        }
    }
}
