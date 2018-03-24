#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <queue>
#include <unistd.h>
#include <signal.h>

#include <grpcpp/grpcpp.h>
#include <grpc/support/log.h>

#include "threadpool.h"

#include "store.pb.h"
#include "store.grpc.pb.h"

#include "vendor.pb.h"
#include "vendor.grpc.pb.h"

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;
using grpc::Status;

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;

enum CallStatus { CREATE, PROCESS, FINISH };

typedef struct transport_t {
    store::Store::AsyncService* service_;
    ServerCompletionQueue* cq_;

	void* instance_;
} transport_t;

static std::queue<transport_t*> work_queue;

class WalMart_Store {

private:
	//vendor addresses filename
	std::string vendor_addresses;
	//vendor ip addresses
	std::vector <std::string> ip_addrresses;
	//expose on
	std::string listen_on;

public:
	WalMart_Store(std::string v_a, std::string l_o):vendor_addresses(v_a), listen_on(l_o) {
		this->populate_ip_addresses();
	};
	~WalMart_Store() {}

	void populate_ip_addresses() {
		std::ifstream myfile(this->vendor_addresses);
		if (myfile.is_open()) {
			std::string ip_addr;
			while(getline(myfile, ip_addr)) {
				this->ip_addrresses.push_back(ip_addr);
			}
			myfile.close();
		}
		else std::cout << "Error loading IP addresses\n";
	}

	void show_ip_addresses() {
		for (std::vector<std::string>::iterator it = this->ip_addrresses.begin(); it != this->ip_addrresses.end(); it++) {
			std::cout << *it << std::endl;
		}
	}

	const std::vector <std::string>& get_ip_addresses() {
		return ip_addrresses;
	}

};

static WalMart_Store* my_store;
static Threadpool* thread_pool;

void thread_work(std::vector <std::string> ips);

class VendorClient {
 public:
  explicit VendorClient(std::shared_ptr<Channel> channel)
      : stub_(vendor::Vendor::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  vendor::BidReply getProductBid(const std::string& product_name) {
    // Data we are sending to the server.
	vendor::BidQuery request;
    request.set_product_name(product_name);

    // Container for the data we expect from the server.
    vendor::BidReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The producer-consumer queue we use to communicate asynchronously with the
    // gRPC runtime.
    CompletionQueue cq;

    // Storage for the status of the RPC upon completion.
    Status status;

    // stub_->PrepareAsyncSayHello() creates an RPC object, returning
    // an instance to store in "call" but does not actually start the RPC
    // Because we are using the asynchronous API, we need to hold on to
    // the "call" instance in order to get updates on the ongoing RPC.
    std::unique_ptr<ClientAsyncResponseReader<vendor::BidReply> > rpc(
        stub_->PrepareAsyncgetProductBid(&context, request, &cq));

    // StartCall initiates the RPC call
    rpc->StartCall();

    // Request that, upon completion of the RPC, "reply" be updated with the
    // server's response; "status" with the indication of whether the operation
    // was successful. Tag the request with the integer 1.
    rpc->Finish(&reply, &status, (void*)1);
    void* got_tag;
    bool ok = false;
    // Block until the next result is available in the completion queue "cq".
    // The return value of Next should always be checked. This return value
    // tells us whether there is any kind of event or the cq_ is shutting down.
    GPR_ASSERT(cq.Next(&got_tag, &ok));

    // Verify that the result from "cq" corresponds, by its tag, our previous
    // request.
    GPR_ASSERT(got_tag == (void*)1);
    // ... and that the request was completed successfully. Note that "ok"
    // corresponds solely to the request for updates introduced by Finish().
    GPR_ASSERT(ok);

    // Act upon the status of the actual RPC.
    if (status.ok()) {
      return reply;
    }
  }

 private:
  // Out of the passed in Channel comes the stub, stored here, our view of the
  // server's exposed services.
  std::unique_ptr<vendor::Vendor::Stub> stub_;
};


class ServerImpl final {
 public:
  ~ServerImpl() {
    server_->Shutdown();
    // Always shutdown the completion queue after the server.
    cq_->Shutdown();
  }

  // There is no shutdown handling in this code.
  void Run(std::string addr) {
	std::string server_address(addr);
	ServerBuilder builder;
	// Listen on the given address without any authentication mechanism.
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	// Register "service_" as the instance through which we'll communicate with
	// clients. In this case it corresponds to an *asynchronous* service.
	builder.RegisterService(&service_);
	// Get hold of the completion queue used for the asynchronous communication
	// with the gRPC runtime.
	cq_ = builder.AddCompletionQueue();
	// Finally assemble the server.
	server_ = builder.BuildAndStart();
	std::cout << "Server listening on " << server_address << std::endl;

	// Proceed to the server's main loop.
	HandleRpcs();
  }

 public:
  // Class encompasing the state and logic needed to serve a request.
  class CallData {
   public:
    // Take in the "service" instance (in this case representing an asynchronous
    // server) and the completion queue "cq" used for asynchronous communication
    // with the gRPC runtime.
    CallData(store::Store::AsyncService* service, ServerCompletionQueue* cq)
        : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE) {
      // Invoke the serving logic right away.
      Proceed();
    }

    void Proceed() {
    	transport_t* new_work = new transport_t;
		new_work->instance_ = (void*)this;
		new_work->cq_ = cq_;
		new_work->service_ = service_;
        work_queue.push(new_work);
        thread_pool->cv.notify_one();
    }

