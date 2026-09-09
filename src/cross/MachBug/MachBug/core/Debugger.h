#pragma once

#include <sys/types.h>
#include <mach/mach.h>
#include <mach/exception_types.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include <MachBug/types/MachBug.h>
#include <MachBug/types/Global.h>
#include <MachBug/core/Breakpoints.h>
#include <MachBug/core/Threads.h>
#include <MachBug/process/Process.h>

namespace MachBug
{
    // Milestone 2 task 4 adds the exception loop: Start() installs a Mach exception port on the
    // task, resumes the child Task 3 left suspended, and then owns a receive loop for the rest
    // of the target's life. Continue/StepInto/Pause/Stop are callable from another thread while
    // Start() blocks, matching the split machbug_api.h documents.
    //
    // THE INVERSION THAT MATTERS: ElfBug stops because waitpid() returned -- ptrace leaves the
    // tracee stopped in the kernel and the tracer finds out about it later, asynchronously.
    // MachBug has no such kernel-level "stopped" state to poll for. A Mach exception is a
    // message; the thread that raised it is not resumed until *something* replies to that exact
    // message, and nothing requires the reply to be immediate. So here, the stopped state IS the
    // pending, unanswered reply -- there is no other flag or kernel bit that means "stopped."
    // Concretely: MIG's generated dispatcher (mach_exc_server, called from exceptionLoop() in
    // Debugger.Loop.cpp) invokes handleException() synchronously for every exception message,
    // and *returning from handleException() is what sends the reply and lets the target run
    // again* -- MIG does that send, not this class. So handleException() must not return while
    // the user expects the target stopped: it parks on the command queue (mCmdMutex/mCmdCv)
    // until Continue(), StepInto() or Stop() posts a decision, then returns the kern_return_t
    // that expresses it. See Debugger.Loop.cpp and ExceptionServer.cpp for the two halves of
    // this (the receive loop, and the MIG entry point), and MachBugMigStubs.c for why the real
    // handler must be extern "C" to ever be linked in at all.
    //
    // Undeclared-until-now divergence, noted here since it was not called out earlier: where
    // ElfBug::Debugger tracks its children in an unordered_map<pid_t, Process> (multiple
    // tracees under one Debugger), this class holds a single unique_ptr<Process>. That is a
    // deliberate simplification for a single-target debugger, not an oversight -- nothing in
    // this milestone attaches to more than one process at a time -- but it means this class
    // cannot yet track multiple children the way ElfBug's map shape implies it eventually will.
    // Revisit if/when multi-process debugging becomes a requirement.
    class Debugger
    {
    public:
        Debugger();

        // If Start() is running on another thread when this runs, that thread must already have
        // returned (i.e. the caller has Stop()'d the loop and joined it) -- the destructor tears
        // down mProcess via Terminate(), and the loop thread reads mProcess for the rest of its
        // life. Nothing here synchronizes against a Start() still in flight; that ordering is
        // the caller's responsibility, the same way joining a std::thread before destroying
        // whatever it captured by reference is.
        virtual ~Debugger();

        // Launches szFilePath suspended (POSIX_SPAWN_START_SUSPENDED) and takes its task port
        // via task_for_pid. The child exists but has executed no instruction of its own when
        // this returns -- that window is deliberate, it is what lets Start() install exception
        // ports before the target can run and race the debugger. Returns false, leaving
        // GetPid()/GetTaskPort() at 0/MACH_PORT_NULL, if the path cannot be executed or the
        // task port cannot be obtained; a partially-launched (suspended, task-for-pid-denied)
        // child is killed rather than leaked.
        bool Init(const char* szFilePath, const char* const* argv = nullptr);

