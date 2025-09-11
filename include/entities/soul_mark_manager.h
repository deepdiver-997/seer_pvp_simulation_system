#ifndef SOUL_MARK_MANAGER_H
#define SOUL_MARK_MANAGER_H

#include <iostream>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <functional>

#ifdef _WIN32
    #include <windows.h>
    #define DLIB_HANDLE HMODULE
    #define DLIB_OPEN(path) LoadLibraryA(path)
    #define DLIB_SYM(handle, name) GetProcAddress((HMODULE)(handle), (name))
    #define DLIB_CLOSE(handle) FreeLibrary((HMODULE)(handle))
    #define DLIB_ERROR() "Windows dynamic load error"
#else
    #include <dlfcn.h>
    #define DLIB_HANDLE void*
    #define DLIB_OPEN(path) dlopen((path), RTLD_LAZY)
    #define DLIB_SYM(handle, name) dlsym((handle), (name))
    #define DLIB_CLOSE(handle) dlclose((handle))
    #define DLIB_ERROR() dlerror()
#endif

#include <fsm/battleContext.h>
#include <entities/soul_mark.h>

// #define CLEAR_CACHE 1

#ifdef CLEAR_CACHE
#define EFFECT_TYPE std::shared_ptr<std::function<void(std::shared_ptr<BattleContext>)>>
#else
#define EFFECT_TYPE std::function<void(std::shared_ptr<BattleContext>)>
#endif

// using std::function<void(std::shared_ptr<BattleContext>)> = std::function<void(std::shared_ptr<BattleContext>)>;
//魂印管理器
class SoulMarkManager {
public:
    // 获取单例实例（线程安全）
    static SoulMarkManager& getInstance() {
        static SoulMarkManager instance; // C++11保证静态局部变量初始化线程安全
        return instance;
    }

    // 根据魂印ID获取效果函数指针（线程安全）
    EFFECT_TYPE getEffectFunc(int soulmarkId) {
        {
            std::shared_lock<std::shared_mutex> read_lock(cache_mutex_); // 读锁
            auto it = effect_cache_.find(soulmarkId);
            if (it != effect_cache_.end()) {
                #ifndef CLEAR_CACHE
                return it->second; // 缓存命中，直接返回
                #else
                if (auto spt = it->second.lock()) { // 尝试将weak_ptr提升为shared_ptr
                    return spt; // 缓存命中且有效，直接返回
                }
                #endif
            }
        }
        // 缓存未命中或已过期，需要加载动态库
        std::unique_lock<std::shared_mutex> write_lock(cache_mutex_); // 升级为写锁

        // 双重检查，防止在获取写锁期间，其他线程已经加载了该魂印
        auto it = effect_cache_.find(soulmarkId);
        if (it != effect_cache_.end()) {
            #ifdef CLEAR_CACHE
            if (auto spt = it->second.lock()) {
                return spt;
            }
            #endif
        }

        // 加载动态库并获取函数指针的逻辑（需要你实现）
        EFFECT_TYPE func_ptr = loadFromLibrary(soulmarkId);

        if (func_ptr) {
            // 将新加载的shared_ptr转换为weak_ptr存入缓存
            effect_cache_[soulmarkId] = func_ptr;
        }

        return func_ptr;
    }

    #ifdef CLEAR_CACHE
    // 清理缓存中已过期的函数指针（线程安全）
    void clearCache() {
        std::unique_lock<std::shared_mutex> write_lock(cache_mutex_); // 写锁
       for (auto it = effect_cache_.begin(); it != effect_cache_.end(); ) {
            if (it->second.expired()) {
                it = effect_cache_.erase(it); // 删除过期的缓存项
            } else {
                ++it;
            }
        }
    }
    #endif

private:
    SoulMarkManager() = default; // 私有构造函数
    ~SoulMarkManager(); // 私有析构函数

