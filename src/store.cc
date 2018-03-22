#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>

#include <grpcpp/grpcpp.h>
#include <grpc/support/log.h>

#include "threadpool.h"
#include "store.pb.h"
#include "store.grpc.pb.h"

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;
using grpc::Status;

void run_server(const std::string server_address);

class Store {

private:
	//vendor addresses filename
	std::string vendor_addresses;
	//vendor ip addresses
	std::vector <std::string> ip_addrresses;
	//threads
	Threadpool threadpool;

public:
	Store(std::string v_a):vendor_addresses(v_a) {
		this->populate_ip_addresses();
	};
	~Store() {}

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

 private:
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
      if (status_ == CREATE) {
        // Make this instance progress to the PROCESS state.
        status_ = PROCESS;

        // As part of the initial CREATE state, we *request* that the system
        // start processing SayHello requests. In this request, "this" acts are
        // the tag uniquely identifying the request (so that different CallData
        // instances can serve different requests concurrently), in this case
        // the memory address of this CallData instance.
        service_->RequestgetProducts(&ctx_, &request_, &responder_, cq_, cq_,
                                  this);
      } else if (status_ == PROCESS) {
        // Spawn a new CallData instance to serve new clients while we process
        // the one for this CallData. The instance will deallocate itself as
        // part of its FINISH state.
        new CallData(service_, cq_);

        // The actual processing.
        reply_.products();

        //(prefix + request_.set_product_name());

        // And we are done! Let the gRPC runtime know we've finished, using the
        // memory address of this instance as the uniquely identifying tag for
        // the event.
        status_ = FINISH;
        responder_.Finish(reply_, Status::OK, this);
      } else {
        GPR_ASSERT(status_ == FINISH);
        // Once in the FINISH state, deallocate ourselves (CallData).
        delete this;
      }
    }

   private:
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
    enum CallStatus { CREATE, PROCESS, FINISH };
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

char* filename;
int main(int argc, char** argv) {
	if (argc < 2) {
		filename = (char*)"vendor_addresses.txt";
	}
	else {
		filename = argv[1];
	}

	Store my_store = Store(filename);
	my_store.show_ip_addresses();

//	run_server(my_store.get_ip_addresses()[2]);

	return EXIT_SUCCESS;
}

//void run_server(const std::string server_address)   {
//	std::string server_addressess(server_address);
//	store::Store::service_full_name(server_address);
//
//	grpc::ServerBuilder builder;
//	builder.AddListeningPort(server_addressess, grpc::InsecureServerCredentials());
//	builder.RegisterService(&service);
//
//	std::unique_ptr<Server> server(builder.BuildAndStart());
//	std::cout << "Server listening on " << server_addressess << std::endl;

//	server->Wait();
//}

//load vendor addresses
	//on each product query server requests all of vendor servers for their bid on queried product
	//once client (store) has answers from all vendors, we collate bid, vendor_id from vendors and send it back to client
