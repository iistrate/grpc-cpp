# Project 3: Distributed Service using GRPC

## Overview
This project has us build a store that receives requests from different users, querying the prices offered by the different registered vendors.

## Store
The registered vendors addresses are in ./src/vendor_addresses.txt
Our store is implemented in ./src/store.cc:
 1. Takes in address and threads_num, defaults are: "0:0:0:0:50093" and 4
 2. Loads up the vendor addresses to which we will make async calls from ./src/vendor_addresses.txt
Once our store is loaded, we then proceed to initialize the threadpool which is defined in threadpool.h
## Threadpool
The threadpool is using a vector for holding the threads, and each thread is assigned to:
~~~~
void thread_work(std::vector <std::string> ips);
~~~~
We send in the ips, as loaded by the store to each thread.
Our threadpool uses as a condition variable:
~~~
thread_pool->cv.wait(lk, []{return thread_pool->is_joined() || !work_queue.empty();})
~~~
Where `thread_pool->is_joined()` is used to break out of the main loop if program is killed and `work_queue.empty()` to check if there's any available work to be performed.
Our work is passed around as a transport_t type where, among other, we save an instance of `ServerImpl`.
~~~
typedef struct transport_t {
    store::Store::AsyncService* service_;
    ServerCompletionQueue* cq_;
	void* instance_;
} transport_t;
~~~
## How it works
After creating our store and threadpool, we instantiate `ServerImpl` and run it.
`class ServerImpl` and `class VendorClient` have been uses as per the [HelloWorld example](https://github.com/grpc/grpc/tree/master/examples/cpp/helloworld). from the gRPC C++ Hello World Tutorial.
`class ServerImpl` has the `void Proceed()` as follows
~~~
    void Proceed() {
    	transport_t* new_work = new transport_t;
		new_work->instance_ = (void*)this;
		new_work->cq_ = cq_;
		new_work->service_ = service_;
        work_queue.push(new_work);
        thread_pool->cv.notify_one();
    }
~~~
We save the instance in our Queue, as well as the ServerCompletionQueue, and the store::Store::AsyncService* service_ to be processes within our 
`void thread_work(std::vector <std::string> ips)`
Once we signal a thread that work can be done we
~~~
//pop from queue
transport_t* transport = work_queue.front();
work_queue.pop();
//release mutex
lk.unlock();
~~~
And perform the work as it was in the original  `void Proceed()` using  + we contact each of the vendors with `VendorClient`. All of these is done by using the Async method as per the HelloWorld async example for both server and client. We then, add the products and set their bid, and vendor id.
~~~
for (std::vector <std::string>::iterator it_ip = ips.begin(); it_ip != ips.end(); it_ip++) {
	std::cout << std::this_thread::get_id() << " contacting vendor" << *it_ip << std::endl;
	VendorClient client(grpc::CreateChannel(
		*it_ip, grpc::InsecureChannelCredentials()));
	vendor::BidReply reply = client.getProductBid(((ServerImpl::CallData*)transport->instance_)->request_.product_name());
	store::ProductInfo* product_info = ((ServerImpl::CallData*)transport->instance_)->reply_.add_products();
	//set retrieved product info
	product_info->set_price(reply.price());
	product_info->set_vendor_id(reply.vendor_id());
}
~~~
## Cleanup
Cleanup is done by listening to a kill event with:
~~~
static void _sig_handler(int signo){
	if (signo == SIGINT || signo == SIGTERM){
		std::cout << "kill cmd fired" << std::endl;
		delete my_store;
		delete thread_pool;
		exit(signo);
	}
}
if (signal(SIGTERM, _sig_handler) == SIG_ERR || signal(SIGINT, _sig_handler) == SIG_ERR){
	fprintf(stderr,"Can't catch SIGTERM or SIGINT...exiting.\n");
	delete my_store;
	delete thread_pool;
	exit(EXIT_FAILURE);
}
~~~
And the thread cleanup is done by, setting one of our Condition Variable to True and broadcasting to all the threads so the can exit the main loop `while(!thread_pool->is_joined())`, then join and delete them:
~~~
Threadpool::~Threadpool() {
	joined = true;
	cv.notify_all();
	for (int i = 0, j = threads.size(); i < j; i++) {
		std::cout << "cleaning up " << threads[i]->get_id() << std::endl;
		threads[i]->join();
		delete threads[i]; //joined threads still occupy some memory.
	}
}
~~~
## Testing
I have tested my project by running the following commands in 2 separate terminals:
~~~
$ top -H -p $(pidof store) || ps -T -p $(pidof store) #shows usage per thread
$ for ((n=0; n < 100;n++)) do ./run_tests 0.0.0.0:50093 10 && sleep 0.3; done
~~~
And by the old, thread id print out interleaving.
## Other
All in all, this was a simple project taking ~8h and was highly enjoyable. I had some issues with installing grpc because I missed the project description installation details somehow and tried installing it by following their documentation. When I followed your documentation everything worked like a charm. Thank you for that!

## Resources
1. [HelloWorld example gRPC](https://github.com/grpc/grpc/tree/master/examples/cpp/helloworld)
2. http://en.cppreference.com/w/cpp/thread/thread
3. http://en.cppreference.com/w/cpp/thread/condition_variable
4. https://github.com/grpc/grpc/issues/9728
5. And my previous work using multi-threading in Sun RPC from GIOS.