   public:
    // The means of communication with the gRPC runtime for an asynchronous
    // server.
    store::Store::AsyncService* service_;
    // The producer-consumer queue where for asynchronous server notifications.
    ServerCompletionQueue* cq_;
    // Context for the rpc, allowing to tweak aspects of it such as the use
    // of compression, authentication, as well as to send metadata back to the
    // client.
    ServerContext ctx_;

    // What we get from the client.
    store::ProductQuery request_;
    // What we send back to the client.
    store::ProductReply reply_;

    // The means to get back to the client.
    ServerAsyncResponseWriter<store::ProductReply> responder_;

    // Let's implement a tiny state machine with the following states.
    CallStatus status_;  // The current serving state.
  };

  // This can be run in multiple threads if needed.
  void HandleRpcs() {
    // Spawn a new CallData instance to serve new clients.
    new CallData(&service_, cq_.get());
    void* tag;  // uniquely identifies a request.
    bool ok;
    while (true) {
      // Block waiting to read the next event from the completion queue. The
      // event is uniquely identified by its tag, which in this case is the
      // memory address of a CallData instance.
      // The return value of Next should always be checked. This return value
      // tells us whether there is any kind of event or cq_ is shutting down.
      GPR_ASSERT(cq_->Next(&tag, &ok));
      GPR_ASSERT(ok);
      static_cast<CallData*>(tag)->Proceed();
    }
  }
  std::unique_ptr<ServerCompletionQueue> cq_;
  store::Store::AsyncService service_;
  std::unique_ptr<Server> server_;
};


static void _sig_handler(int signo){
	if (signo == SIGINT || signo == SIGTERM){
		std::cout << "kill cmd called" << std::endl;
		my_store->~WalMart_Store();
		thread_pool->~Threadpool();
		exit(signo);
	}
}

int main(int argc, char** argv) {
	std::string listen_on = "";
	std::string vendor_addr = "vendor_addresses.txt";
	int threads = 4;

	if (argc != 3) {
		std::cerr << "Error. Usage is ./store address threads\n";
		exit(EXIT_FAILURE);
	}
	else {
		listen_on = (char*)argv[1];
		threads = atoi(argv[2]);
	}

	my_store = new WalMart_Store(vendor_addr, listen_on);
	std::vector <std::string> ips = my_store->get_ip_addresses();

	thread_pool = new Threadpool();
	for (int i = 0; i < threads; i++)	{
		//fire up thread and send the ips with it, we don't really need them though since store is global.
		thread_pool->threads.push_back(new std::thread(thread_work, ips));
	}

	ServerImpl server;
	server.Run(listen_on);

	if (signal(SIGTERM, _sig_handler) == SIG_ERR){
		fprintf(stderr,"Can't catch SIGTERM...exiting.\n");
		goto cleanup;
	}

	cleanup:
		my_store->~WalMart_Store();
		thread_pool->~Threadpool();
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}

void thread_work(std::vector <std::string> ips) {
	while(1) {
		//acquire mutex
		std::unique_lock<std::mutex> lk(thread_pool->mutex_lock);
		//wait on cond variable
		std::cout << std::this_thread::get_id() << " joining the wait" << std::endl;
		thread_pool->cv.wait(lk, []{return !work_queue.empty();});//http://en.cppreference.com/w/cpp/thread/condition_variable
		//pop from queue
		transport_t* transport = work_queue.front();
		work_queue.pop();
		//release mutex
		lk.unlock();
		std::cout << std::this_thread::get_id() << " aquired lock " << std::endl;

    	if (((ServerImpl::CallData*)transport->instance_)->status_ == CREATE) {
		// Make this instance progress to the PROCESS state.
        ((ServerImpl::CallData*)transport->instance_)->status_ = PROCESS;

		// As part of the initial CREATE state, we *request* that the system
		// start processing SayHello requests. In this request, "this" acts are
		// the tag uniquely identifying the request (so that different CallData
		// instances can serve different requests concurrently), in this case
		// the memory address of this CallData instance.
        ((ServerImpl::CallData*)transport->instance_)->service_->RequestgetProducts(&((ServerImpl::CallData*)transport->instance_)->ctx_,
        		&((ServerImpl::CallData*)transport->instance_)->request_, &((ServerImpl::CallData*)transport->instance_)->responder_,
				((ServerImpl::CallData*)transport->instance_)->cq_, ((ServerImpl::CallData*)transport->instance_)->cq_,
				((ServerImpl::CallData*)transport->instance_));
    	}
    	else if (((ServerImpl::CallData*)transport->instance_)->status_ == PROCESS) {
        // Spawn a new CallData instance to serve new clients while we process
        // the one for this CallData. The instance will deallocate itself as
        // part of its FINISH state.
        new ServerImpl::CallData(((ServerImpl::CallData*)transport->instance_)->service_, ((ServerImpl::CallData*)transport->instance_)->cq_);

		//contact vendors
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

        // And we are done! Let the gRPC runtime know we've finished, using the
        // memory address of this instance as the uniquely identifying tag for
        // the event.
		((ServerImpl::CallData*)transport->instance_)->status_ = FINISH;
		((ServerImpl::CallData*)transport->instance_)->responder_.Finish(((ServerImpl::CallData*)transport->instance_)->reply_, Status::OK, ((ServerImpl::CallData*)transport->instance_));
		std::cout << std::this_thread::get_id() << " done and done" << std::endl;
    	}
    	else {
    		GPR_ASSERT(((ServerImpl::CallData*)transport->instance_)->status_ == FINISH);
    		// Once in the FINISH state, deallocate ourselves (CallData).
    		delete ((ServerImpl::CallData*)transport->instance_);
    	}
	}
}
