#ifndef MAIL_SYSTEM_BOOST_THREAD_POOL_H
#define MAIL_SYSTEM_BOOST_THREAD_POOL_H

#include "thread_pool_base.h"
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/bind.hpp>

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <type_traits>

// namespace mail_system {

/**
 * @brief 基于Boost的线程池实现
 * 
 * 这个类使用Boost.Asio的thread_pool来实现线程池功能，
 * 适用于处理CPU密集型或阻塞性任务。
 */
class BoostThreadPool : public ThreadPoolBase {
public:
    /**
     * @brief 构造函数
     * 
     * @param thread_count 线程数量，默认为系统硬件并发数
     */
    explicit BoostThreadPool(size_t thread_count = std::thread::hardware_concurrency())
        : m_thread_count(thread_count), m_running(false) {
    }

    /**
     * @brief 析构函数
     * 
     * 确保线程池在销毁前停止
     */
    ~BoostThreadPool() override {
        stop(true);
    }

    /**
     * @brief 启动线程池
     */
    void start() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) {
            m_running = true;
            std::cout << "Starting BoostThreadPool..." << std::endl;
            m_pool = std::make_unique<boost::asio::thread_pool>(m_thread_count);
        }
    }

    /**
     * @brief 停止线程池
     * 
     * @param wait_for_tasks 是否等待所有任务完成
     */
    void stop(bool wait_for_tasks = true) override {
        std::unique_ptr<boost::asio::thread_pool> pool;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running) return;
            m_running = false;
            pool = std::move(m_pool);
        }

        if (pool) {
            if (wait_for_tasks) {
                pool->wait();
            }
            pool->stop();
            std::cout << "Stopped BoostThreadPool" << std::endl;
            pool->join();
        }
    }

    /**
     * @brief 获取线程池中的线程数量
     * 
     * @return size_t 线程数量
     */
    size_t thread_count() const override {
        return m_thread_count;
    }

    /**
     * @brief 检查线程池是否正在运行
     * 
     * @return true 如果线程池正在运行
     * @return false 如果线程池已停止
     */
    bool is_running() const override {
        return m_running.load();
    }

protected:
    /**
     * @brief 提交任务的实现
     * 
     * @tparam F 任务函数类型
     * @tparam Args 任务函数参数类型
     * @param f 任务函数
     * @param args 任务函数参数
      * @return std::future<std::invoke_result_t<F, Args...>> 任务结果的future
     */
    template<class F, class... Args>
    auto submit_impl(F&& f, Args&&... args) 
          -> std::future<std::invoke_result_t<F, Args...>> {
          using return_type = std::invoke_result_t<F, Args...>;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) {
            throw std::runtime_error("Thread pool is not running");
        }

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> result = task->get_future();
        boost::asio::post(*m_pool, [task]() { (*task)(); });
        return result;
    }

    /**
     * @brief 提交任务的实现（无返回值版本）
     * 
     * @param f 任务函数
     */
    void post_impl(std::function<void()> f) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) {
            throw std::runtime_error("Thread pool is not running");
        }
        boost::asio::post(*m_pool, f);
    }

private:
    size_t m_thread_count;                          ///< 线程数量
    std::unique_ptr<boost::asio::thread_pool> m_pool; ///< Boost线程池
    std::atomic<bool> m_running;                                 ///< 线程池是否运行中
    std::mutex m_mutex;                             ///< 互斥锁，保护线程池状态
};

// } // namespace mail_system

#endif // MAIL_SYSTEM_BOOST_THREAD_POOL_H