        // Attaches to a process that is already running, rather than launching one -- Task 6's
        // addition to Init()'s launch path. Reaches for ptrace(PT_ATTACHEXC), the same call
        // Start() makes for a launched child (see Debugger.Loop.cpp), not PT_ATTACH: the EXC
        // variant is what makes the kernel route every signal pid would otherwise receive
        // through a Mach exception port once one exists, which is the entire model this engine
        // is built on -- PT_ATTACH would attach pid to ordinary signal-based tracing this
        // engine's exception loop has no way to observe at all.
        //
        // Unlike Init()'s POSIX_SPAWN_START_SUSPENDED, PT_ATTACHEXC's own attach does not
        // synchronously stop pid: per ptrace(2), it sends SIGSTOP and leaves the caller to
        // waitpid() for it, which this call does before returning -- that waitpid() is also what
        // completes PT_ATTACHEXC's reparent of pid to this process for tracing purposes, the
        // reason a later waitpid() (Start()'s exceptionLoop(), in Debugger.Loop.cpp) can wait on
        // a pid this process did not itself spawn. Only once that stop is confirmed does this
        // call take the task port, for the same reason Init() waits for the suspended-at-exec
        // window before doing so: task_set_exception_ports must be installed (by Start(), next)
        // before anything can run and race the debugger.
        //
        // On a task_for_pid denial, unlike Init()'s equivalent failure, pid is not killed: it did
        // not come from this engine, and a caller reaching for Attach() does not get to have
        // someone else's process killed just because this engine failed to finish taking control
        // of it. It is instead detached and left running, resumed exactly as if Attach() had
        // never been called.
        //
        // Returns false, leaving GetPid()/GetTaskPort() at 0/MACH_PORT_NULL, if pid is not
        // positive, cannot be attached to, exits before the attach completes, or denies
        // task_for_pid. See LastTaskForPidResult() for distinguishing that last case from the
        // others.
        bool Attach(pid_t pid);

        pid_t GetPid() const;
        mach_port_t GetTaskPort() const;

        // The kern_return_t from the most recent task_for_pid call this object made, via Init()
        // or Attach(), or KERN_SUCCESS if neither has been called yet. Exists so the C API layer
        // (machbug_api.cpp) can distinguish a denied task_for_pid -- machbug_api.h documents
        // DbgStatus_NotPermitted specifically for this -- from any other launch/attach failure,
        // rather than collapsing every failure into one generic status.
        kern_return_t LastTaskForPidResult() const;

        // Kills a child that Init() launched but that was never resumed, or that Start()'s loop
        // is no longer tracking (Start() already returned). Returns false if there is no
        // launched child to terminate.
        bool Terminate();

        // Blocking; owns the exception receive loop for as long as the child lives. Attaches with
        // ptrace(PT_ATTACHEXC) (needed so a target that never faults on its own still generates a
        // first stop -- see Debugger.Loop.cpp's comment), installs the task's exception ports
        // (EXCEPTION_STATE_IDENTITY | MACH_EXCEPTION_CODES, covering EXC_BREAKPOINT/EXC_BAD_ACCESS/
        // EXC_BAD_INSTRUCTION/EXC_ARITHMETIC/EXC_SOFTWARE) on the still-suspended task, resumes it,
        // then alternates between waiting for an exception message and polling for the child's
        // exit -- a clean exit raises no Mach exception, so waitpid(WNOHANG) is what notices it
        // (this process is the child's real parent via posix_spawn, so that call is always valid
        // here, unlike in an attach-based debugger; it also has to tell an actual exit apart from
        // a ptrace stop notification the same PT_ATTACHEXC makes waitpid() report -- see
        // Process::IsRealExit()). Returns once the child has exited or Stop() has ended the loop.
        // Requires a prior successful Init(); call it once per launched child -- a fresh Init()
        // (which a Debugger reused for a new target calls before this) resets the bookkeeping a
        // second Start() would otherwise still see from the previous one.
        bool Start();

