#include "compact_ring_queue.h"
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <fstream>
#include <atomic>
#include <iostream>
#include <vector>

const uint32_t kMemLen = 8 * 1024 * 1024; // 8M
const uint32_t kLineLen = 128 * 1024; // 128K

CommonCompactRingQueue<> queue_worker;
//std::atomic_bool push_finished = false;
volatile bool push_finished = false;

pthread_barrier_t barrier;
int push_thread_num = 8;
int pop_thread_num = 8;

void *PushExcutor(void *arg)
{
    char line[] = "nihaowohaodajiahao12345623e42ffassa3wfasffafewafsdaweasdccccccccccccccccccccsdswdeasjfajxidwoaanfkleinafnfeownfklxjfha234481ndns889x12#232311441344552";
    int32_t line_num = 8000000 / push_thread_num;
    int32_t line_len = strlen(line);
    for (int i = 0; i < line_num; ++i) {
        queue_worker.Push(line, line_len);
    }
    pthread_barrier_wait(&barrier);
    return (void *)0;
}

void *PopExcutor(void *arg)
{
    char buff[kLineLen];
    if (pop_thread_num > 1) {
        while (!push_finished) {
            if (queue_worker.Pop(buff, kLineLen) <= 0 && push_finished) {
                break;
            }
        }
    } else {
        while (!push_finished) {
            CompactQueueElement *e = queue_worker.Pop();
            if (!e && push_finished) {
                break;
            }
        }
    }
    return (void *)0;
}

void *ReadExcutor(void *arg)
{
    char *p = (char *)arg;
    std::ifstream in_file(p, std::ios::in);
    if (!in_file.is_open()) {
        std::cout << "open file " << p << " failed" << std::endl;
        pthread_barrier_wait(&barrier);
        return (void *) - 1;
    }

    char line[kLineLen];
    while (in_file.getline(line, kLineLen)) {
        uint32_t l = strlen(line);
        line[l] = 0;
        CompactQueueElement *e = queue_worker.Push(line, l + 1);
    }
    in_file.close();
    std::cout << (long)pthread_self() << " push finished" << std::endl;
    pthread_barrier_wait(&barrier);
    return (void *)0;
}

void *WriteExcutor(void *arg)
{
    char *p = (char *)arg;
    std::ofstream o_file(p, std::ios::out);
    char buff[kLineLen];

    if (!o_file.is_open()) {
        std::cout << "open file " << p << " failed" << std::endl;
        return (void *) - 1;
    }

    if (pop_thread_num > 1) {
        while (!push_finished) {
            int len = queue_worker.Pop(buff, kLineLen);
            if (len > 0) {
                o_file.write(buff, len);
            } else if (push_finished) {
                break;
            }
        }
    } else {
        while (!push_finished) {
            CompactQueueElement *e = queue_worker.Pop();
            if (e) {
                o_file.write(e->buff, e->len);
            } else if (push_finished) {
                break;
            }
        }
    }
    o_file.close();

    return (void *)0;
}

void speed_test()
{
    pthread_barrier_init(&barrier, nullptr, push_thread_num + 1);

    timespec begin_time;
    clock_gettime(CLOCK_REALTIME, &begin_time);

    printf("%s begin create thread\n", __FUNCTION__);
    char t_name[128];
    std::vector<pthread_t> o_tids;
    o_tids.resize(pop_thread_num);
    for (int i = 0; i < pop_thread_num; ++i) {
        pthread_create(&o_tids[i], nullptr, PopExcutor, nullptr);
        snprintf(t_name, sizeof(t_name), "out_%d", i);
        pthread_setname_np(o_tids[i], t_name);
    }

    pthread_t i_tid;
    for (int i = 0; i < push_thread_num; ++i) {
        pthread_create(&i_tid, nullptr, PushExcutor, nullptr);
        snprintf(t_name, sizeof(t_name), "in_%d", i);
        pthread_setname_np(i_tid, t_name);
    }

    pthread_barrier_wait(&barrier);
    pthread_barrier_destroy(&barrier);

    push_finished = true;
    printf("%s all push finished\n", __FUNCTION__);

    for (size_t i = 0; i < o_tids.size(); ++i) {
        pthread_join(o_tids[i], nullptr);
    }

    timespec end_time;
    clock_gettime(CLOCK_REALTIME, &end_time);
    int64_t begin_ns = begin_time.tv_sec * 1000000000 + begin_time.tv_nsec;
    int64_t end_ns = end_time.tv_sec * 1000000000 + end_time.tv_nsec;
    int64_t diff_ns = end_ns - begin_ns;
    int64_t diff_ms = diff_ns / 1000000;
    int64_t diff_s = diff_ns / 1000000000;

    printf("%s all pop finished, time cost %ldns[%ldms, %lds]\n", __FUNCTION__, diff_ns, diff_ms, diff_s);
}

