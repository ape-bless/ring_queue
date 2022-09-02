CFLAGS 		+= 	-g -std=c++0x -Wl,-lpthread
CXX			= 	g++

ifeq ($(mw), 1)
	CFLAGS += -DMULTI_THREAD_PUSH_QUEUE
endif

ifeq ($(mr), 1)
	CFLAGS += -DMULTI_THREAD_POP_QUEUE
endif

queue_test:ring_queue_test.cpp
	$(CXX) $(CFLAGS) -o queue_test ring_queue_test.cpp
clean:
	rm queue_test