        // Thread-safe: intended to be called from a thread other than the one blocked in
        // Start(). Continue()/StepInto() only have an effect while the target is parked in
        // handleException() (IsStopped() true because of a pending exception reply) -- they
        // post a decision to the command queue that the parked call is waiting on. Stop() always
        // has an effect: it flags the loop to end, waking a parked handleException() if there is
        // one so its reply still goes out before the child is killed.
        void Continue();
        void StepInto();
        void Stop();

        // Thread-safe. Unlike Continue/StepInto/Stop, Pause() does not go through the command
        // queue: there may be no thread parked in handleException() to hand a decision to at all
        // -- a free-running target only ever raises an exception when its own code faults, traps,
        // or (via PT_ATTACHEXC) receives a signal, none of which Pause() can wait around for -- so
        // it reaches for task_suspend() directly instead, the same primitive SIGSTOP would use on
        // ElfBug's side, not a signal, since Mach has no signal-based process control. Idempotent
        // (a second Pause() before a Continue() is a no-op, not a second task_suspend()) and, like
        // Continue()/StepInto()/Stop(), fully serialized against handleException() by mCmdMutex,
        // so it cannot race a stop that is in the middle of happening. Calling it while already
        // stopped at an exception is also a no-op: the target cannot get any less running than
        // that.
        void Pause();

        // True once handleException() has parked on the command queue for an unanswered
        // exception, or once Pause() has task_suspended the task directly -- either way, nothing
        // in the target can execute an instruction right now.
        bool IsStopped() const;

        // True from the start of Start() until its exceptionLoop() returns.
        bool IsRunning() const;

        // The thread port that raised the exception the target is currently parked on, or
        // MACH_PORT_NULL when nothing is parked. Registers are per-thread and this milestone has
        // no thread enumeration (task_threads is milestone 5), so this port -- handed to
        // handleException() by MIG and recorded there -- is the only thread identity the engine
        // has. It is valid for exactly as long as the stop is: cleared when the reply goes out.
        mach_port_t StoppedThread() const;

        // Turns the C API's threadId into a thread port. threadId 0 means "the thread stopped at
        // the current exception" -- the case a register view has, since a caller reading
        // registers is by definition looking at a stop. Any other value is taken as a
        // mach_port_t and checked against the target's threads before being returned, so a stale
        // or invented id resolves to MACH_PORT_NULL rather than reaching thread_get_state.
        mach_port_t ResolveThread(uint64_t threadId) const;

        // The breakpoint table this engine is enforcing. Public because the C API layer drives it
        // (SetBreakpoint/DeleteBreakpoint) and the exception loop consults it -- it is engine
        // state rather than a private detail of the loop, and a caller holding a Debugger is
        // already holding the thing that owns the target.
        Breakpoints& BreakpointTable();

        // How many threads the target had at the last stop. Not a live query: the engine looks
        // when it stops the target, because that is the only moment it can act on what it sees.
        std::size_t ThreadCount() const;

        // Internal: the MIG dispatch trampoline in ExceptionServer.cpp calls this, on the same
        // thread that is running exceptionLoop(), for every Mach exception message it decodes.
        // Not for any other caller -- there is exactly one legitimate caller
        // (catch_mach_exception_raise_state_identity), and it is documented here rather than
        // hidden behind a friend declaration only because mach_exc.h has no extern "C" guard and
        // pulling it into this header to spell a friend declaration would leak MIG's generated
        // types into every translation unit that includes Debugger.h.
        kern_return_t handleException(mach_port_t thread, exception_type_t exception,
                                       mach_exception_data_t code, mach_msg_type_number_t codeCnt);

        // Internal: the Debugger instance whose exceptionLoop() is dispatching a message on the
        // calling thread right now, or nullptr. Set/cleared only by exceptionLoop() itself,
        // immediately around its call into mach_exc_server() -- see Debugger.Loop.cpp for why a
        // thread-local pointer is enough here and a global port->Debugger* registry is not
        // needed: MIG's catch_* callback is always invoked synchronously, on the same thread,
        // from within that one call.
        static Debugger* Current();

