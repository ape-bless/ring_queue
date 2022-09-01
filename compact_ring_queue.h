/**
 * @file compact_ring_queue.h
 * @brief 一个高效、内存利用率高的通用支持多写单读的 FIFO 队列
 * @author ape-bless
 * @version 1.0.0
 * @date 2022-08-23 22:02
 */
#ifndef __COMPACT_RING_QUEUE_H__
#define __COMPACT_RING_QUEUE_H__

#include <stdint.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <string.h>
#include <iostream>

#define barrier() __asm__ __volatile__("mfence" ::: "memory");

struct CompactQueueHeader {
    uint64_t head_;
    uint64_t tail_;
    uint64_t incomplete_num_;
};

struct CompactQueueElement {
    uint32_t len;
    // 利用 complete 标志位降低锁的粒度
    uint8_t complete;
    char buff[];
    CompactQueueElement() : len(0), complete(0)
    {
    }
};

/**
 * @brief 循环队列
 *
 * @tparam H : 队列头
 * @tparam E : 存储元素
 */
template<typename H, typename E>
struct CompactRingQueue {
    CompactRingQueue() : buff_len_(0), safe_len_(0), circle_len_(0), overload_len_(0), max_element_(0), desc_(nullptr), buff_(nullptr)
    {
    }

    ~CompactRingQueue()
    {
    }

    /**
     * @brief Init
     *
     * @param mem_addr 内存地址
     * @param mem_len 内存长度
     * @param max_data_len 每次 Push 的最大长度
     *
     * @return 清除的 uncomplete 的 Element 数量
     */
    int32_t Init(void *mem_addr, uint32_t mem_len, uint32_t max_data_len)
    {
        if (!mem_addr) {
            return -1;
        }
        uint32_t max_element = sizeof(E) + max_data_len;
        if (mem_len < (sizeof(H) + max_element * 2)) {
            return -2;
        }
        max_element_ = max_element;
        buff_len_ = mem_len - sizeof(H);
        safe_len_ = buff_len_ - 2 * max_element;
        circle_len_ = buff_len_ - max_element;
        overload_len_ = buff_len_ * 3 / 4;

        desc_ = new (mem_addr)H;
        buff_ = (char *)mem_addr + sizeof(H);
        desc_->incomplete_num_ = RemoveIncomplete();
        return desc_->incomplete_num_;
    }

    E *Push(char *src, uint32_t len)
    {
        if (!src || len > max_element_) {
            return nullptr;
        }
        if (__builtin_expect(!!(!full()), 0)) {
            uint32_t inc = sizeof(E) + ( (len + kAlignofE - 1) / kAlignofE ) * kAlignofE;
            E *e = nullptr;
            {
#ifdef MULTI_THREAD_PUSH_QUEUE
                std::unique_lock<std::mutex> lock(buff_mutex_);
#endif
                char *cur = buff_ + (desc_->tail_ % circle_len_);
                e = new (cur)E;
                desc_->tail_ += inc;
            }
            memcpy(e->buff, src, len);
            e->len = len;

            // 必须保证 complete 在所有操作完成后才执行
            barrier();
            e->complete = 1;
            return e;
        }
        return nullptr;
    }

    E *Pop()
    {
#ifndef MULTI_THREAD_POP_QUEUE
        if (!empty()) {
            E *e = (E *)(buff_ + desc_->head_ % circle_len_);
            if (e->complete) {
                uint32_t inc = sizeof(E) + ( (e->len + kAlignofE - 1) / kAlignofE ) * kAlignofE;
                desc_->head_ += inc;
                return e;
            }
        }
#else
        {
            // 对于 buff_ 来说只有一个线程来读, head_ 也只会在这一个线程修改, 可以放弃加锁, 性能提升显著, 对于多写多读的场景需要加锁
            std::unique_lock<std::mutex> lock(buff_mutex_);
            if ( !pop_condition_.wait_for(lock, std::chrono::milliseconds(200), [this] { return !this->Empty(); }) ) {
                return nullptr;
            }
            E *e = (NarutoLogEvent *)(buff_ + desc_->head_ % LOG_BUFF_CIRCLE_LEN);
            uint32_t inc = sizeof(NarutoLogEvent) + ( (e->len + kAlignofEvent - 1) / kAlignofEvent ) * kAlignofEvent;
            desc_->head_ += inc;
        }
        push_condition_.notify_one();
        return e;
#endif
        return nullptr;
    }

