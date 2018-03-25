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

public:
	Threadpool() {}
	~Threadpool() {
		for (int i = 0, j = threads.size(); i < j; i++) {
			std::cout << threads[i]->get_id() << "cleaning up " << std::endl;
			threads[i]->join();
			delete threads[i]; //joined thread still occupies memory.
		}
	}

	std::vector <std::thread*> threads;
	std::mutex mutex_lock;
	std::condition_variable cv;
};

#endif //THREAD_POOL_H

