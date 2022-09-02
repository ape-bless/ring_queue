# ring_queue
# 1. 简介
&#8195;&#8195;该项目实现了一个同时支持一写一读、多写一读、多写多读的通用、高效的循环队列，可用于消息队列、异步日志的读写等场景。其中的多写、多读，都可以通过编译开关启用、禁止。为了提升性能，我们采用了inplacement new 来减少内存的分配、复制，同时我们每次读、写操作，都会进行字节对齐，来提升内存的访问速度。    
&#8195;&#8195;队列类名之所以包含一个Compact(紧凑的)，是因为在保证性能的同时，还兼顾到了内存的紧凑性。   
&#8195;&#8195;这个循环队列一写一读、多写一读部分，已经经过了一款百万DAU的线上项目的验证，可以放心使用，多写多读只是在测试环境压测过。

# 2. 使用
## 2.1 一写一读
make 
## 2.1 多写一读
make mw=1
## 2.1 多写一读
make mw=1 mr=1  
最后会生成可执行程序 queue_test，这个程序会将 Push 进队列的内容输出到标准输出，如果需要做其他处理，可以修改 BasalRingQueueWorker::Executor 的逻辑
