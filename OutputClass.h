#ifndef OUTPUTCLASS_H
#define OUTPUTCLASS_H

#include "ConstsFile.h"

using namespace std;

class OutputClass {
    public:
        // Output internal error
        static void out_err_intern (string msg) {
            cerr << string("ERR: " + msg) << endl;
        }
        // Output received ERR message from server
        static void out_err_server (string display_name, string msg) {
            cerr << string("ERR FROM " + display_name + ": " + msg) << endl;
        }
        // Output received MSG message from server
        static void out_msg (string display_name, string msg) {
            cout << string(display_name + ": " + msg) << endl;
        }
        // Output received REPLY message from server
        static void out_reply (bool result, string reason) {
            cerr << string(((result) ? "Success: " : "Failure: ") + reason) << endl;
        }
        // Output help about how to run the program
        static void out_help () {
            std::string help_text;
            help_text += "Help text:\n";
            help_text += "  -t to set type [tcp/udp]\n";
            help_text += "  -s for providing IPv4 address\n";
            help_text += "  -p for specifying port\n";
            help_text += "  -d for UDP timeout [ms]\n";
            help_text += "  -r for UDP retransmissions count";
            // Output to stdout
            cout << help_text << endl;
        }
        // Output help about available client commands to use
        static void out_help_cmds () {
            std::string help_text;
            help_text += "Available local commands:\n";
            help_text += "  /auth {Username} {Secret} {DisplayName} : Sends AUTH message to the server, locally sets the DisplayName value\n";
            help_text += "  /join {ChannelID} : Sends JOIN message to the server\n";
            help_text += "  /rename {DisplayName} : Locally changes the display name of the user to be sent with new messages/selected commands\n";
            help_text += "  /help : Prints this help text :-)";
            // Output to stdout
            cout << help_text << endl;
        }
};

#endif // OUTPUTCLASS_H