    protected:
        // Called with a diagnostic whenever an operation fails. The default implementation
        // discards it; a subclass (or a recording test harness) overrides it to observe why.
        virtual void cbInternalError(const std::string & error);

        // Fired once, from the loop thread, when Start() has installed the exception ports and
        // is about to resume the (still suspended) child.
        virtual void cbCreateProcessEvent(pid_t pid);

        // Fired once, from the loop thread, when the child has exited -- detected via
        // waitpid(WNOHANG) inside exceptionLoop() (see Start()'s comment), not via any Mach
        // exception. exitCode mirrors ElfBug's convention: WEXITSTATUS() for a normal exit,
        // -WTERMSIG() for one killed by a signal.
        virtual void cbExitProcessEvent(int exitCode);

        // Fired from the loop thread for the first exception the target ever raises. On a
        // freshly resumed process with an exception port installed before it ran a single
        // instruction, this is dyld's own debugger-notification trap -- the same "stopped at
        // entry" signal ElfBug gets for free from PTRACE_TRACEME's initial SIGTRAP, except here
        // it had to be asked for explicitly by installing the port before resuming.
        virtual void cbSystemBreakpoint();

        // Fired from the loop thread once the reply to an exception has actually been sent --
        // i.e. the target thread the exception stopped is running again. That send, not the
        // Continue()/StepInto() call that decided it, is what resumes the target (see the class
        // comment above), so this is the only point from which "the target is genuinely running"
        // can be reported rather than guessed at.
        //
        // Deliberately not fired for Pause()/Continue()'s task_suspend()/task_resume() pairing:
        // that path is synchronous on the caller's own thread -- task_resume() has already
        // returned by the time Continue() does -- so a caller there needs no notification to
        // learn what it just did itself.
        virtual void cbResumed();

        // Fired from the loop thread when a single-step armed by StepInto() completes (the
        // thread raised the trap that single-stepping itself causes, rather than a fresh,
        // unrelated exception).
        virtual void cbStep();

        // Fired when the engine first sees a thread that was not there at the previous stop, and
        // when one it knew about is gone. `threadId` is a Mach thread port, the same identity
        // ResolveThread() accepts.
        //
        // Fired from whichever thread observed the stop: the loop thread for an exception, and
        // the caller's own thread for Pause(), which suspends the target synchronously and so is
        // the one stop the loop never hears about. That is a wart, and it is written here rather
        // than discovered: a caller that touches UI state from these must marshal, exactly as it
        // already must for Pause() itself.
        virtual void cbThreadCreate(uint64_t threadId);
        virtual void cbThreadExit(uint64_t threadId);

        // Fired from the loop thread when the target stopped on a breakpoint this engine
        // installed. `address` is the breakpoint's own address -- the one the caller asked for,
        // after the architecture's program-counter fixup -- not the raw program counter the
        // exception arrived with, which on x86-64 is one byte past it.
        virtual void cbBreakpoint(uint64_t address);

        // Fired from the loop thread for any exception other than the first one and any
        // step-completion -- i.e. one this milestone has no more specific handling for yet.
        // `address`, when known, is the Mach exception's own subcode (code[1] under
        // MACH_EXCEPTION_CODES, e.g. the faulting address for EXC_BAD_ACCESS); it is 0 when the
        // exception did not report one. Breakpoint-specific dispatch (matching an address
        // against installed breakpoints) is a later milestone's job, not this task's.
        virtual void cbException(uint32_t type, uint64_t address);

    private:
        bool spawnSuspended(const char* szFilePath, const char* const* argv, pid_t* outPid);

        // The mach_msg receive loop; runs on the thread that called Start() until the child
        // exits or Stop() ends it. Defined in Debugger.Loop.cpp.
        void exceptionLoop();

        enum class Command
        {
            None,
            Continue,
            StepInto,
            Stop,
        };

        std::unique_ptr<Process> mProcess;

