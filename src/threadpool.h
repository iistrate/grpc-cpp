#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <string>
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <memory>
#include <cstdlib>
#include <mutex>
#include <condition_variable>


typedef std::unique_ptr<std::thread> ThreadPtr;

class Threadpool {
private:

public:
	Threadpool() {}
	~Threadpool() {}

	std::vector <ThreadPtr> threads;
	std::mutex mutex_lock;
	std::condition_variable cv;
};

#endif //THREAD_POOL_H