    int32_t RemoveIncomplete()
    {
        uint64_t offset = desc_->head_;
        E *e = nullptr;
        uint32_t inc = 0;
        while (offset < desc_->tail_) {
            e = (E *)(buff_ + offset % circle_len_);
            if (!e->complete) {
                break;
            }
            inc = sizeof(E) + ( (e->len + kAlignofE - 1) / kAlignofE ) * kAlignofE;
            offset += inc;
        }
        uint64_t x = offset;
        int32_t num = 0;
        while (x < desc_->tail_) {
            e = (E *)(buff_ + x % circle_len_);
            inc = sizeof(E) + ( (e->len + kAlignofE - 1) / kAlignofE ) * kAlignofE;
            x += inc;
            ++num;
        }
        if (offset < desc_->tail_) {
            desc_->tail_ = offset;
        }
        return num;
    }

    H *desc()
    {
        return desc_;
    }

    bool full()
    {
        return (desc_->head_ + safe_len_) <= desc_->tail_;
    }

    bool empty()
    {
        return desc_->head_ >= desc_->tail_;
    }

    uint32_t buff_len_;         // 队列总长度
    uint32_t safe_len_;         // 安全长度 = buff_len_ - 2 * max_element, 达到该长度认为队列已满不再写入
    uint32_t circle_len_;       // 队列循环长度 = buff_len_ - max_element
    uint32_t overload_len_;     // 队列过载长度 = buff_len_ * 0.75
    uint32_t max_element_;

    H   *desc_;                 // 队列头, 存储头、尾指针等信息
    char *buff_;
    static const int32_t kAlignofE = __alignof__(E);

#ifdef MULTI_THREAD_PUSH_QUEUE
    std::mutex buff_mutex_;
#endif

    // 多写单读, 可以去掉条件变量, 性能提升2-4倍
#ifdef MULTI_THREAD_POP_QUEUE
    std::condition_variable push_condition_;
    std::condition_variable pop_condition_;
#endif
};

template<typename H, typename E>
class BasalRingQueueWorker
{
public:
    BasalRingQueueWorker() : running_(false) {}
    ~BasalRingQueueWorker() {}

    int32_t Init(char *mem_addr, uint32_t mem_len, uint32_t max_data_len)
    {
        int ret = ring_queue_.Init(mem_addr, mem_len, max_data_len);
        if (ret < 0) {
            return ret;
        }
        running_.store(true, std::memory_order_release);
        worker_ = std::thread(&BasalRingQueueWorker<H, E>::Run, this);
        pthread_setname_np(worker_.native_handle(), "ring_queue_worker");
        return 0;
    }

    int32_t Exit()
    {
        if (!running_.load(std::memory_order_relaxed)) {
            return 0;
        }
        running_.store(false, std::memory_order_release);
        worker_.join();
        return 0;
    }

    E *Pop()
    {
        return ring_queue_.Pop();
    }

    E *Push(char *src, uint32_t len)
    {
        return ring_queue_.Push(src, len);
    }

    void Executor(E *e)
    {
        static uint32_t num = 0;
        std::cout << ++num << "\t" << e->buff << std::endl;
        //printf("%d\t%s\n", ++num, e->buff);
    }

    void Run()
    {
        while (running_.load(std::memory_order_acquire)) {
            E *e = ring_queue_.Pop();
            if (e) {
                Executor(e);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        E *p = ring_queue_.Pop();
        while (p) {
            Executor(p);
            p = ring_queue_.Pop();
        }
    }

protected:
    CompactRingQueue<H, E> ring_queue_;
    std::thread worker_;
    std::atomic_bool running_;
};

#endif // __COMPACT_RING_QUEUE_H__
