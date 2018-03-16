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
#include "Arachne.h"

namespace Arachne {

extern FILE* errorStream;
LogLevel Logger::displayMinLevel = NOTICE;
std::mutex Logger::mutex;

void
Logger::log(LogLevel level, const char* fmt, ...) {
    if (level < displayMinLevel) {
        return;
    }

    Lock lock(mutex);
    va_list args;
    va_start(args, fmt);
    vfprintf(errorStream, fmt, args);
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
