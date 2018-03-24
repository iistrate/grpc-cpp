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
		for (int i = 0; i < threads.size(); i++) {
			std::cout << threads[i]->get_id() << "cleaning up " << std::endl;
			threads[i]->join();
		}
	}

	std::vector <std::thread*> threads;
	std::mutex mutex_lock;
	std::condition_variable cv;
};

#endif //THREAD_POOL_H

