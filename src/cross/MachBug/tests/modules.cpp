// Milestone 5 task 2: what dyld has loaded into a live target, read out of the target's own
// memory rather than out of this process's.
#include <catch2/catch_test_macros.hpp>

#include <mach/mach.h>

#include <chrono>
#include <cstdio>
#include <thread>
#include <string>
#include <vector>

#include <MachBug/core/Debugger.h>
#include <MachBug/core/Modules.h>
#include <MachBug/macho/Image.h>
#include <MachBug/macho/Reader.h>

#include "TestHarness.h"

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

namespace
{
    bool fileExists(const std::string& path)
    {
        if(FILE* file = std::fopen(path.c_str(), "rb"))
        {
            std::fclose(file);
            return true;
        }
        return false;
    }
}

TEST_CASE("a stopped target reports the images dyld has loaded into it")
{
    RecordingDebugger debugger;
    const std::string fixture = FIXTURE("run_endlessly");
    REQUIRE(debugger.Init(fixture.c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    // MEASURED: at the very first stop the image array is not there yet. dyld publishes it as
    // it goes and sets the pointer to NULL while it is writing (mach-o/dyld_images.h:52), and
    // the first stop is dyld's own notification trap -- before any of this engine's callers
    // would think to ask, but exactly when a debugger's own startup code does. Enumerate()
    // answers "nothing yet, true" there rather than failing, and the list fills in once the
    // target has run.
    std::vector<MachBug::Modules::Image> atFirstStop;
    std::string firstError;
    REQUIRE(MachBug::Modules::Enumerate(debugger.GetTaskPort(), &atFirstStop, &firstError));
    INFO("at the first stop: " << atFirstStop.size() << " images, diagnostic: " << firstError);

    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Resumed));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    debugger.Pause();
    REQUIRE(debugger.IsStopped());

    std::vector<MachBug::Modules::Image> images;
    std::string error;
    REQUIRE(MachBug::Modules::Enumerate(debugger.GetTaskPort(), &images, &error));
    INFO("diagnostic: " << error << "; " << images.size() << " images");

    // A trivial program loads 45 on this machine. Ten is a floor that says "this is a real
    // image list" without asserting a number that belongs to an OS release rather than to this
    // engine.
    REQUIRE(images.size() > 10);

    bool sawFixture = false;
    bool sawSharedCacheImage = false;
    for(const MachBug::Modules::Image& image : images)
    {
        if(image.path == fixture)
        {
            sawFixture = true;
            REQUIRE(image.loadAddress > 0x1000);
            REQUIRE(image.size > 0);
        }
        // The case the whole byte-reader design exists for: an image with a path that names no
        // file. If this ever stops being true, the parser could have taken a path all along and
        // the reason for the interface is gone -- which is worth a failing test, not a silent
        // change of circumstances.
        if(!image.path.empty() && image.path.rfind("/usr/lib/", 0) == 0 &&
           !fileExists(image.path))
        {
            sawSharedCacheImage = true;
        }
    }

    REQUIRE(sawFixture);
    REQUIRE(sawSharedCacheImage);
}

TEST_CASE("the reported slide is what turns a link-time address into a runtime one")
{
    RecordingDebugger debugger;
    const std::string fixture = FIXTURE("run_endlessly");
    REQUIRE(debugger.Init(fixture.c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));
    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Resumed));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    debugger.Pause();

    std::vector<MachBug::Modules::Image> images;
    std::string error;
    REQUIRE(MachBug::Modules::Enumerate(debugger.GetTaskPort(), &images, &error));

    const MachBug::Modules::Image* main = nullptr;
    for(const MachBug::Modules::Image& image : images)
    {
        if(image.path == fixture)
            main = &image;
    }
    REQUIRE(main != nullptr);

    // Checked against the image itself, not recomputed the same way twice: the slide is only
    // worth anything if loadAddress - slide really is where the linker put __TEXT.
    MachBug::macho::MemoryReader reader(debugger.GetTaskPort());
    MachBug::macho::Image parsed;
    REQUIRE(MachBug::macho::Parse(reader, main->loadAddress, &parsed, &error));
    INFO("diagnostic: " << error);
    INFO("loaded at 0x" << std::hex << main->loadAddress << ", linked at 0x" << parsed.textVmAddr
         << ", slide 0x" << main->slide);
    REQUIRE(main->loadAddress - main->slide == parsed.textVmAddr);

    // And it is a real slide. ASLR is on for these fixtures, so a zero here is what a stub that
    // forgot to subtract looks like -- the one wrong answer that would otherwise pass the line
    // above as well.
    REQUIRE(main->slide != 0);
}

