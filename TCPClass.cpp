#include "TCPClass.h"

TCPClass::TCPClass(std::map<std::string, std::string> data_map)
    : port            (4567),
      socket_id       (-1),
      retval          (EXIT_SUCCESS),
      display_name    (""),
      server_hostname (""),
      cur_state       (S_START),
      stop_send       (false),
      stop_recv       (false)
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
        throw std::string("TCP socket creation failed");

    struct hostent *server = gethostbyname(this->server_hostname.c_str());
    if (!server)
        throw std::string("Unknown or invalid hostname provided");

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
    if (connect(this->socket_id, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        OutputClass::out_err_intern("Error connecting to TCP server");
        this->retval = EXIT_FAILURE;
        session_end();
    }

    // Set rimeout for checking if server is online (1sec)
    set_socket_timeout();

    // Create threads for sending and receiving server msgs
    this->send_thread = std::jthread(&TCPClass::handle_send, this);
    this->recv_thread = std::jthread(&TCPClass::handle_receive, this);
}
/***********************************************************************************/
void TCPClass::session_end() {
    this->stop_send = true;
    this->stop_recv = true;
    // Change state
    this->cur_state.store(S_END, std::memory_order_relaxed);
    // Clear sockets
    shutdown(this->socket_id, SHUT_RD);
    shutdown(this->socket_id, SHUT_WR);
    shutdown(this->socket_id, SHUT_RDWR);
    // Close socket
    close(this->socket_id);
    // Exit the program
    exit(this->retval);
}
/***********************************************************************************/
void TCPClass::send_auth(std::string user, std::string display, std::string secret) {
    if (this->cur_state.load(std::memory_order_relaxed) != S_START)
        throw std::string("Can't send auth message outside of start state");

    // Update display name
    send_rename(display);

    TCP_DataStruct data = {
        .type = AUTH,
        .user_name = user,
        .display_name = display,
        .secret = secret
    };

    // Move to auth state for handling reply msgs
    this->cur_state.store(S_AUTH, std::memory_order_relaxed);
    send_message(data);
}

void TCPClass::send_msg(std::string msg) {
    if (this->cur_state.load(std::memory_order_relaxed) != S_OPEN)
        throw std::string("Can't process join outside of open state");

    TCP_DataStruct data = {
        .type = MSG,
        .message = msg,
        .display_name = this->display_name
    };
    send_message(data);
}

void TCPClass::send_join(std::string channel_id) {
    if (this->cur_state.load(std::memory_order_relaxed) != S_OPEN)
        throw std::string("Can't process join outside of open state");

    TCP_DataStruct data = {
        .type = JOIN,
        .display_name = this->display_name,
        .channel_id = channel_id
    };
    send_message(data);
}

void TCPClass::send_rename(std::string new_display_name) {
    if (regex_match(new_display_name, display_name_pattern) == false)
        throw std::string("Invalid new value for display name");
    // Update display name
    this->display_name = new_display_name;
}

