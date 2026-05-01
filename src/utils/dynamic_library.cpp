#include <utils/dynamic_library.h>

#include <dirent.h>
#include <cstring>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

DynamicLibrary::DynamicLibrary(const std::string& path)
    : path_(path), handle_(nullptr) {
    handle_ = DL_OPEN(path.c_str());
    if (!handle_) {
        error_ = DL_ERROR();
    }
}

DynamicLibrary::~DynamicLibrary() {
    if (handle_) {
        DL_CLOSE(handle_);
        handle_ = nullptr;
    }
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : path_(std::move(other.path_)),
      handle_(other.handle_),
      error_(std::move(other.error_)) {
    other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            DL_CLOSE(handle_);
        }
        path_ = std::move(other.path_);
        handle_ = other.handle_;
        error_ = std::move(other.error_);
        other.handle_ = nullptr;
    }
    return *this;
}

DLSymbol DynamicLibrary::getSymbol(const char* name) {
    if (!handle_) {
        error_ = "Library not loaded";
        return nullptr;
    }
    DLSymbol symbol = DL_SYM(handle_, name);
    if (!symbol) {
        error_ = DL_ERROR();
    }
    return symbol;
}

bool DynamicLibrary::hasSymbol(const char* name) {
    return getSymbol(name) != nullptr;
}

int DynamicLibraryLoader::loadFromDirectory(const std::string& dir_path,
                                            const std::string& extension) {
    int loaded_count = 0;

#if defined(_WIN32) || defined(_WIN64)
    WIN32_FIND_DATAA find_data;
    std::string search_path = dir_path + "\\*" + extension;
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string filename = find_data.cFileName;
            if (filename != "." && filename != "..") {
                std::string full_path = dir_path + "\\" + filename;
                DynamicLibrary lib(full_path);
                if (lib.isLoaded()) {
                    libraries_.push_back(std::move(lib));
                    loaded_count++;
                }
            }
        } while (FindNextFileA(hFind, &find_data));
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(dir_path.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            if (filename == "." || filename == "..") {
                continue;
            }
            // Check if file has the desired extension
            if (extension.empty() ||
                (filename.size() >= extension.size() &&
                 filename.compare(filename.size() - extension.size(),
                                  extension.size(), extension) == 0)) {
                std::string full_path = dir_path + "/" + filename;
                DynamicLibrary lib(full_path);
                if (lib.isLoaded()) {
                    libraries_.push_back(std::move(lib));
                    loaded_count++;
                }
            }
        }
        closedir(dir);
    }
#endif

    return loaded_count;
}

DynamicLibrary* DynamicLibraryLoader::findLibraryWithSymbol(const char* symbol_name) {
    // Check cache first
    auto it = symbol_to_library_.find(symbol_name);
    if (it != symbol_to_library_.end()) {
        return it->second;
    }

    // Search all libraries
    for (auto& lib : libraries_) {
        if (lib.hasSymbol(symbol_name)) {
            DynamicLibrary* ptr = &lib;
            symbol_to_library_[symbol_name] = ptr;
            return ptr;
        }
    }

    return nullptr;
}