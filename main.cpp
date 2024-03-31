#include "UDPClass.h"
#include "TCPClass.h"

// Global variable for chat client
ClientClass* client = nullptr;
// Global variable for notifying main function about EOF
bool eof_event = false;

void signalHandler (int sig_val) {
    if (sig_val == SIGINT)
        client->send_priority_bye();
}

void handle_user_input () {
    struct pollfd fds[1];
    // Standard input (stdin)
    fds[0].fd = 0;
    fds[0].events = POLLIN;

    // Process user input
    std::string user_line;
    std::vector<std::string> line_vec;

    while (std::cin.eof() == false && client->stop_program() == false) {
        int result = poll(fds, 1, /*waiting timeout [ms]*/100);
        if (result > 0) {
            if (fds[0].revents & (POLLIN | POLLHUP)) { // Input is available, read it
                std::getline(std::cin, user_line);

                // Skip empty line
                if (user_line.empty() == true)
                    continue;

                if (user_line.c_str()[0] == '/') {
                    // Command - load words from user input line
                    client->split_to_vec(user_line, line_vec, ' ');
                    if (line_vec.at(0) == std::string("/auth") && line_vec.size() == 4)
                        client->send_auth(line_vec.at(1), line_vec.at(3), line_vec.at(2));
                    else if (line_vec.at(0) == std::string("/join") && line_vec.size() == 2)
                        client->send_join(line_vec.at(1));
                    else if (line_vec.at(0) == std::string("/rename") && line_vec.size() == 2)
                        client->send_rename(line_vec.at(1));
                    else if (line_vec.at(0) == std::string("/help") && line_vec.size() == 1)
                        OutputClass::out_help_cmds();
                    else if (std::cin.eof() == false) // Output error and continue
                        OutputClass::out_err_intern("Unknown command or unsufficinet number of command params provided");
                }
                else if (std::cin.eof() == false) // Msg to send
                    client->send_msg(user_line);
            }
        }
    }
    // Check for EOF event
    if (std::cin.eof() && client->stop_program() == false) {
        eof_event = true;
        // Notify main thread
        client->get_cond_var().notify_one();
    }
}

int main (int argc, char *argv[]) {
    // Store client type given by user
    char* client_type = nullptr;
    // Mutex for conditional variable
    std::mutex end_mutex;

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
    if (!client_type || data_map.find("ipaddr") == data_map.end()) {
        OutputClass::out_err_intern("Compulsory values are missing");
        return EXIT_FAILURE;
    }

    TCPClass tcpClient(data_map);
    UDPClass udpClient(data_map);

    // Decide which client use
    if (strcmp(client_type, "tcp") == 0)
        client = &tcpClient;
    else
        client = &udpClient;

    // Try opening new connection
    try {
        client->open_connection();
    } catch (const std::logic_error& e) {
        OutputClass::out_err_intern(std::string(e.what()));
        return EXIT_FAILURE;
    }

    // Set interrput signal handling - CTRL+C
    std::signal(SIGINT, signalHandler);

    // Create thread for user input
    std::jthread user_input = std::jthread(handle_user_input);

    // Wait for either user EOF or thread ENDING
    std::unique_lock<std::mutex> lock(end_mutex);
    client->get_cond_var().wait(lock, [] {
        return (eof_event || client->stop_program());
    });

    if (eof_event == true) // User EOF event
        client->send_bye();
    client->wait_for_threads();

    // End program
    return EXIT_SUCCESS;
}
