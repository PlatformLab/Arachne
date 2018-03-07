# Arachne Kernel API Design

## System calls
1. `initArachne(yieldInfoSharedMemoryPtr)`: Called at Arachne startup to establish a shared memory pointer for communication with ther kernel about how many threads the application should continue to run (this shared memory pointer is *not* thread local).
2. `blockUntilCoreAvailable(coreInfoSharedMemoryPtr)`: Adds the calling thread to the kernel's internal list of sleeping threads that it will wake up and run when a core becomes available. The shared memory pointer is used to tell the application which core this thread is running on (it should point to a thread local variable) after this call returns.
3. `setNumCores(priorityArray[])`: Given an array of the number of cores to allocate at each priority, the kernel will asynchronously wake up the requested number of threads from their calls to `blockUntilCoreAvailable` and allow them to run on individual cores.

## Kernel Scheduler
The kernel will have a new high-priority scheduling class that only allows one thread on its run queue at a time. It keeps track of the threads that have called `blockUntilCoreAvailable` and chooses which threads to wake up and add to a run queue when the application calls `setNumCores`. The kernel communicates via the `coreInfoSharedMemoryPtr` (provided by `blockUntilCoreAvailable`) which core this thread is running on.

When the kernel needs to communicate to the application that it wants a core back, it will write the number of cores it wants *the application* to have to the `yieldInfoSharedMemoryPtr` (provided by `init`). It does not communicate how many cores it wants back because that would require the application to write to the shared memory and create a race.

### Blocking System Calls (Kernel Side)
The kernel knows that there is a blocking system call when a run queue that previously had a thread running is empty. The kernel will wake up a sleeping thread and substitute it into the run queue, providing the thread's new core information via `coreInfoSharedMemoryPtr`.

When a blocking system call returns, the kernel will communicate to the substituted thread via `coreInfoSharedMemoryPtr` that it should yield at the next opportunity so that the original thread can run.

## Arachne Library (Userspace)
The Arachne library creates n+k kernel threads (using `std::thread`), where n is the number of cores and k is some extra number of cores to have in reserve in case of blocking system calls. It adds more threads to the pool if it ever runs out. We are creating threads in userspace under the assumption that it is easier to deal with them there than in the kernel. Each thread will have the following main loop:

    while (true):
        blockUntilCoreAvailable()
        if we are the first thread running on this core (check shared memory):
            initialize state
            run normally
        else:
            // There was a blocking system call on this core
            increment counter for the threads running on this core
            run until the kernel tells us via shared memory that we should yield


When the application notices via the `yieldInfoSharedMemoryPtr` that it needs to give back some number of cores, it will choose which cores to give up and then call `blockUntilCoreAvailable` on all threads running on that core.

# Open Questions
1. How will priorities work?
2. What is the minimum set of functions we need to implement in the scheduler?
