// {{TITLE}}: what this port tells dreamcomp. Game facts with sources: docs/GAME-INTERNALS.md.
#include "dreamcomp/hook.h"
#include "dreamcomp/port.h"

namespace {

const dreamcomp::PortInfo kInfo = [] {
    dreamcomp::PortInfo p;
    p.id = "{{ID}}";
    p.title = "{{TITLE}}";
    return p;
}();
dreamcomp::RegisterPort g_register(kInfo);

}  // namespace
