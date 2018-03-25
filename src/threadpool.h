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


class Threadpool {
private:
	bool joined;
public:
	Threadpool():joined(false) {}
	~Threadpool();

	std::vector <std::thread*> threads;
	std::mutex mutex_lock;
	std::condition_variable cv;

	bool is_joined() {
		return joined;
	}
};

#endif //THREAD_POOL_H

