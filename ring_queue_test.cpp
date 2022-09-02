#include "compact_ring_queue.h"
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>

static thread_local int32_t log_thread_id = syscall(SYS_gettid);
const uint32_t kMemLen = 4 * 1024 * 1024;
BasalRingQueueWorker<CompactQueueHeader, CompactQueueElement> queue_worker;

void PushFunc()
{
    for (uint32_t msg_id = 0; msg_id < 10000; ++msg_id) {
        char buff[256];
        struct timeval t;
        gettimeofday(&t, nullptr);
        struct tm now;
        localtime_r(&t.tv_sec, &now);

        int header_len = snprintf(buff, sizeof(buff), "%04d-%02d-%02d %02d:%02d:%02d.%06ld|%d|this is test msg %u",
                                  1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, t.tv_usec, log_thread_id,
                                  msg_id);
        buff[header_len] = 0;
        queue_worker.Push(buff, header_len);
    }
}

int main()
{
    void *mem_addr = malloc(kMemLen);
    queue_worker.Init((char *)mem_addr, kMemLen, 256);
    sleep(1);
    std::thread t1(PushFunc);
#ifdef DMULTI_THREAD_PUSH_QUEUE
    std::thread t2(PushFunc);
    std::thread t3(PushFunc);
#endif
    t1.join();
#ifdef DMULTI_THREAD_PUSH_QUEUE
    t2.join();
    t3.join();
#endif
    while(1);
    return 0;
}
