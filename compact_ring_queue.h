/**
 * @file common_compact_ring_queue.h
 * @brief 一个通用队列, 可支持一写一读、一写多读、多写一读、多写多读, 相对于普通的队列, 有两个明显的优点:
 * 1.在支持变长的条件下可以 inplacement new, 减少内存分配、拷贝次数
 * 2."原子" Push, 如果在 Push 过程中, 程序退出, 队列不会乱掉
 * @author ape-bless
 * @version 1.0.0
 * @date 2022-09-27 22:54
 */
#ifndef __COMPACT_RING_QUEUE_H__
#define __COMPACT_RING_QUEUE_H__

#include <stdint.h>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <mutex>
#include <thread>
#include <string.h>
#include <atomic>
//#include <iostream>

#define barrier() __asm__ __volatile__("mfence" ::: "memory");

// H、E 的最小实现(必须有此处出现的成员变量、函数)
struct CompactQueueHeader {
    uint64_t head_;
    uint64_t tail_;
    uint64_t incomplete_num_;
    void Increase(uint32_t inc) __attribute__((always_inline))
    {
        tail_ += inc;
    }

    void Decrease(uint32_t inc) __attribute__((always_inline))
    {
        head_ += inc;
    }
};

struct CompactQueueElement {
    uint32_t len;
    // 利用 complete 标志位降低锁的粒度
    uint8_t complete;
    char buff[];
    CompactQueueElement() : len(0), complete(0)
    {
    }
    void CompletePush(va_list &args) __attribute__((always_inline))
    {
        // 必须保证 complete 在所有操作完成后才执行
        barrier();
        complete = 1;
    }
};

/**
 * @brief 通用队列
 *
 * @tparam H : 队列头
 * @tparam E : 存储元素
 */
template<class H = CompactQueueHeader, class E = CompactQueueElement>
struct CommonCompactRingQueue {
    CommonCompactRingQueue() : buff_len_(0), safe_len_(0), circle_len_(0), max_element_(0), element_count_(0), buff_(nullptr), desc_(nullptr), multi_w_(true), multi_r_(true)
    {
    }

    ~CommonCompactRingQueue()
    {
    }

    /**
     * @brief Init
     *
     * @param mem_addr 内存地址
     * @param mem_len 内存长度
     * @param max_data_len 每次 Push 的最大长度
     * @param multi_w 是否多线程 Push
     * @param multi_r 是否多线程 Pop
     *
     * @return 清除的 uncomplete 的 Element 数量
     */
    int32_t Init(void *mem_addr, uint32_t mem_len, uint32_t max_data_len, bool multi_w, bool multi_r)
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

        desc_ = new (mem_addr)H;
        buff_ = (char *)mem_addr + sizeof(H);
        multi_w_ = multi_w;
        multi_r_ = multi_r;

        desc_->incomplete_num_ = RemoveIncomplete();
        return desc_->incomplete_num_;
    }

    /**
     * @brief Push 每次 Push 成功, 需要对返回值进行一次 complete_push 操作
     *
     * @param src
     * @param len
     *
     * @return
     */
    E *Push(const char *src, uint32_t len, ...)
    {
        if (!src || len > max_element_) {
            return nullptr;
        }
        E *e = nullptr;
        char *cur = nullptr;
        uint32_t inc = sizeof(E) + ( (len + kAlignofE - 1) / kAlignofE ) * kAlignofE;
        if (multi_w_) {
            std::unique_lock<std::mutex> lock(buff_mutex_);
            if (__builtin_expect(!!(full()), 0)) {
                push_condition_.wait(lock, [this] { return !this->full(); });
            }
            cur = buff_ + (desc_->tail_ % circle_len_);
            //std::cout << "from push|" << desc_->head_ << "|" << desc_->tail_ << "|" << len << "|" << inc << "|" << (cur - buff_) << std::endl;
            e = new (cur)E;
            barrier();
            desc_->Increase(inc);
            lock.unlock();
            e->len = len;
            memcpy(e->buff, src, len);
        } else {
            if (__builtin_expect(!!(full()), 0)) {
                std::unique_lock<std::mutex> lock(buff_mutex_);
                push_condition_.wait(lock, [this] { return !this->full(); });
            }
            char *cur = buff_ + (desc_->tail_ % circle_len_);
            e = new (cur)E;
            barrier();
            desc_->Increase(inc);
            memcpy(e->buff, src, len);
            e->len = len;
        }
        va_list arg;
        va_start(arg, len);
        e->CompletePush(arg);
        va_end(arg);
        ++element_count_;
        return e;
    }

    /**
     * @brief Pop 可多线程调用,单线程推荐调用 E *Pop() 函数,性能更高
     *
     * @param dest 内存需要自己分配
     * @param len
     *
     * @return -1:分配内存空间不足 0. 无可读数据 其他:数据长度
     */
    int32_t Pop(void *dest, size_t len)
    {
        std::unique_lock<std::mutex> lock(buff_mutex_);
        if (!empty()) {
            E *e = (E *)(buff_ + desc_->head_ % circle_len_);
            //std::cout << "begin pop|" << desc_->head_ << "|" << desc_->tail_ << "|" << e->len << "|" << (uint32_t)e->complete << "|" << ((char *)e - buff_) << std::endl;
            if (e->complete == 1) {
                if (e->len > len) {
                    return -1;
                }
                uint32_t l = e->len;
                uint32_t inc = sizeof(E) + ( (e->len + kAlignofE - 1) / kAlignofE ) * kAlignofE;
                memcpy(dest, e->buff, e->len);
                desc_->Decrease(inc);
                --element_count_;
                //std::cout << "from pop|" << desc_->head_ << "|" << desc_->tail_ << "|" << l << "|" << inc << "|" << ((char *)e - buff_) << std::endl;
                lock.unlock();
                push_condition_.notify_one();
                return l;
            }
        }
        return 0;
    }

    // 只有一个线程读队列时调用
    E *Pop()
    {
        assert(!multi_r_);
        if (!empty()) {
            E *e = (E *)(buff_ + desc_->head_ % circle_len_);
            if (e->complete == 1) {
                uint32_t inc = sizeof(E) + ( (e->len + kAlignofE - 1) / kAlignofE ) * kAlignofE;
                desc_->Decrease(inc);
                --element_count_;
                push_condition_.notify_one();
                return e;
            }
        }
        return nullptr;
    }

    int32_t RemoveIncomplete()
    {
        element_count_ = 0;
        uint64_t offset = desc_->head_;
        E *e = nullptr;
        uint32_t inc = 0;
        while (offset < desc_->tail_) {
            e = (E *)(buff_ + offset % circle_len_);
            if (e->complete != 1) {
                desc_->tail_ = offset;
                return 1;
            }
            ++element_count_;
            inc = sizeof(E) + ( (e->len + kAlignofE - 1) / kAlignofE ) * kAlignofE;
            offset += inc;
        }
        return 0;
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

    int size()
    {
        return element_count_;
    }

    uint32_t buff_len_;                 // 队列总长度
    uint32_t safe_len_;                 // 安全长度 = buff_len_ - 2 * max_element, 达到该长度认为队列已满不再写入
    uint32_t circle_len_;               // 队列循环长度 = buff_len_ - max_element
    uint32_t max_element_;
    std::atomic_int element_count_;     // 元素数量
    char *buff_;
    H   *desc_;                         // 队列头, 存储头、尾指针等信息
    bool multi_w_;
    bool multi_r_;

    std::mutex buff_mutex_;
    std::condition_variable push_condition_;
    static const int32_t kAlignofE = __alignof__(E);
};

#endif // __COMPACT_RING_QUEUE_H__
