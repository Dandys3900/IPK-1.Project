#include "UDPClass.h"

UDPClass::UDPClass (std::map<std::string, std::string> data_map)
    : msg_id           (-1),
      port             (4567),
      recon_attempts   (3),
      timeout          (250),
      socket_id        (-1),
      retval           (EXIT_SUCCESS),
      server_hostname  (""),
      display_name     ("mock"), // TODO change back to blank IG
      cur_state        (S_START),
      replying_to_id   (-1),
      latest_sent_id   (-1),
      stop_send        (false),
      stop_recv        (false)
{
    std::map<std::string, std::string>::iterator iter;
    // Look for init values in map to override init values
    if ((iter = data_map.find("ipaddr")) != data_map.end())
        this->server_hostname = iter->second;

    if ((iter = data_map.find("port")) != data_map.end())
        this->port = static_cast<uint16_t>(std::stoi(iter->second));

    if ((iter = data_map.find("reconcount")) != data_map.end())
        this->recon_attempts = static_cast<uint8_t>(std::stoi(iter->second));

    if ((iter = data_map.find("timeout")) != data_map.end())
        this->timeout = static_cast<uint16_t>(std::stoi(iter->second));
}
/***********************************************************************************/
void UDPClass::open_connection () {
    // Create UDP socket
    if ((this->socket_id = socket(AF_INET, SOCK_DGRAM, 0)) <= 0)
        throw std::string("UDP socket creation failed");

    struct hostent* server = gethostbyname(this->server_hostname.c_str());
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
    // Store to class
    this->sock_str = server_addr;

    // Set proper timeout
    set_socket_timeout(this->timeout);

    // Create threads for sending and receiving server msgs
    this->send_thread = std::jthread(&UDPClass::handle_send, this);
    this->recv_thread = std::jthread(&UDPClass::handle_receive, this);
}
/***********************************************************************************/
void UDPClass::session_end () {
    std::unique_lock<std::mutex> lock(this->session_end_mutex);
    safePrint("ending session");
    this->stop_send = true;
    // Notify
    this->send_cond_var.notify_one();
    this->stop_recv = true;
    // Change state
    this->cur_state.store(S_END, std::memory_order_relaxed);
    // Close socket
    close(this->socket_id);
    // Exit the program
    exit(this->retval);
}
/***********************************************************************************/
void UDPClass::send_auth (std::string user, std::string display, std::string secret) {
    if (this->cur_state.load(std::memory_order_relaxed) != S_START)
        throw std::string("Can't send auth message outside of start state");

    // Update display name
    send_rename(display);

    UDP_DataStruct data = {
        .header = create_header(AUTH),
        .user_name = user,
        .display_name = display,
        .secret = secret
    };

    safePrint("send_auth called");

    // Move to auth state for handling reply msgs
    this->cur_state.store(S_AUTH, std::memory_order_relaxed);
    send_message(data);
}
/***********************************************************************************/
void UDPClass::send_msg (std::string msg) {
    if (this->cur_state.load(std::memory_order_relaxed) != S_OPEN)
        throw std::string("Can't send message outside of open state");

    UDP_DataStruct data = {
        .header = create_header(MSG),
        .message = msg,
        .display_name = this->display_name
    };
    send_message(data);
}
/***********************************************************************************/
void UDPClass::send_join (std::string channel_id) {
    if (this->cur_state.load(std::memory_order_relaxed) != S_OPEN)
        throw std::string("Can't process join outside of open state");

    UDP_DataStruct data = {
        .header = create_header(JOIN),
        .display_name = this->display_name,
        .channel_id = channel_id
    };
    send_message(data);
}
/***********************************************************************************/
void UDPClass::send_rename (std::string new_display_name) {
    if (new_display_name.length() > DISPLAY_NAME_MAX_LENGTH || !str_printable(new_display_name, false))
        throw std::string("Invalid new value for display name");
    // Update display name
    this->display_name = new_display_name;
}
/***********************************************************************************/
void UDPClass::send_confirm (uint16_t confirm_to_id) {
    UDP_DataStruct data = {
        .header = create_header(CONFIRM),
        .ref_msg_id = confirm_to_id
    };
    send_message(data);
}
/***********************************************************************************/
void UDPClass::send_bye () {
    // Switch to end state
    this->cur_state.store(S_END, std::memory_order_relaxed);
    // Send bye message
    UDP_DataStruct data = {
        .header = create_header(BYE)
    };
    send_message(data);
}
/***********************************************************************************/
void UDPClass::send_err (std::string err_msg) {
    UDP_DataStruct data = {
        .header = create_header(ERR),
        .message = err_msg,
        .display_name = this->display_name
    };
    send_message(data);
}
/***********************************************************************************/
void UDPClass::check_msg_valid (UDP_DataStruct& data) {
    // Each type check for:
        // Check for allowed sizes of params
        // Check for allowed chars used
    switch (data.header.type) {
        case AUTH:
            if (data.user_name.length() > USERNAME_MAX_LENGTH || data.display_name.length() > DISPLAY_NAME_MAX_LENGTH || data.secret.length() > SECRET_MAX_LENGTH)
                throw std::string("Prohibited length of param/s");

            if (!str_alphanums(data.user_name) || !str_alphanums(data.secret) || !str_printable(data.display_name, false))
                throw std::string("Prohibited chars used");

            // Save auth data if resend needed
            this->auth_data = data;
            break;
        case ERR:
            // When having display_name from user side, its already checked, from server side check it now
            if (data.display_name.length() > DISPLAY_NAME_MAX_LENGTH)
                throw std::string("Prohibited length of param");

            if (!str_printable(data.display_name, false))
                throw std::string("Prohibited chars used");
            break;
        case REPLY:
        case MSG:
            if (data.message.length() > MESSAGE_MAX_LENGTH)
                throw std::string("Prohibited length of param");

            if (!str_printable(data.message, true))
                throw std::string("Prohibited chars used");
            break;
        case JOIN:
            if (data.channel_id.length() > CHANNEL_ID_MAX_LENGTH)
                throw std::string("Prohibited length of param");

            if (!str_alphanums(data.channel_id))
                throw std::string("Prohibited chars used");
            break;
        default:
            break;
    }
}
/***********************************************************************************/
void UDPClass::set_socket_timeout (uint16_t timeout /*miliseconds*/) {
    struct timeval time = {
        .tv_sec = 0,
        .tv_usec = suseconds_t(timeout * 1000)
    };
    // Set timeout for created socket
    if (setsockopt(this->socket_id, SOL_SOCKET, SO_RCVTIMEO, (char*)&(time), sizeof(struct timeval)) < 0)
        throw std::string ("Setting receive timeout failed");
}
/***********************************************************************************/
void UDPClass::send_message (UDP_DataStruct data) {
    // Avoid racing between main and response thread
    std::lock_guard<std::mutex> lock(this->editing_front_mutex);

    try { // Check for msg integrity
        check_msg_valid(data);
    } catch (std::string err_msg) {
        // Output error and avoid further message processing
        OutputClass::out_err_intern(err_msg);
        return;
    }

    safePrint("adding msg to queue: " + get_msg_type(data.header.type));
    // Add new message to the queue with given resend count
    this->messages_to_send.push({data, this->recon_attempts});

    // Notify send thread about message to be send
    this->send_cond_var.notify_one();
}
/***********************************************************************************/
void UDPClass::send_data (UDP_DataStruct data) {
    // Store msg_id to avoid multiple sends of the same msg in handle_send
    this->latest_sent_id.store(data.header.msg_id, std::memory_order_relaxed);

    // Prepare data to send
    std::string message = convert_to_string(data);
    const char* out_buffer = message.data();

    safePrint("sending msg type: " + get_msg_type(data.header.type));

    // Send data
    ssize_t bytes_send =
        sendto(this->socket_id, out_buffer, message.size(), 0, (struct sockaddr*)&(this->sock_str), sizeof(this->sock_str));

    // Check for errors
    if (bytes_send <= 0) {
        OutputClass::out_err_intern("Error while sending data to server");
        this->retval = EXIT_FAILURE;
        session_end();
    }
}
/***********************************************************************************/
std::string UDPClass::get_msg_type (uint8_t type) {
    switch (type) {
        case NO_TYPE:
            return std::string("No type");
        case CONFIRM:
            return std::string("Confirm type");
        case REPLY:
            return std::string("Reply type");
        case AUTH:
            return std::string("Auth type");
        case JOIN:
            return std::string("Join type");
        case MSG:
            return std::string("Msg type");
        case ERR:
            return std::string("Err type");
        case BYE:
            return std::string("Bye type");
        default:
            return std::string("No type");
    }
}
/***********************************************************************************/
void UDPClass::safePrint (const string& msg) {
    std::lock_guard<std::mutex> lock(this->print_mutex);
    std::cout << msg << std::endl;
}
/***********************************************************************************/
void UDPClass::handle_send () {
    while (this->stop_send == false) {
        { // Mutex lock scope
            std::unique_lock<std::mutex> lock(this->editing_front_mutex);

            this->send_cond_var.wait(lock, [&] { return (!this->messages_to_send.empty() || this->stop_send); });
            // Stop sending if requested
            if (this->stop_send == true)
                break;

            // Load message to send from queue front
            auto to_send = this->messages_to_send.front();

            // Avoid sending multiple msgs with the same msg_id
            if (to_send.first.header.msg_id != this->latest_sent_id.load(std::memory_order_relaxed)) {
                // Send it to server
                send_data(to_send.first);

                // Confirm message wont be confirmed by server so remove after being sent
                if (to_send.first.header.type == CONFIRM)
                    this->messages_to_send.pop();
                else // Store its id to check for matching reply ref_msg_id from server
                    this->replying_to_id.store(to_send.first.header.msg_id, std::memory_order_relaxed);
            }
        } // Mutex unlocks when getting out of scope
    }
}
/***********************************************************************************/
void UDPClass::thread_event (THREAD_EVENT event, uint16_t confirm_to_id) {
    std::lock_guard<std::mutex> lock(this->editing_front_mutex);
    if (!this->messages_to_send.empty()) {
        auto& front_msg = this->messages_to_send.front();
        // Timeout event occured
        if (event == TIMEOUT) {
            // Decrease front msg resend count
            if (front_msg.second > 1) {
                // Decrease resend count
                front_msg.second -= 1;
                // Update msg_id since in fact considered as new message
                front_msg.first.header = create_header(front_msg.first.header.type);
            }
            else {
                // Confirmed BYE msg from server -> end connection
                if (front_msg.first.header.type == BYE)
                    session_end();

                // Pop it from queue and continue with another message (if any)
                this->messages_to_send.pop();
                if (this->cur_state.load(std::memory_order_relaxed) == S_AUTH)
                    this->cur_state.store(S_START, std::memory_order_relaxed);
            }
        }
        // Confirmation event occured
        if (event == CONFIRMATION && front_msg.first.header.msg_id == confirm_to_id) {
            safePrint("confirmation for queue front msg received");
            // Pop it from queue and continue with another message (if any)
            this->messages_to_send.pop();
            // Confirmed BYE msg -> end connection
            if (front_msg.first.header.type == BYE)
                session_end();
        }
        // Notify waiting thread (if any)
        this->send_cond_var.notify_one();
    }
}
/***********************************************************************************/
void UDPClass::handle_receive () {
    char in_buffer[MAXLENGTH];
    socklen_t sock_len = sizeof(this->sock_str);

    while (this->stop_recv == false) {
        ssize_t bytes_received =
            recvfrom(this->socket_id, (char*)in_buffer, MAXLENGTH, 0, (struct sockaddr*)&this->sock_str, &sock_len);

        // Stop receiving when requested
        if (this->stop_recv == true)
            break;

        if (bytes_received <= 0) {
            if ((errno == EWOULDBLOCK || errno == EAGAIN)) // Timeout event
                thread_event(TIMEOUT);
            continue;
        }

        // Store received data
        UDP_DataStruct data;
        // Load header
        std::memcpy(&data.header, in_buffer, HEADER_SIZE);

        if (data.header.type == CONFIRM) { // Confirmation from server event
            thread_event(CONFIRMATION, data.header.msg_id);
            continue;
        }

        safePrint("RECEIVED TYPE: " + get_msg_type(data.header.type));

        // Send confirmation to the server before processing further
        send_confirm(data.header.msg_id);

        // Check vector of already processed message IDs
        if ((std::find(processed_msgs.begin(), processed_msgs.end(), data.header.msg_id)) == processed_msgs.end()) {
            // Store and mark as proceeded msg ID
            processed_msgs.push_back(data.header.msg_id);

            try {
                deserialize_msg(data, in_buffer);
            } catch (std::string err_msg) {
                // Output error and avoid further message processign
                OutputClass::out_err_intern(err_msg);
                // Invalid message from server -> end connection
                this->retval = EXIT_FAILURE;
                send_err("Invalid message type received");
                send_bye();
                continue;
            }
        }
        else // Ignore and continue
            continue;

        // Process response
        switch (this->cur_state.load(std::memory_order_relaxed)) {
            case S_AUTH:
                switch (data.header.type) {
                    case REPLY:
                        // Replying to unexpected message id
                        if (data.ref_msg_id != this->replying_to_id.load(std::memory_order_relaxed))
                            invalid_reply_id();
                        // Output message
                        OutputClass::out_reply(data.result, data.message);

                        if (data.result == true) // Positive reply - switch to open
                            this->cur_state.store(S_OPEN, std::memory_order_relaxed);
                        else { // Negative reply -> resend auth msg
                            this->auth_data.header = create_header(AUTH);
                            send_message(this->auth_data);
                        }
                        break;
                    case ERR: // Output error and end
                        OutputClass::out_err_server(data.display_name, data.message);
                    default: // End connection
                        send_bye();
                        break;
                }
                break;
            case S_OPEN:
                switch (data.header.type) {
                    case REPLY:
                        // Replying to unexpected message id
                        if (data.ref_msg_id != this->replying_to_id.load(std::memory_order_relaxed))
                            invalid_reply_id();
                        // Output server reply
                        OutputClass::out_reply(data.result, data.message);
                        break;
                    case MSG:
                        // Output message
                        OutputClass::out_msg(this->display_name, data.message);
                        break;
                    case ERR: // Output error and end
                        OutputClass::out_err_server(data.display_name, data.message);
                    case BYE: // End connection
                        send_bye();
                        break;
                    default: // Switch to err state
                        this->cur_state.store(S_ERROR, std::memory_order_relaxed);
                        std::string err_msg = "Unexpected server message";
                        // Notify user
                        OutputClass::out_err_intern(err_msg);
                        // Notify server
                        send_err(err_msg);
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
                OutputClass::out_err_intern("Unknown current state");
                break;
        }
    }
}
/***********************************************************************************/
void UDPClass::invalid_reply_id () {
    std::string err_msg = "Reply message has invalid ref_id";
    // Notify user
    OutputClass::out_err_intern(err_msg);
    // Notify server
    send_err(err_msg);
    // End connection
    send_bye();
}
/***********************************************************************************/
void UDPClass::get_msg_part (const char* input, size_t& input_pos, std::string& store_to) {
    for (size_t pos = 0; input[pos] != '\0'; ++pos, ++input_pos)
        store_to += input[pos];
    // Skip currently found null byte
    ++input_pos;
}

void UDPClass::deserialize_msg (UDP_DataStruct& out_str, const char* msg) {
    size_t msg_pos = HEADER_SIZE;
    switch (out_str.header.type) {
        case REPLY:
            // Result
            std::memcpy(&out_str.result, msg + msg_pos, sizeof(out_str.result));
            msg_pos += sizeof(out_str.result);
            // Ref msg_id
            std::memcpy(&out_str.result, msg + msg_pos, sizeof(out_str.ref_msg_id));
            msg_pos += sizeof(out_str.ref_msg_id);
            // Message
            get_msg_part(msg + msg_pos, msg_pos, out_str.message);
            break;
        case ERR:
        case MSG:
            // Display name
            get_msg_part(msg + msg_pos, msg_pos, out_str.display_name);
            // Message
            get_msg_part(msg + msg_pos, msg_pos, out_str.message);
            break;
        case BYE:
            break;
        default:
            throw std::string("Unknown message type provided");
    }
    // Check for msg integrity
    check_msg_valid(out_str);
}
/***********************************************************************************/
std::string UDPClass::convert_to_string (UDP_DataStruct data) {
    std::string msg(1, static_cast<char>(data.header.type));

    switch (data.header.type) {
        case CONFIRM:
            msg += std::string(2, data.ref_msg_id);
            break;
        case AUTH:
            msg += std::string(2, data.header.msg_id) + data.user_name + '\0' + data.display_name + '\0' + data.secret + '\0';
            break;
        case JOIN:
            msg += std::string(2, data.header.msg_id) + data.channel_id + '\0' + data.display_name + '\0';
            break;
        case MSG:
            msg += std::string(2, data.header.msg_id) + data.display_name + '\0' + data.message + '\0';
            break;
        case ERR:
            msg += std::string(2, data.header.msg_id) + data.display_name + '\0' + data.message + '\0';
            break;
        case BYE:
            msg += std::string(2, data.header.msg_id);
            break;
        default: // Shouldn't happen as type is not user-provided
            break;
    }
    // Return composed message
    return msg;
}
/***********************************************************************************/
UDP_Header UDPClass::create_header (uint8_t type) {
    return (UDP_Header){
        .type   = type,
        .msg_id = ++(this->msg_id)
    };
}
