#include "TCPClass.h"

TCPClass::TCPClass(std::map<std::string, std::string> data_map)
    : ClientClass ()
{
    std::map<std::string, std::string>::iterator iter;
    // Look for init values in map to override init values
    if ((iter = data_map.find("ipaddr")) != data_map.end())
        this->server_hostname = iter->second;

    if ((iter = data_map.find("port")) != data_map.end())
        this->port = static_cast<uint16_t>(std::stoi(iter->second));
}
/***********************************************************************************/
void TCPClass::open_connection() {
    // Create TCP socket
    if ((this->socket_id = socket(AF_INET, SOCK_STREAM, 0)) <= 0)
        throw std::logic_error("TCP socket creation failed");

    struct hostent *server = gethostbyname(this->server_hostname.c_str());
    if (!server)
        throw std::logic_error("Unknown or invalid hostname provided");

    // Setup server details
    struct sockaddr_in server_addr;
    // Make sure everything is reset
    memset(&server_addr, 0, sizeof(server_addr));
    // Set domain
    server_addr.sin_family = AF_INET;
    // Set port number
    server_addr.sin_port = htons(this->port);
    // Set server address
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    // Connect to server
    if (connect(this->socket_id, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
        throw std::logic_error("Error connecting to TCP server");

    // Create threads for sending and receiving server msgs
    this->send_thread = std::jthread(&TCPClass::handle_send, this);
    this->recv_thread = std::jthread(&TCPClass::handle_receive, this);
}
/***********************************************************************************/
void TCPClass::session_end() {
    this->stop_send = true;
    this->stop_recv = true;
    // Change state
    this->cur_state = S_END;
    // Clear sockets
    shutdown(this->socket_id, SHUT_RD);
    shutdown(this->socket_id, SHUT_WR);
    shutdown(this->socket_id, SHUT_RDWR);
    // Close socket
    close(this->socket_id);
    // Exit the program by notifying main function
    this->end_program = true;
    this->cond_var.notify_one();
    // Ensure stopping of user input handling thread
    this->load_input = true;
    this->input_cond_var.notify_one();
}
/***********************************************************************************/
void TCPClass::send_auth(std::string user, std::string display, std::string secret) {
    // Update display name
    if (send_rename(display) == false)
        return;

    TCP_DataStruct data = {
        .type = AUTH,
        .user_name = user,
        .display_name = display,
        .secret = secret
    };
    send_message(data);
}

void TCPClass::send_msg(std::string msg) {
    TCP_DataStruct data = {
        .type = MSG,
        .message = msg,
        .display_name = this->display_name
    };
    send_message(data);
}

void TCPClass::send_join(std::string channel_id) {
    TCP_DataStruct data = {
        .type = JOIN,
        .display_name = this->display_name,
        .channel_id = channel_id
    };
    send_message(data);
}

bool TCPClass::send_rename(std::string new_display_name) {
    if (regex_match(new_display_name, display_name_pattern) == false) {
        OutputClass::out_err_intern("Invalid new value for display name");
        return false;
    }
    // Update display name
    this->display_name = new_display_name;
    return true;
}

void TCPClass::send_bye() {
    // Send bye message
    TCP_DataStruct data = {
        .type = BYE
    };
    send_message(data);
}
/***********************************************************************************/
void TCPClass::send_priority_bye () {
    this->high_priority = true;
    // Switch to end state
    this->cur_state = S_END;
    send_bye();
}
/***********************************************************************************/
void TCPClass::send_err (std::string err_msg) {
    // Switch to err state
    this->cur_state = S_ERROR;

    TCP_DataStruct data = {
        .type = ERR,
        .message = err_msg,
        .display_name = this->display_name
    };
    send_message(data);
}
/***********************************************************************************/
void TCPClass::send_message(TCP_DataStruct &data) {
    // Check for message validity
    if (check_valid_msg<TCP_DataStruct>(data.type, data) == false) {
        OutputClass::out_err_intern("Invalid content of message provided, wont send");
        return;
    }

    // Avoid racing between main and response thread
    std::lock_guard<std::mutex> lock(this->editing_front_mutex);
    if (this->high_priority == true) {
        // Clear the queue
        this->messages_to_send = {};
        // Reset waiting for reply flag
        this->wait_for_reply = false;
        // Reset priority flag after using
        this->high_priority = false;
    }
    // Add new message to the queue with given resend count
    this->messages_to_send.push(data);
}
/***********************************************************************************/
void TCPClass::send_data(TCP_DataStruct &data) {
    // Prepare data to send
    std::string message = convert_to_string(data);
    const char* out_buffer = message.c_str();

    // Send data
    ssize_t bytes_send = send(this->socket_id, out_buffer, message.size(), 0);

    // Check for errors
    if (bytes_send < 0)
        OutputClass::out_err_intern("Error while sending data to server");
}
/***********************************************************************************/
MSG_TYPE TCPClass::get_msg_type(std::string first_msg_word) {
    if (std::regex_match(first_msg_word, std::regex("^REPLY$", std::regex_constants::icase)))
        return REPLY;
    if (std::regex_match(first_msg_word, std::regex("^AUTH$", std::regex_constants::icase)))
        return AUTH;
    if (std::regex_match(first_msg_word, std::regex("^JOIN$", std::regex_constants::icase)))
        return JOIN;
    if (std::regex_match(first_msg_word, std::regex("^MSG$", std::regex_constants::icase)))
        return MSG;
    if (std::regex_match(first_msg_word, std::regex("^ERR$", std::regex_constants::icase)))
        return ERR;
    if (std::regex_match(first_msg_word, std::regex("^BYE$", std::regex_constants::icase)))
        return BYE;
    return NO_TYPE;
}
/***********************************************************************************/
void TCPClass::handle_send() {
    while (this->stop_send == false) {
        { // Mutex lock scope
            std::unique_lock<std::mutex> lock(this->editing_front_mutex);
            if (this->messages_to_send.empty() == true && this->wait_for_reply == false) {
                this->load_input = true;
                this->input_cond_var.notify_one();
            }
            else if (this->messages_to_send.empty() == false && this->wait_for_reply == false) {
                // Stop sending if requested
                if (this->stop_send == true)
                    break;

                // Load message to send from queue front
                auto to_send = this->messages_to_send.front();

                // Check if given message can be send in client's current state
                if (check_msg_context(to_send.type, this->cur_state) == false) {
                    OutputClass::out_err_intern("Sending this type of message is prohibited for current client state");
                    // Remove this message from queue
                    this->messages_to_send.pop();
                    continue;
                }

                // Send it to server
                send_data(to_send);

                // Wait with sending another msgs till REPLY from server is received
                if (to_send.type == AUTH || to_send.type == JOIN)
                    this->wait_for_reply = true;

                // After sending BYE to server, close connection
                if (to_send.type == BYE)
                    session_end();

                // Remove message after being sent
                this->messages_to_send.pop();
            }
        } // Mutex unlocks when getting out of scope
    }
}
/***********************************************************************************/
void TCPClass::switch_to_error (std::string err_msg) {
    // Notify user
    OutputClass::out_err_intern(err_msg);
    // Clear the queue
    this->high_priority = true;
    // Notify server
    send_err(err_msg);
    // Then send BYE and end
    send_bye();
}
/***********************************************************************************/
void TCPClass::handle_receive () {
    char in_buffer[MAXLENGTH];
    size_t msg_shift = 0;
    std::string response;

    while (this->stop_recv == false) {
        ssize_t bytes_received =
            recv(this->socket_id, (in_buffer + msg_shift), (MAXLENGTH - msg_shift), 0);

        if (this->stop_recv == true) // Stop when requested
            break;

        if (bytes_received <= 0) {
            OutputClass::out_err_intern("Unexpected server disconnected");
            session_end();
            break;
        }

        // Store response to string
        std::string recv_data((in_buffer + msg_shift), bytes_received);
        response += recv_data;

        // Check if message is completed, thus. ending with "\r\n", else continue and wait for rest of message
        if (std::regex_search(recv_data, std::regex("\\r\\n$")) == false) {
            msg_shift += bytes_received;
            continue;
        }
        // Reuse it
        size_t end_symb_pos = 0;
        // When given buffer contains multiple messages, iterate thorugh them
        while ((end_symb_pos = response.find("\r\n")) != std::string::npos) {
            std::string cur_msg = response.substr(0, end_symb_pos);

            // Move in buffer to another msg (if any)
            response.erase(0, (end_symb_pos + /*delimiter length*/2));

            // Load whole message - each msg ends with "\r\n";
            split_to_vec(cur_msg, this->line_vec, ' ');

            TCP_DataStruct data;
            try { // Check for valid msg_type provided
                deserialize_msg(data);
            } catch (const std::logic_error& e) {
                // Invalid message from server -> end connection
                switch_to_error(e.what());
                break;
            }

            // Process response
            switch (this->cur_state) {
                case S_AUTH:
                    switch (data.type) {
                        case REPLY:
                            // Output message
                            OutputClass::out_reply(data.result, data.message);

                            if (data.result == true) // Positive reply - switch to open
                                this->cur_state = S_OPEN;
                            // else: Negative reply -> stay in AUTH state and allow user to re-authenticate

                            // Reset waiting for reply flag
                            this->wait_for_reply = false;
                            break;
                        case ERR: // Output error and end
                            OutputClass::out_err_server(data.display_name, data.message);
                            send_priority_bye();
                            break;
                        default: // Transition to error state
                            switch_to_error("Unexpected message received");
                            break;
                    }
                    break;
                case S_OPEN:
                    switch (data.type) {
                        case REPLY:
                            // Output server reply
                            OutputClass::out_reply(data.result, data.message);
                            // Reset waiting for reply flag
                            this->wait_for_reply = false;
                            break;
                        case MSG: // Output message
                            OutputClass::out_msg(data.display_name, data.message);
                            break;
                        case ERR: // Output error and send bye
                            OutputClass::out_err_server(data.display_name, data.message);
                            send_priority_bye();
                            break;
                        case BYE: // End connection
                            session_end();
                            break;
                        default: // Transition to error state
                            switch_to_error("Unexpected message received");
                            break;
                    }
                    break;
                case S_ERROR: // Switch to end state
                    this->cur_state = S_END;
                    send_priority_bye();
                    break;
                case S_START: // After initial connection immediate server msg, unexpected
                    // Notify user
                    OutputClass::out_err_intern("Unexpected message received");
                    // Clear the queue
                    this->high_priority = true;
                    // Then send BYE and end
                    send_bye();
                    break;
                case S_END: // Ignore everything
                    break;
                default: // Not expected state, output error
                    OutputClass::out_err_intern("Unknown current client state");
                    break;
            }
        }
        // Reset before next iteration
        response = "";
        msg_shift = 0;
    }
}
/***********************************************************************************/
std::string TCPClass::load_rest (size_t start_from) {
    std::string out = "";
    // Concatenate the rest of words as message content
    for (size_t start_ind = start_from; start_ind < this->line_vec.size(); ++start_ind) {
        if (start_ind != start_from)
            out += " ";
        out += this->line_vec.at(start_ind);
    }
    return out;
}
/***********************************************************************************/
void TCPClass::deserialize_msg(TCP_DataStruct& out_str) {
    out_str.type = get_msg_type(this->line_vec.at(0));
    switch (out_str.type) {
        case REPLY: // REPLY OK/NOK IS {MessageContent}\r\n
            if (this->line_vec.size() < 3)
                throw std::logic_error("Unsufficient lenght of REPLY message received");
            out_str.result = (std::regex_match(this->line_vec.at(1), std::regex("^OK$", std::regex_constants::icase)) ? true : false);
            out_str.message = load_rest(3);
            break;
        case MSG: // MSG FROM {DisplayName} IS {MessageContent}\r\n
            if (this->line_vec.size() < 4)
                throw std::logic_error("Unsufficient lenght of MSG message received");

            out_str.display_name = this->line_vec.at(2);
            out_str.message = load_rest(4);
            break;
        case ERR: // ERROR FROM {DisplayName} IS {MessageContent}\r\n
            if (this->line_vec.size() < 4)
                throw std::logic_error("Unsufficient lenght of ERR message received");

            out_str.display_name = this->line_vec.at(2);
            out_str.message = load_rest(4);
            break;
        case BYE: // BYE\r\n
            break;
        case AUTH: // Why would server send AUTH message to client?
            throw std::logic_error("Unexpected message received");
        default:
            throw std::logic_error("Unknown message type provided");
    }
    // Check for msg integrity
    if (check_valid_msg<TCP_DataStruct>(out_str.type, out_str) == false)
        throw std::logic_error("Invalid message provided");
}
/***********************************************************************************/
std::string TCPClass::convert_to_string(TCP_DataStruct &data) {
    std::string msg;
    switch (data.type) {
    case AUTH: // AUTH {Username} AS {DisplayName} USING {Secret}\r\n
        msg = "AUTH " + data.user_name + " AS " + data.display_name + " USING " + data.secret + "\r\n";
        break;
    case JOIN: // JOIN {ChannelID} AS {DisplayName}\r\n
        msg = "JOIN " + data.channel_id + " AS " + data.display_name + "\r\n";
        break;
    case MSG: // MSG FROM {DisplayName} IS {MessageContent}\r\n
        msg = "MSG FROM " + data.display_name + " IS " + data.message + "\r\n";
        break;
    case ERR: // ERR FROM {DisplayName} IS {MessageContent}\r\n
        msg = "ERR FROM " + data.display_name + " IS " + data.message + "\r\n";
        break;
    case BYE: // BYE\r\n
        msg = "BYE\r\n";
        break;
    default: // Shouldnt happen as type is not user-provided
        break;
    }
    // Return composed message
    return msg;
}
