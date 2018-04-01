/* Copyright (c) 2017-2018 Stanford University
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

#ifndef COREPOLICY_H_
#define COREPOLICY_H_

#include <string.h>
#include <atomic>
#include "Logger.h"

namespace Arachne {
/**
 * Implementors of this interface specify the allocation and use of cores in
 * Arachne.
 */
class CorePolicy {
  public:
    /*
     * An unordered list of cores. This class's thread-safety is dependent on
     * the thread-safety of torn reads. In particular, we expect that an
     * unsynchronized read of numFilled will return either a former value or the
     * current value, but not a mixture of bits from different values.
     * Transiently reading a former value larger than the current value may
     * cause scheduling to the wrong core, but this race already exists when
     * cores move between lists.
     */
    struct CoreList {
        // Constructor
        explicit CoreList(int capacity, bool mustFree = false)
            : capacity(static_cast<uint16_t>(capacity)), mustFree(mustFree) {
            cores = new int[capacity];
            numFilled = 0;
        }
        /**
         * Destructor frees memory depending on the value of mustFree.
         */
        ~CoreList() {
            if (mustFree)
                delete[] cores;
        }
        // Copy constructor
        CoreList(const CoreList& other) { *this = other; }

        // Copy assignment will allocate new memory iff the source object
        // requires memory to be freed.
        CoreList& operator=(const CoreList& other) {
            numFilled = other.numFilled;
            capacity = other.capacity;
            mustFree = other.mustFree;
            if (mustFree) {
                // If the destructor releases memory, then the new core list
                // must be a deep copy.
                cores = new int[capacity];
                memcpy(cores, other.cores, numFilled * sizeof(cores[0]));
            } else {
                // If the destructor does not release memory, then the core list
                // can share memory with the old.
                cores = other.cores;
            }
            return *this;
        }

        // Get the current number of elements in this CoreList.
        uint16_t size() const { return numFilled; }

        // Get the maximum number of elements in this CoreList.
        uint32_t getCapacity() const { return capacity; }

        // Insert the Core Id at the back of this list.
        void add(int coreId) {
            if (numFilled >= capacity) {
                ARACHNE_LOG(
                    ERROR,
                    "Failed to add core %d; numFilled = %u, capacity = %u",
                    coreId, numFilled, capacity);
                abort();
            }
            this->cores[numFilled] = coreId;
            this->numFilled++;
        }

        // Return the index of the given coreId, or -1 if it is not in the
        // current list.
        int find(int coreId) const {
            for (int i = 0; i < numFilled; i++)
                if (cores[i] == coreId)
                    return i;
            return -1;
        }

        // Remove the Core Id at the given index in the list.
        void remove(int index) {
            if (index >= numFilled) {
                ARACHNE_LOG(WARNING,
                            "Failed to remove core; index = %d, numFilled = %u",
                            index, numFilled);
                return;
            }
            cores[index] = cores[numFilled - 1];
            numFilled--;
        }

        int& operator[](std::size_t index) const { return cores[index]; }
        int& get(std::size_t index) const { return cores[index]; }

      private:
        /* The number of cores in the list */
        uint16_t numFilled;

        /* The maximum number of cores this list is able to hold */
        uint16_t capacity;

        /*
         * An array containing IDs for cores in this list. If mustFree is false,
         * memory is allocated in the constructor and never de-allocated.
         * Otherwise, memory is allocated in the constructor and copy
         * constructor, and deallocated in the destructor.
         */
        int* cores;

        /*
         * This variable indicates whether the memory for this CoreList should
         * be released.
         */
        bool mustFree;
    };

    /**
     * Invoked by an Arachne kernel thread after it wakes up on a dedicated
     * core and sets up state to run the Arachne scheduler.
     *
     * \param myCoreId
     *     The identifier for the core that is invoking this method.
     */
    virtual void coreAvailable(int myCoreId) = 0;

    /**
     * This method is called to let CorePolicy know that the given core is
     * about to be returned to CoreArbiter.
     */
    virtual void coreUnavailable(int coreId) = 0;

    /**
     * This method is invoked to determine where a new thread will be placed.
     * The return value indicates one or more cores on which the thread may be
     * placed.  Arachne will choose a lightly loaded core among these. It
     * should return an empty CorePolicy::CoreList if an invalid threadClass
     * is passed in.
     */
    virtual CoreList getCores(int threadClass) = 0;

    virtual ~CorePolicy() {}
};

}  // namespace Arachne
#endif  // COREPOLICY_H_
