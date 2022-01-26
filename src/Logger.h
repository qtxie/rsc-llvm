#ifndef RED_SYSTEM_LOGGER_H
#define RED_SYSTEM_LOGGER_H

#include <source_location>
#include <llvm/Support/raw_ostream.h>

constexpr int verbose = 1;

class Logger {
public:
    template <typename T>
    Logger& operator<<(const T& s) {
    if constexpr (verbose > 0) {
        llvm::errs() << s;
        return *this;
    } else {
        return *this;
    }}
    Logger& operator()(const std::source_location location = std::source_location::current()) {
        if constexpr (verbose > 0) {
        llvm::errs() << '<' << location.function_name() << '>' << ": ";
        }
        return *this;
    }
};

#endif /* RED_SYSTEM_LOGGER_H */
