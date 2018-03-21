#include "threadpool.h"

#include <iostream>

#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>

class Store {

private:
	std::string vendor_addresses;
	std::vector <std::string> ip_addrresses;
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
};

char* filename;
int main(int argc, char** argv) {
	if (argc < 2) {
		filename = (char*)"vendor_addresses.txt";
	}
	else {
		filename = argv[1];
	}

	Store store = Store(filename);
	store.show_ip_addresses();

	return EXIT_SUCCESS;
}

//load vendor addresses
	//on each product query server requests all of vendor servers for their bid on queried product
	//once client (store) has answers from all vendors, we collate bid, vendor_id from vendors and send it back to client
