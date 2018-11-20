/* Copyright (c) 2015-2018 Stanford University
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

#include "Logger.h"
#include <execinfo.h>
#include "PerfUtils/Cycles.h"

using PerfUtils::Cycles;

namespace Arachne {

extern FILE* errorStream;
LogLevel Logger::displayMinLevel = DEBUG;
std::mutex Logger::mutex;
PerfUtils::Initialize Logger::_(Logger::init);
uint64_t Logger::startingTsc;

/**
  * Friendly names for each #LogLevel value.
  * Keep this in sync with the LogLevel enum.
  */
static const char* logLevelNames[] = {"VERBOSE", "DEBUG", "NOTICE", "WARNING",
                                      "ERROR", "SILENT"};


void
Logger::init()
{
startingTsc = Cycles::rdtsc();	
}
void
Logger::log(LogLevel level, const char* fmt, ...) {
    if (level < displayMinLevel) {
        return;
    }

    // First format the message; timestamps are in seconds since the first log
#define MAX_MESSAGE_CHARS 2000
    // Construct a message on the stack and then print it out with the lock
    char buffer[MAX_MESSAGE_CHARS];
    int spaceLeft = MAX_MESSAGE_CHARS;
    int charsWritten = 0;
    int actual;
    double time = Cycles::toSeconds(Cycles::rdtsc() - startingTsc);

    // Add a header including rdtsc time and location in the file.
    actual = snprintf(buffer + charsWritten, spaceLeft,
                      "%.10lf: %s: ", time, logLevelNames[level]);
    charsWritten += actual;
    spaceLeft -= actual;

    // Add the intended message
    va_list args;
    va_start(args, fmt);
    actual = vsnprintf(buffer + charsWritten, spaceLeft, fmt, args);
    va_end(args);

    Lock lock(mutex);
    fprintf(errorStream, "%s\n", buffer);
    fflush(errorStream);
    va_end(args);
}

void
Logger::logBacktrace(LogLevel level) {
    // No lock needed: doesn't access Logger object.
    const int maxFrames = 128;
    void* retAddrs[maxFrames];
    int frames = backtrace(retAddrs, maxFrames);
    char** symbols = backtrace_symbols(retAddrs, frames);
    if (symbols == NULL) {
        // If the malloc failed we might be able to get the backtrace out
        // to stderr still.
        backtrace_symbols_fd(retAddrs, frames, 2);
        return;
    }
    log(level, "Backtrace (Not Optimization-Resistant):\n");
    for (int i = 1; i < frames; ++i)
        log(level, "%s\n", symbols[i]);

    log(level, "Pretty Backtrace (Not Optimization-Resistant):\n");
    for (int i = 1; i < frames; ++i) {
        // Find the first occurence of '(' or ' ' in message[i] and assume
        // everything before that is the file name.
        int p = 0;
        while (symbols[i][p] != '(' && symbols[i][p] != ' ' &&
               symbols[i][p] != 0)
            ++p;
        char syscom[256];
        snprintf(syscom, sizeof(syscom), "addr2line %p -e %.*s", retAddrs[i], p,
                 symbols[i]);
        int ret;
        ret = system(syscom);
        if (ret == -1) {
            log(level,
                "Child process could not be created, or its status could not "
                "be retrieved\n");
        }
    }
    free(symbols);
}

}  // namespace Arachne
