#include <string>
#include <vector>

#include "dream/translator/emit.h"

#include "doctest.h"

using dream::translator::emit_unit;
using dream::translator::EmitOptions;
using dream::translator::FunctionSpec;
using dream::translator::Image;

// The entry interrupt poll runs before the resume switch. On a resumed entry the guest is at
// resume_pc, so that is what SPC must record: with the function entry instead, a task switch taken
// at this poll came back to the entry and re-ran the prologue on the resumed frame's stack
// (Soulcalibur, docs/emitter-design.md "Non-local returns").
TEST_CASE("emit: the entry poll records resume_pc on a resumed entry") {
    Image image;
    image.base = 0x8C010000u;
    // mov #1,r0 ; rts ; nop
    image.bytes = {0x01, 0xE0, 0x0B, 0x00, 0x09, 0x00};
    FunctionSpec f;
    f.entry = 0x8C010000u;
    f.end = 0x8C010006u;
    const auto r = emit_unit(image, {f}, EmitOptions{});
    CHECK(r.source.find("c.pc = resume_pc ? resume_pc : 0x8c010000u; deliver_irq(c, m);") !=
          std::string::npos);
    CHECK(r.source.find("c.pc = 0x8c010000u; deliver_irq(c, m);") == std::string::npos);
}