        // True once Attach() has already ptrace(PT_ATTACHEXC)'d mProcess->pid, so Start() knows
        // to skip its own call of the same -- ptrace(2) does not allow attaching to an
        // already-attached pid a second time (EBUSY), and there is nothing useful a second call
        // would add: Attach() already established the one relationship Start() needs (pid marked
        // as a Mach-exception tracee of this process). Init()'s launch path leaves this false;
        // Start() still needs to make that call itself there, since launching never establishes
        // the ptrace relationship on its own. Reset on Terminate() along with mProcess.
        bool mAttachedViaPtrace = false;

        // See LastTaskForPidResult()'s comment.
        kern_return_t mLastTaskForPidResult = KERN_SUCCESS;

        mach_port_t mExceptionPort = MACH_PORT_NULL;

        std::atomic<bool> mIsRunning{false};
        std::atomic<bool> mStopRequested{false};

        // True exactly while a thread is parked inside handleException(), i.e. an exception
        // reply is pending. Loop-thread writes, any-thread reads -- Continue()/StepInto() read
        // it (under mCmdMutex) to decide whether there is anything parked to hand a decision to.
        std::atomic<bool> mStopped{false};

        // Written by handleException() on the loop thread, read by any thread through
        // StoppedThread()/ResolveThread(). Atomic for the same reason mStopped is: the reader is
        // usually a UI thread and the writer is always the loop thread.
        std::atomic<mach_port_t> mStoppedThread{MACH_PORT_NULL};

        // True while Pause() has task_suspended the task directly (no parked handleException()
        // to route the pause through). Continue() clears it with a matching task_resume() when
        // there is nothing else to resume via the command queue.
        std::atomic<bool> mPausedByUser{false};

        // True once handleException() has armed a single-step (StepInto()) and is waiting to
        // see whether the *next* exception is that step completing. Loop-thread only -- nothing
        // else touches it, so it is a plain bool rather than an atomic.
        bool mStepArmed = false;

        // Loop-thread only: distinguishes the very first exception (-> cbSystemBreakpoint) from
        // every later one (-> cbException), once mStepArmed has already been ruled out.
        bool mSeenFirstStop = false;

        // The breakpoints this engine has installed in the target.
        Breakpoints mBreakpoints;

        // The target's threads as of the last stop, and the diff that produced the last pair of
        // callbacks.
        Threads mThreads;

        // Looks at the target's thread list and fires cbThreadCreate/cbThreadExit for what
        // changed. Called at every stop this engine creates -- see cbThreadCreate's comment for
        // which thread that leaves the callbacks on.
        void refreshThreads();

        // Tells the kernel not to deliver the signal that accompanies a trap this engine caused.
        // See the implementation for the two failures that made it necessary.
        void suppressPendingSignal(mach_port_t thread, const char* what);

        // Loop-thread only. The breakpoint the target is currently stopped on, or 0: resuming
        // from it needs the trap taken out first, and this is what remembers which one to put
        // back afterwards.
        uint64_t mStoppedAtBreakpoint = 0;

        // Loop-thread only. True while the engine is single-stepping purely to get off a
        // breakpoint's address. The step that follows is machinery rather than something the
        // caller asked for, so its completion re-arms the trap and resumes without reporting --
        // unless mStepWasRequested says the caller asked for a step as well, in which case the
        // completion is theirs to hear about.
        bool mSteppingOverBreakpoint = false;
        bool mStepWasRequested = false;

        // Guards mPendingCommand; handleException() waits on mCmdCv for Continue()/StepInto()/
        // Stop() to post one. This -- and nothing else in this class -- is the thread-safety
        // split machbug_api.h documents: Start() (and therefore handleException(), which only
        // ever runs on Start()'s thread) owns the loop, and these four are what a second thread
        // uses to talk to it.
        std::mutex mCmdMutex;
        std::condition_variable mCmdCv;
        Command mPendingCommand = Command::None;
    };
}
