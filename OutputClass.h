#ifndef OUTPUTCLASS_H
#define OUTPUTCLASS_H

#include "ConstsFile.h"

using namespace std;

class OutputClass {
    public:
        static void out_err_intern (string msg) {
            cerr << string("ERR: " + msg + "\n").c_str();
            fflush(stderr);
        }

        static void out_err_server (string display_name, string msg) {
            cerr << string("ERR FROM " + display_name + ": " + msg + "\n").c_str();
            fflush(stderr);
        }

        static void out_msg (string display_name, string msg) {
            cout << string(display_name + ": " + msg + "\n").c_str();
            fflush(stdout);
        }

        static void out_reply (bool result, string reason) {
            cerr << string(((result) ? "Success: " : "Failure: ") + reason + "\n").c_str();
            fflush(stderr);
        }

        static void out_help () {
            cout << std::string("Help text: \n Use -t to set type [tcp/udp],\n -s for providing IPv4 address,\n -p for specifying port,\n -d for UDP timeout\n -r for UDP retransmissions count \n").c_str();
            fflush(stdout);
        }
};

#endif // OUTPUTCLASS_H