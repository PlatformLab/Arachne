/* Copyright (c) 2015-2017 Stanford University
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

#ifndef CORE_ARBITER_LOGGER_H
#define CORE_ARBITER_LOGGER_H

#include <stdarg.h>
#include <stdint.h>
#include <mutex>

#define LOG Logger::log

namespace Arachne {

/**
 * Log levels from most to least inclusive.
 */
enum LogLevel { DEBUG, NOTICE, WARNING, ERROR, SILENT };

class Logger {
  public:

    /**
     * Used to set the minimum severity to print out.
     */
    static void setLogLevel(LogLevel level) {
        displayMinLevel = level;
    }

    /**
     * Print a message to the console at a given severity level. Accepts
     * printf-style format strings.
     *
     * \param level
     *     The severity level of this message.
     * \param fmt
     *     A format string, followed by its arguments.
     */
    static void log(LogLevel level, const char* fmt, ...)
        __attribute__((format (printf, 2, 3)));

  private:
    // The minimum severity level to print.
    static LogLevel displayMinLevel;

    // Lock around printing since Arachne is multithreaded.
    typedef std::unique_lock<std::mutex> Lock;
    static std::mutex mutex;
};

}

#endif
