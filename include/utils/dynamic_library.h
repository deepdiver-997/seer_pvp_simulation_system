#ifndef DYNAMIC_LIBRARY_H
#define DYNAMIC_LIBRARY_H

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

// Platform-specific dynamic library loading macros
#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_DL_EXT ".dll"
    #define PLATFORM_PATH_SEP "\\"

    #include <windows.h>

    using DLHandle = HMODULE;
    using DLSymbol = FARPROC;

    #define DL_OPEN(path) LoadLibraryA(path)
    #define DL_CLOSE(handle) FreeLibrary(handle)
    #define DL_SYM(handle, name) GetProcAddress(handle, name)
    #define DL_ERROR() std::string(GetLastError() ? "LoadLibrary failed" : "GetProcAddress failed")

#else
    #define PLATFORM_DL_EXT ".so"
    #define PLATFORM_PATH_SEP "/"

    #include <dlfcn.h>

    using DLHandle = void*;
    using DLSymbol = void*;

    #define DL_OPEN(path) dlopen(path, RTLD_NOW)
    #define DL_CLOSE(handle) dlclose(handle)
    #define DL_SYM(handle, name) dlsym(handle, name)
    #define DL_ERROR() std::string(dlerror() ? dlerror() : "dlsym failed")

#endif

// Forward declarations for plugin registration
class EffectFactory;
class SoulMarkManager;

// Dynamic library wrapper class
class DynamicLibrary {
public:
    explicit DynamicLibrary(const std::string& path);
    ~DynamicLibrary();

    // Prevent copying
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    // Allow moving
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

    bool isLoaded() const { return handle_ != nullptr; }
    const std::string& path() const { return path_; }

    // Get a symbol from the library
    DLSymbol getSymbol(const char* name);

    // Check if a symbol exists
    bool hasSymbol(const char* name);

    // Get last error message
    const std::string& error() const { return error_; }

private:
    std::string path_;
    DLHandle handle_;
    std::string error_;
};

// Dynamic library manager - scans directories and loads plugins
class DynamicLibraryLoader {
public:
    DynamicLibraryLoader() = default;

    // Load all dynamic libraries from a directory
    // extension: file extension to filter (e.g., ".so", ".dll", ".dylib")
    // Returns number of successfully loaded libraries
    int loadFromDirectory(const std::string& dir_path,
                          const std::string& extension = PLATFORM_DL_EXT);

    // Get all loaded libraries
    const std::vector<DynamicLibrary>& libraries() const { return libraries_; }

    // Find a symbol in all loaded libraries
    // Returns the first library that contains the symbol, or nullptr if not found
    DynamicLibrary* findLibraryWithSymbol(const char* symbol_name);

    // Call an init function from a library
    template<typename InitFn>
    bool callInitFunction(const std::string& lib_path, const char* fn_name, InitFn fn) {
        for (auto& lib : libraries_) {
            if (lib.path() == lib_path || lib.path().find(lib_path) != std::string::npos) {
                DLSymbol symbol = lib.getSymbol(fn_name);
                if (symbol) {
                    auto init_fn = reinterpret_cast<InitFn>(symbol);
                    init_fn();
                    return true;
                }
            }
        }
        return false;
    }

private:
    std::vector<DynamicLibrary> libraries_;
    std::unordered_map<std::string, DynamicLibrary*> symbol_to_library_;
};

#endif // DYNAMIC_LIBRARY_H