    #ifdef CLEAR_CACHE
    // 缓存映射表：魂印ID -> weak_ptr<效果函数>
    std::unordered_map<int, std::weak_ptr<std::function<void(std::shared_ptr<BattleContext>)>>> effect_cache_;
    #else
    // 缓存映射表：魂印ID -> 效果函数
    std::unordered_map<int, std::function<void(std::shared_ptr<BattleContext>)>> effect_cache_;
    #endif
    mutable std::shared_mutex cache_mutex_; // 用于保护缓存映射表的读写锁

    std::unordered_map<std::string, DLIB_HANDLE> library_handles_; // 动态库句柄缓存
    mutable std::shared_mutex library_mutex_; // 用于保护动态库句柄缓存的读写锁

    // 禁止拷贝
    SoulMarkManager(const SoulMarkManager&) = delete;
    SoulMarkManager& operator=(const SoulMarkManager&) = delete;

    std::string getLibraryPath(int soulmarkId) {
        // 根据魂印ID返回对应的动态库路径
        #ifndef _WIN32
        return "lib_soulmark_" + std::to_string(soulmarkId) + ".so"; // 示例路径
        #else
        return "soulmark_lib_" + std::to_string(soulmarkId) + ".dll"; 
        #endif
    }

    // 实际加载动态库的函数
    void* getLibraryHandle(int soulmarkId) {
        {
            // ... 你的动态库加载和符号查找逻辑 ...
            std::shared_lock<std::shared_mutex> read_lock(library_mutex_); // 读锁
            auto it = library_handles_.find(getLibraryPath(soulmarkId));
            if (it != library_handles_.end()) {
                return it->second; // 缓存命中，直接返回
            }
        }
        // 缓存未命中，需要加载动态库
        std::unique_lock<std::shared_mutex> write_lock(library_mutex_); // 写锁
        // 双重检查，防止在获取写锁期间，其他线程已经加载了该动态库
        auto it = library_handles_.find(getLibraryPath(soulmarkId));
        if (it != library_handles_.end()) {
            return it->second;
        }
        std::string lib_path = getLibraryPath(soulmarkId);
        DLIB_HANDLE handle = DLIB_OPEN(lib_path.c_str());
        if (!handle) {
            std::cerr << "Cannot load library: " << DLIB_ERROR() << '\n';
            return nullptr;
        }
        library_handles_[lib_path] = handle;
        return handle;
    }

    EFFECT_TYPE loadFromLibrary(int soulmarkId) {
        DLIB_HANDLE handle = getLibraryHandle(soulmarkId);
        if (!handle) {
            return nullptr;
        }
    #ifndef _WIN32
        dlerror(); // 清除之前的错误
    #endif
        std::string func_name = "soulmark_effect_" + std::to_string(soulmarkId);
        typedef void (*FuncType)(std::shared_ptr<BattleContext>);
        FuncType func = reinterpret_cast<FuncType>(DLIB_SYM(handle, func_name.c_str()));
        const char* dlsym_error =
        #ifdef _WIN32
            func ? nullptr : DLIB_ERROR();
        #else
            DLIB_ERROR();
        #endif
        if (!func || dlsym_error) {
            std::cerr << "Cannot load symbol '" << func_name << "': " << (dlsym_error ? dlsym_error : "null") << '\n';
            return nullptr;
        }
    #ifdef CLEAR_CACHE
        auto func_wrapper = std::make_shared<std::function<void(std::shared_ptr<BattleContext>)>>(
            std::function<void(std::shared_ptr<BattleContext>)>(func)
        );
        return func_wrapper;
    #else
        return func;
    #endif
    }
};

SoulMarkManager::~SoulMarkManager() {
    // 关闭所有打开的动态库
    for (auto& pair : library_handles_) {
        DLIB_CLOSE(pair.second);
    }
    library_handles_.clear();
    effect_cache_.clear();
}


#endif // SOUL_MARK_MANAGER_H