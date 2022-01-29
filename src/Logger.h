#ifndef RED_SYSTEM_LOGGER_H
#define RED_SYSTEM_LOGGER_H

#include <source_location>
#include <llvm/Support/raw_ostream.h>

constexpr int verbose = 1;

class Logger {
private:
    int i = 0;
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
        int n = i;
        while (n--) llvm::errs() << "  ";
        llvm::errs() << '<' << location.function_name() << '>' << ": ";
        }
        return *this;
    }
    void indent() {if constexpr (verbose > 0) {i++;}}
    void unindent() {if constexpr (verbose > 0) {i--;}}
};

#endif /* RED_SYSTEM_LOGGER_H */