void io_test()
{
    pthread_barrier_init(&barrier, nullptr, push_thread_num + 1);

    timespec begin_time;
    clock_gettime(CLOCK_REALTIME, &begin_time);

    push_finished = false;
    printf("%s begin create thread\n", __FUNCTION__);
    char **o_ptr = new char*[pop_thread_num];
    char t_name[128];
    std::vector<pthread_t> o_tids;
    o_tids.resize(pop_thread_num);
    for (int i = 0; i < pop_thread_num; ++i) {
        o_ptr[i] = new char[128];
        snprintf(o_ptr[i], 128, "out_%d.txt", i);
        pthread_create(&o_tids[i], nullptr, WriteExcutor, o_ptr[i]);
        snprintf(t_name, sizeof(t_name), "out_%d", i);
        pthread_setname_np(o_tids[i], t_name);
    }

    pthread_t i_tid;
    char **i_ptr = new char*[push_thread_num];
    for (int i = 0; i < push_thread_num; ++i) {
        i_ptr[i] = new char[128];
        snprintf(i_ptr[i], 128, "in_%d.txt", i);
        pthread_create(&i_tid, nullptr, ReadExcutor, i_ptr[i]);
        snprintf(t_name, sizeof(t_name), "in_%d", i);
        pthread_setname_np(i_tid, t_name);
    }

    pthread_barrier_wait(&barrier);
    pthread_barrier_destroy(&barrier);

    push_finished = true;
    printf("%s all push finished\n", __FUNCTION__);

    for (size_t i = 0; i < o_tids.size(); ++i) {
        pthread_join(o_tids[i], nullptr);
    }
    sleep(3);

    for(int i = 0; i < push_thread_num; ++i) {
        delete []i_ptr[i];
    }
    for(int i = 0; i < pop_thread_num; ++i) {
        delete []o_ptr[i];
    }

    timespec end_time;
    clock_gettime(CLOCK_REALTIME, &end_time);
    int64_t begin_ns = begin_time.tv_sec * 1000000000 + begin_time.tv_nsec;
    int64_t end_ns = end_time.tv_sec * 1000000000 + end_time.tv_nsec;
    int64_t diff_ns = end_ns - begin_ns;
    int64_t diff_ms = diff_ns / 1000000;
    int64_t diff_s = diff_ns / 1000000000;

    printf("%s all pop finished, time cost %ldns[%ldms, %lds]\n", __FUNCTION__, diff_ns, diff_ms, diff_s);
}

int main(int argc, char **argv)
{
    if(3 != argc) {
        std::cout << "usage ./queue_test push_thread_num pop_thread_num" << std::endl;
        return -1;
    }

    push_thread_num = atoi(argv[1]);
    pop_thread_num = atoi(argv[2]);

    void *mem_addr = malloc(kMemLen);
    memset(mem_addr, 0, kMemLen);
    queue_worker.Init((char *)mem_addr, kMemLen, kLineLen, push_thread_num > 1, pop_thread_num > 1);
    printf("init ring queue success\n");

    speed_test();
    io_test();

    return 0;
}