TEST_CASE("enumerating without a target is refused rather than answered emptily")
{
    std::vector<MachBug::Modules::Image> images;
    std::string error;
    REQUIRE_FALSE(MachBug::Modules::Enumerate(MACH_PORT_NULL, &images, &error));
    INFO("diagnostic: " << error);
    REQUIRE_FALSE(error.empty());
    REQUIRE(images.empty());
}

TEST_CASE("a library the target loads is reported, and the ones it started with are not")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("loads_a_library").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    // The baseline is taken at the first stop that actually sees images, which is not this one:
    // at the very first stop dyld has published nothing. Resuming and pausing again is what
    // establishes it -- and the fixture sleeps 300ms before its dlopen, so this lands before it.
    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Resumed));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    debugger.Pause();

    const std::size_t baseline = debugger.ModuleCount();
    INFO("baseline: " << baseline << " images, "
         << debugger.count(EventType::LoadModule) << " load events so far");
    REQUIRE(baseline > 10);

    // The forty-odd libraries the target was launched with are not events: they were not loaded
    // under this engine's watch. This is the assertion that would fail if an empty first read
    // were taken as the baseline.
    REQUIRE(debugger.count(EventType::LoadModule) == 0);

    // Now let the fixture reach its dlopen. No WaitFor(Resumed) here, unlike above: this
    // Continue is undoing a Pause, and Pause/Continue is synchronous -- task_resume has already
    // returned by the time Continue does, so the engine fires no event for it (Debugger.h says
    // so on cbResumed). Waiting for one here hangs for the full timeout and then fails.
    debugger.Continue();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    debugger.Pause();

    std::string loadedPath;
    uint64_t loadedBase = 0;
    for(const MachBug::test::Event& event : debugger.events())
    {
        if(event.type != EventType::LoadModule)
            continue;
        if(event.message.find("libcurl") != std::string::npos)
        {
            loadedPath = event.message;
            loadedBase = event.moduleBase;
        }
    }

    INFO("after the dlopen: " << debugger.ModuleCount() << " images, "
         << debugger.count(EventType::LoadModule) << " load events; libcurl at 0x"
         << std::hex << loadedBase << " (" << loadedPath << ")");
    REQUIRE_FALSE(loadedPath.empty());
    REQUIRE(loadedBase > 0x1000);
    REQUIRE(debugger.ModuleCount() > baseline);
}

TEST_CASE("a stop with nothing loaded in between reports nothing")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Resumed));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    debugger.Pause();
    const std::size_t afterBaseline = debugger.count(EventType::LoadModule);

    // Three more stops, with a target that loads nothing. A diff that compared against the
    // wrong thing, or forgot to store what it saw, reports the whole list again every time --
    // and a caller building a module list from these events would have it four times over.
    for(int i = 0; i < 3; ++i)
    {
        debugger.Continue();   // undoing a Pause, so no Resumed event -- see the test above
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        debugger.Pause();
    }

    INFO("load events after the baseline: " << debugger.count(EventType::LoadModule));
    REQUIRE(debugger.count(EventType::LoadModule) == afterBaseline);
    REQUIRE(debugger.count(EventType::UnloadModule) == 0);
}