void TCPClass::send_bye() {
    // Switch to end state
    this->cur_state.store(S_END, std::memory_order_relaxed);
    // Send bye message
    TCP_DataStruct data = {
        .type = BYE
    };
    send_message(data);
}
/***********************************************************************************/
void TCPClass::send_err (std::string err_msg) {
    // Switch to err state
    this->cur_state.store(S_ERROR, std::memory_order_relaxed);

    TCP_DataStruct data = {
        .type = ERR,
        .message = err_msg,
        .display_name = this->display_name
    };
    send_message(data);
}
/***********************************************************************************/
void TCPClass::set_socket_timeout () {
    struct timeval time = {
        .tv_sec = 1, // 1 second
        .tv_usec = 0
    };
    // Set timeout for created socket
    if (setsockopt(this->socket_id, SOL_SOCKET, SO_RCVTIMEO, (char*)&(time), sizeof(struct timeval)) < 0)
        throw std::string ("Setting receive timeout failed");
}
/***********************************************************************************/
void TCPClass::send_message(TCP_DataStruct &data) {
    // Check for msg integrity
    if (check_valid_msg<TCP_DataStruct>(data.type, data) == false) {
        OutputClass::out_err_intern("Invalid message provided");
        return;
    }

    { // Avoid racing between main and response thread
        std::lock_guard<std::mutex> lock(this->editing_front_mutex);
        // Add new message to the queue
        this->messages_to_send.push(data);
    }
}
/***********************************************************************************/
void TCPClass::send_data(TCP_DataStruct &data) {
    // Prepare data to send
    std::string message = convert_to_string(data);
    const char* out_buffer = message.c_str();

    // Send data
    ssize_t bytes_send = send(this->socket_id, out_buffer, message.size(), 0);

    // Check for errors
    if (bytes_send <= 0) {
        OutputClass::out_err_intern("Error while sending data to server");
        this->retval = EXIT_FAILURE;
        session_end();
    }
}
/***********************************************************************************/
MSG_TYPE TCPClass::get_msg_type(std::string first_msg_word) {
    if (first_msg_word == std::string("REPLY"))
        return REPLY;
    if (first_msg_word == std::string("AUTH"))
        return AUTH;
    if (first_msg_word == std::string("JOIN"))
        return JOIN;
    if (first_msg_word == std::string("MSG"))
        return MSG;
    if (first_msg_word == std::string("ERR"))
        return ERR;
    if (first_msg_word == std::string("BYE"))
        return BYE;
    return NO_TYPE;
}
/***********************************************************************************/
void TCPClass::handle_send() {
    while (this->stop_send == false) {
        { // Mutex lock scope
            std::unique_lock<std::mutex> lock(this->editing_front_mutex);
            if (this->messages_to_send.empty() == false) {
                // Stop sending if requested
                if (this->stop_send == true)
                    break;

                // Load message to send from queue front
                auto to_send = this->messages_to_send.front();

                // Send it to server
                send_data(to_send);

                // Remove message after being sent
                this->messages_to_send.pop();

                // After sending BYE to server, close connection
                if (to_send.type == BYE)
                    session_end();
            }
        } // Mutex unlocks when getting out of scope
    }
}
/***********************************************************************************/
void TCPClass::thread_event (THREAD_EVENT event) {
    if (event == TIMEOUT) { // Nothing is received, nothing to send -> move to end state
        if (this->cur_state.load(std::memory_order_relaxed) == S_AUTH || this->cur_state.load(std::memory_order_relaxed) == S_ERROR)
            send_bye();
    }
}
/***********************************************************************************/
void TCPClass::switch_to_error (std::string err_msg) {
    // Notify user
    OutputClass::out_err_intern(err_msg);
    // Notify server
    send_err(err_msg);
}
/***********************************************************************************/
void TCPClass::handle_receive () {
    char in_buffer[MAXLENGTH];
    bool err_occured = false;

    while (this->stop_recv == false) {
        ssize_t bytes_received = recv(this->socket_id, (char*)in_buffer, MAXLENGTH, 0);

        if (this->stop_recv == true) // Stop when requested
            break;

        if (bytes_received < 0) {
            if ((errno == EWOULDBLOCK || errno == EAGAIN)) // Timeout event
                thread_event(TIMEOUT);
            else { // Output error and end
                OutputClass::out_err_intern(std::strerror(errno));
                err_occured = true;
                this->retval = EXIT_FAILURE;
                break;
            }
            continue;
        }
        else if (bytes_received == 0) // No reason for processing zero response
            continue;

        // Store response to string
        std::string response(in_buffer, bytes_received);

        // Load whole message - each msg ends with "\r\n";
        get_line_words(response.substr(0, response.find("\r\n")), this->line_vec);

        TCP_DataStruct data;
        try { // Check for valid msg_type provided
            deserialize_msg(data);
        } catch (std::string err_msg) {
            // Output error and avoid further message processing
            OutputClass::out_err_intern(err_msg);
            // Invalid message from server -> end connection
            this->retval = EXIT_FAILURE;
            send_err(err_msg);
            send_bye();
            continue;
        }

        // Process response
        switch (this->cur_state.load(std::memory_order_relaxed)) {
            case S_AUTH:
                switch (data.type) {
                    case REPLY:
                        // Output message
                        OutputClass::out_reply(data.result, data.message);

                        if (data.result == true) // Positive reply - switch to open
                            this->cur_state.store(S_OPEN, std::memory_order_relaxed);
                        else // Negative reply -> resend auth msg
                            send_message(this->auth_data);
                        break;
                    case ERR: // Output error and end
                        OutputClass::out_err_server(data.display_name, data.message);
                        send_bye();
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
                        break;
                    case MSG: // Output message
                        OutputClass::out_msg(data.display_name, data.message);
                        break;
                    case ERR: // Output error and send bye
                        OutputClass::out_err_server(data.display_name, data.message);
                        send_bye();
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
                this->cur_state.store(S_END, std::memory_order_relaxed);
                send_bye();
                break;
            case S_START:
            case S_END: // Ignore everything
                break;
            default: // Not expected state, output error
                OutputClass::out_err_intern("Unknown current client state");
                break;
        }
    }
    // End connection when error occured during receiving
    if (err_occured == true)
        session_end();
}
/***********************************************************************************/
std::string TCPClass::load_rest (size_t start_from) {
    std::string out;
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
            out_str.result = ((this->line_vec.at(1) == std::string("OK")) ? true : false);
            out_str.message = load_rest(2);
            break;
        case MSG: // MSG FROM {DisplayName} IS {MessageContent}\r\n
            out_str.display_name = this->line_vec.at(2);
            out_str.message = load_rest(4);
            break;
        case ERR: // ERROR FROM {DisplayName} IS {MessageContent}\r\n
            out_str.display_name = this->line_vec.at(2);
            out_str.message = load_rest(4);
            break;
        default:
            throw std::string("Unknown message type provided");
            break;
    }
    // Check for msg integrity
    if (check_valid_msg<TCP_DataStruct>(out_str.type, out_str) == false)
        throw std::string("Invalid message provided");
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
    case ERR: // ERROR FROM {DisplayName} IS {MessageContent}\r\n
        msg = "ERROR FROM " + data.display_name + " IS " + data.message + "\r\n";
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
