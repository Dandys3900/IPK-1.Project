#include "UDPClass.h"
#include "TCPClass.h"

// Global variable for chat client
AbstractClass* client;

int main (int argc, char *argv[]) {
    // Store client type given by user
    char* client_type = "";

    // Map for storing user values
    std::map<std::string, std::string> data_map;

    // Parse cli args
    for (int index = 1; index < argc; ++index) {
        std::string cur_val(argv[index]);
        if (cur_val == std::string("-t"))
            client_type = argv[++index];
        else if (cur_val == std::string("-s"))
            data_map.insert({"ipaddr", std::string(argv[++index])});
        else if (cur_val == std::string("-p"))
            data_map.insert({"port", std::string(argv[++index])});
        else if (cur_val == std::string("-d"))
            data_map.insert({"timeout", std::string(argv[++index])});
        else if (cur_val == std::string("-r"))
            data_map.insert({"reconcount", std::string(argv[++index])});
        else if (cur_val == std::string("-h")) {
            // Print help to stdout and exit
            OutputClass::out_help();
            return EXIT_SUCCESS;
        }
        else {
            OutputClass::out_err_intern("Unknown flag provided");
            return EXIT_FAILURE;
        }
    }

    // Check if compulsory user values -t and -s were given
    if (strcmp(client_type, "") == 0 || data_map.find("ipaddr") == data_map.end()) {
        OutputClass::out_err_intern("Compulsory values are missing");
        return EXIT_FAILURE;
    }

    // Decide which user to use
    if (strcmp(client_type, "tcp") == 0)
        client = new TCPClass(data_map);
    else
        client = new UDPClass(data_map);

    // Try opening new connection
    try {
        client->open_connection();
    } catch (std::string err_msg) {
        OutputClass::out_err_intern(std::string(err_msg));
        return EXIT_FAILURE;
    }

    // Process user input
    std::string user_line;
    std::vector<std::string> line_vec;

    // Set interrput signal handling
    std::signal(SIGINT, [](int sig_val){
        client->send_bye();
        exit(EXIT_SUCCESS);
    });

    while (true) {
        // End connection in case of EOF
        if (std::cin.eof()) {
            client->send_bye();
            break;
        }

        std::getline(std::cin, user_line);
        // Try process user command
        try {
            if (user_line.c_str()[0] == '/') {
                // Command - load words from user input line
                client->get_line_words(user_line, line_vec);
                if (line_vec.at(0) == std::string("/auth") && line_vec.size() == 4) {
                    client->send_auth(line_vec.at(1), line_vec.at(3), line_vec.at(2));
                }
                else if (line_vec.at(0) == std::string("/join") && line_vec.size() == 2) {
                    client->send_join(line_vec.at(1));
                }
                else if (line_vec.at(0) == std::string("/rename") && line_vec.size() == 2) {
                    client->send_rename(line_vec.at(1));
                }
                else if (line_vec.at(0) == std::string("/help")) {
                    OutputClass::out_help();
                }
                else {
                    // Output error and continue
                    OutputClass::out_err_intern("Unknown command or unsufficinet number of command params provided");
                }
            }
            else // Msg to send
                client->send_msg(user_line);
        } catch (std::string err_msg) {
            // Output error but continue
            OutputClass::out_err_intern(std::string(err_msg));
        }
    }
    delete client;

    return EXIT_SUCCESS;
}