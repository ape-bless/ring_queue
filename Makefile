queue_test:ring_queue_test.cpp
	g++ -g -o queue_test ring_queue_test.cpp -std=c++0x -lpthread -DMULTI_THREAD_PUSH_QUEUE
clean:
	rm queue_test
