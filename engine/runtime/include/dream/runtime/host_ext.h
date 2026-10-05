// Launcher extensions (dreamcomp addition): code linked into a title's executable can add
// command-line flags and run at fixed points of a run without editing the launcher.
//
// An extension is any object that registers itself during static initialisation:
//
//     struct Widescreen final : dream::host::Extension { ... };
//     static Widescreen ws;
//     static dream::host::Register reg_ws(ws);
//
// Points, in run order: parse_arg (each argv entry the launcher does not know), on_start (RAM
// loaded, devices installed, before the first guest instruction), on_vblank (every vblank-out, in
// the guest thread, before the frame is presented), on_stop (after the guest stopped, before the
// report). All run on the guest thread; none may block.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace dream {
class System;
}

namespace dream::host {

class Extension {
public:
    virtual ~Extension() = default;
    virtual const char* name() const = 0;
    // Before anything is parsed: the whole command line (args[0] is the program) may be edited,
    // e.g. to turn a bare double-click into a configured windowed run from saved settings.
    virtual void adjust_args(std::vector<std::string>& args) { (void)args; }
    // argv[i] is a flag the launcher did not recognise. Return how many argv entries this
    // extension consumed (0: not mine).
    virtual int parse_arg(int i, int argc, char** argv) {
        (void)i, (void)argc, (void)argv;
        return 0;
    }
    virtual void usage(std::FILE* out) { (void)out; }
    virtual void on_start(System& sys) { (void)sys; }
    virtual void on_vblank(System& sys) { (void)sys; }
    virtual void on_stop(System& sys, const char* why) { (void)sys, (void)why; }
};

inline std::vector<Extension*>& extensions() {
    static std::vector<Extension*> list;
    return list;
}

struct Register {
    explicit Register(Extension& e) { extensions().push_back(&e); }
};

}  // namespace dream::host
