#ifndef OUTPUTCLASS_H
#define OUTPUTCLASS_H

#include "ConstsFile.h"

using namespace std;

class OutputClass {
    public:
        static void out_err_intern (string msg) {
            cerr << string("ERROR: " + msg + "\n").c_str();
        }

        static void out_err_server (string display_name, string msg) {
            cerr << string("ERROR FROM " + display_name + ": " + msg + "\n").c_str();
        }

        static void out_msg (string display_name, string msg) {
            cout << string(display_name + ": " + msg + "\n").c_str();
        }

        static void out_reply (bool result, string reason) {
            cout << string(((result) ? "Success: " : "Failure: ") + reason + "\n").c_str();
        }

        static void out_help () {
            cout << std::string("Help text: \n Use -t to set type [tcp/udp], -s for providing IPv4 address, -p for specifying port, -d for UDP timeout and -r for UDP retransmissions count \n").c_str();
        }
};

#endif // OUTPUTCLASS_H