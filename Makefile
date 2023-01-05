CFLAGS 		+= 	-g -std=c++0x -O2 -Wl,-lpthread
CXX			= 	g++

queue_test:ring_queue_test.cpp compact_ring_queue.h
	$(CXX) $(CFLAGS) -o queue_test ring_queue_test.cpp
clean:
	rm queue_test
