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
 * An unordered list of cores.
 */
struct CoreList {
    explicit CoreList(int capacity)
        : capacity(static_cast<uint16_t>(capacity)) {
        cores = new int[capacity];
        numFilled = 0;
    }
    ~CoreList() {
        if (cores != NULL)
            delete cores;
    }
    CoreList& operator=(CoreList&& other) {
        numFilled = other.numFilled;
        capacity = other.capacity;
        cores = other.cores;
        other.cores = NULL;
        return *this;
    }
    CoreList& operator=(const CoreList& other) {
        numFilled = other.numFilled;
        capacity = other.capacity;
        cores = new int[capacity];
        memcpy(cores, other.cores, numFilled * sizeof(cores[0]));
        return *this;
    }
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
    int& operator[](std::size_t index) { return cores[index]; }

    /* The number of cores in the list */
    uint16_t numFilled;
    uint16_t capacity;

    /*
     * An array containing IDs for cores in this list.
     */
    int* cores;
};

/*
 * An object which can be used as a CoreList by the Arachne runtime. It
 * exists to facilitate memory management, allowing CoreManagers to return
 * either a pointer to a CoreList or a CoreList.
 */
struct CoreListView {
    explicit CoreListView(int capacity)
        : isPointer(false), coreList(capacity) {}
    explicit CoreListView(CoreList* list)
        : isPointer(true), coreListPointer(list) {}
    ~CoreListView() {}

    // Both copy and move operators are defined because Arachne's use of
    // std::bind requires at least one copy.
    // If Arachne ever removes its dependency on std::bind, it's possible that
    // the copy assignment and constructor operators can be removed.
    CoreListView(CoreListView&& other) { *this = std::move(other); }
    CoreListView(const CoreListView& other) { *this = other; }
    CoreListView& operator=(CoreListView&& other) {
        isPointer = other.isPointer;
        if (isPointer) {
            coreListPointer = other.coreListPointer;
        } else {
            coreList = std::move(other.coreList);
        }
        return *this;
    }
    CoreListView& operator=(const CoreListView& other) {
        isPointer = other.isPointer;
        if (isPointer) {
            coreListPointer = other.coreListPointer;
        } else {
            coreList = other.coreList;
        }
        return *this;
    }

    uint32_t size() {
        return isPointer ? coreListPointer->size() : coreList.size();
    }
    int& operator[](std::size_t index) {
        return isPointer ? (*coreListPointer)[index] : coreList[index];
    }
    void add(int coreId) {
        if (isPointer)
            coreListPointer->add(coreId);
        else
            coreList.add(coreId);
    }

    bool isPointer;
    /*
     * An array containing IDs for cores in this list.
     */
    union {
        CoreList* coreListPointer;
        CoreList coreList;
    };
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
     * Invoked by Arachne when any core detects a core release request from
     * the Core Arbiter. It is responsible for eventually scheduling
     * releaseCore() onto the core most recently given by the CoreArbiter.
     */
    virtual void coreUnavailable() = 0;

    /**
     * Invoked by Arachne::createThread to get cores available for a particular
     * threadClass.
     */
    virtual CoreListView getCores(int threadClass) = 0;

    virtual ~CoreManager() {}
};

}  // namespace Arachne
#endif  // COREMANAGER_H_
