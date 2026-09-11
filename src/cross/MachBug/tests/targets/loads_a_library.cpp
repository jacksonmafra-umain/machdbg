// Publishes a marker, waits, and then dlopens a library, so a module-tracking test can stop the
// target on both sides of a load it caused rather than racing whatever dyld does at startup.
//
// The library is opened by a path that exists in the shared cache and has no file on disk, which
// is the ordinary case on this platform -- dlopen resolves it out of the cache. That also makes
// the test exercise the same path the parser was built for.
#include <dlfcn.h>
#include <cstdio>
#include <unistd.h>

int main()
{
    // The marker goes out before anything is loaded, so a test knows the target has reached its
    // own code and dyld has published its startup images.
    std::printf("ready\n");
    std::fflush(stdout);

    // Long enough for a debugger to stop the target, look at its module list, and resume before
    // this happens.
    usleep(300 * 1000);

    void* handle = dlopen("/usr/lib/libcurl.4.dylib", RTLD_NOW);
    if(handle == nullptr)
        std::printf("dlopen failed: %s\n", dlerror());
    else
        std::printf("loaded\n");
    std::fflush(stdout);

    for(;;)
        sleep(1);
    return 0;
}
