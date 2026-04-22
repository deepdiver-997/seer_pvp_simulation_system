#ifndef G_ERRORS_H
#define G_ERRORS_H

#include <exception>
#include <stdexcept>
#include <string>

class GError : public std::exception {
    std::string message;
    public:
        GError(const std::string& message) : message(message) {}
        GError(const char* message) : message(message) {}
        const char* what() const noexcept override {
            return message.c_str();
        }
        ~GError() = default;
};

#endif