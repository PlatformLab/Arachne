/* Copyright (c) 2017 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef COREMANAGER_H_
#define COREMANAGER_H_

#include <string.h>
#include <atomic>

namespace Arachne {
/*
 * An unordered list of cores. This class's thread-safety is dependent on the
 * thread-safety of torn reads.
 */
struct CoreList {
    /**
     * Each instance allocates all the memory it will use at the time of
     * construction.
     */
    explicit CoreList(int capacity, bool mustFree = false)
        : capacity(static_cast<uint16_t>(capacity)), mustFree(mustFree) {
        cores = new int[capacity];
        numFilled = 0;
    }
    /**
     * Free memory iff this core has not already been mvoed.
     */
    ~CoreList() {
        if (cores != NULL)
            delete[] cores;
    }
    CoreList& operator=(CoreList&& other) {
        numFilled = other.numFilled;
        capacity = other.capacity;
        mustFree = other.mustFree;
        cores = other.cores;
        other.cores = NULL;
        other.capacity = 0;
        other.numFilled = 0;
        other.mustFree = false;
        return *this;
    }
    CoreList(CoreList& other) { *this = other; }
    CoreList& operator=(const CoreList& other) {
        numFilled = other.numFilled;
        capacity = other.capacity;
        mustFree = other.mustFree;
        cores = new int[capacity];
        memcpy(cores, other.cores, numFilled * sizeof(cores[0]));
        return *this;
    }
    CoreList(CoreList&& other) { *this = std::move(other); }
    uint32_t size() { return numFilled; }
    void add(int coreId) {
        this->cores[numFilled] = coreId;
        this->numFilled++;
    }
    void remove(int index) {
        memmove(cores + index, cores + index + 1,
                (numFilled - index - 1) * sizeof(int));
        numFilled--;
    }

    /**
     * Users of CoreList outside of CoreManager implementations MUST call this
     * function after they are done with the CoreList.
     */
    void free() {
        if (mustFree)
            delete this;
    }
    int& operator[](std::size_t index) { return cores[index]; }
    int& get(std::size_t index) { return cores[index]; }

    /* The number of cores in the list */
    uint16_t numFilled;

    /* The maximum number of cores in the list */
    uint16_t capacity;

    /*
     * An array containing IDs for cores in this list.
     */
    int* cores;

    /*
     * This variable indicates whether this CoreList should be deleted by free.
     */
    bool mustFree;
};

/**
 * Implementors of this interface specify the allocation and use of cores in
 * Arachne.
 */
class CoreManager {
  public:
    /**
     * Invoked by an Arachne kernel thread after it wakes up on a dedicated
     * core and sets up state to run the Arachne scheduler.
     */
    virtual void coreAvailable(int myCoreId) = 0;

    /**
     * Invoked by Arachne to get a CoreId when any core detects a core release
     * request from the Core Arbiter.
     */
    virtual int coreUnavailable() = 0;

    /**
     * Invoked by Arachne::createThread to get cores available for a particular
     * threadClass.
     */
    virtual CoreList* getCores(int threadClass) = 0;

    /**
     * Invoked by Arachne::descheduleCore to get a list of cores to migrate to
     * when clearing out a core.
     */
    virtual CoreList* getMigrationTargets() = 0;

    virtual ~CoreManager() {}
};

}  // namespace Arachne
#endif  // COREMANAGER_H_
