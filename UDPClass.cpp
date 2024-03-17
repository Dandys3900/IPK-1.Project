#include "UDPClass.h"

UDPClass::UDPClass (std::map<std::string, std::string> data_map)
    : msg_id           (0),
      port             (4567),
      recon_attempts   (3),
      timeout          (250),
      socket_id        (-1),
      retval           (EXIT_SUCCESS),
      server_hostname  (""),
      display_name     (""),
      cur_state        (S_START),
      replying_to_id   (0),
      latest_sent_id   (1), // Initially must be different from msg_id (0)
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
    // Switch to err state
    this->cur_state.store(S_ERROR, std::memory_order_relaxed);

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
    try { // Check for msg integrity
        check_msg_valid(data);
    } catch (std::string err_msg) {
        // Output error and avoid further message processing
        OutputClass::out_err_intern(err_msg);
        return;
    }

    {   // Avoid racing between main and response thread
        std::lock_guard<std::mutex> lock(this->editing_front_mutex);
        // Add new message to the queue with given resend count
        this->messages_to_send.push({data, this->recon_attempts});
    }
    // Notify send thread about new message to be send
    this->send_cond_var.notify_one();
}
/***********************************************************************************/
void UDPClass::send_data (UDP_DataStruct& data) {
    // Store msg_id to avoid multiple sends of the same msg in handle_send
    this->latest_sent_id.store(data.header.msg_id, std::memory_order_relaxed);

    // Prepare data to send
    std::string message = convert_to_string(data);
    const char* out_buffer = message.data();

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
    // Skip if messages queue is empty as nothing to deal with
    if (!this->messages_to_send.empty()) {
        auto& front_msg = this->messages_to_send.front();
        if (event == TIMEOUT) { // Timeout event occured
            // Decrease front msg resend count
            if (front_msg.second > 1) {
                // Decrease resend count
                front_msg.second -= 1;
                // Ensure msg_id uniqueness by changing it each time
                front_msg.first.header = create_header(front_msg.first.header.type);
                this->replying_to_id.store(front_msg.first.header.msg_id, std::memory_order_relaxed);
            }
            else { // Pop it from queue and continue with another message (if any)
                this->messages_to_send.pop();
                // BYE msg to server timeouted -> end connection
                if (front_msg.first.header.type == BYE)
                    session_end();
            }
        }
        else if (event == CONFIRMATION) { // Confirmation event occured
            if (front_msg.first.header.msg_id == confirm_to_id) { // Pop it from queue and continue with another message (if any)
                this->messages_to_send.pop();
                // Confirmed BYE msg -> end connection
                if (front_msg.first.header.type == BYE)
                    session_end();
            }
            else
                OutputClass::out_err_intern("Confirmation to unexpected message received");
        }
        // Notify waiting thread (if any)
        this->send_cond_var.notify_one();
    }
    else if (event == TIMEOUT) { // Nothing is received, nothing to send -> move to end state
        if (this->cur_state.load(std::memory_order_relaxed) == S_AUTH || this->cur_state.load(std::memory_order_relaxed) == S_ERROR)
            send_bye();
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

        if (bytes_received < 0) {
            if ((errno == EWOULDBLOCK || errno == EAGAIN)) // Timeout event
                thread_event(TIMEOUT);
            else // Output error
                OutputClass::out_err_intern(std::strerror(errno));
            continue;
        }
        else if (bytes_received == 0) // No reason for processing zero response
            continue;

        // Store received data
        UDP_DataStruct data;
        // Load message header
        std::memcpy(&data.header, in_buffer, sizeof(UDP_Header));
        data.header.msg_id = htons(data.header.msg_id);

        if (data.header.type == CONFIRM) { // Confirmation from server event
            thread_event(CONFIRMATION, data.header.msg_id);
            continue;
        }

        // Send confirmation to the server before processing received message
        send_confirm(data.header.msg_id);

        if ((std::find(processed_msgs.begin(), processed_msgs.end(), data.header.msg_id)) != processed_msgs.end())
            // Ignore and continue as already processed
            continue;

        // Store and mark as proceeded msg ID
        processed_msgs.push_back(data.header.msg_id);

        try {
            deserialize_msg(data, in_buffer);
            data.ref_msg_id = htons(data.ref_msg_id);
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
                switch (data.header.type) {
                    case REPLY:
                        // Replying to unexpected message id
                        if (data.ref_msg_id != this->replying_to_id.load(std::memory_order_relaxed)) {
                            switch_to_error("Reply message has invalid ref_id");
                            // End session
                            session_end();
                            break;
                        }
                        // Output message
                        OutputClass::out_reply(data.result, data.message);

                        if (data.result == true) // Positive reply - switch to open
                            this->cur_state.store(S_OPEN, std::memory_order_relaxed);
                        else { // Negative reply -> resend auth msg
                            {   // If the message is still at queue front, it hasnt been confirmed by server yet, skip
                                std::lock_guard<std::mutex> lock(this->editing_front_mutex);
                                if (!this->messages_to_send.empty() && data.ref_msg_id == this->messages_to_send.front().first.header.msg_id)
                                    continue;
                            }
                            this->auth_data.header = create_header(AUTH);
                            send_message(this->auth_data);
                        }
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
                switch (data.header.type) {
                    case REPLY:
                        // Replying to unexpected message id
                        if (data.ref_msg_id != this->replying_to_id.load(std::memory_order_relaxed)) {
                            switch_to_error("Reply message has invalid ref_id");
                            // End session
                            session_end();
                            break;
                        }
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
}
/***********************************************************************************/
void UDPClass::switch_to_error (std::string err_msg) {
    // Notify user
    OutputClass::out_err_intern(err_msg);
    // Notify server
    send_err(err_msg);
}
/***********************************************************************************/
void UDPClass::get_msg_part (const char* input, size_t& input_pos, std::string& store_to) {
    for (size_t pos = 0; input[pos] != '\0'; ++pos, ++input_pos)
        store_to += input[pos];
    // Skip currently found null byte
    ++input_pos;
}

void UDPClass::deserialize_msg (UDP_DataStruct& out_str, const char* msg) {
    size_t msg_pos = sizeof(UDP_Header);
    switch (out_str.header.type) {
        case REPLY:
            // Result
            std::memcpy(&out_str.result, msg + msg_pos, sizeof(out_str.result));
            msg_pos += sizeof(out_str.result);
            // Ref msg_id
            std::memcpy(&out_str.ref_msg_id, msg + msg_pos, sizeof(out_str.ref_msg_id));
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
std::string UDPClass::get_str_msg_id (uint16_t msg_id) {
    std::string retval = "";
    // Extract the individual bytes
    char high_byte = static_cast<char>((msg_id >> 8) & 0xFF);
    char low_byte  = static_cast<char>(msg_id & 0xFF);

    // Append the bytes
    retval += high_byte;
    retval += low_byte;

    return retval;
}
/***********************************************************************************/
std::string UDPClass::convert_to_string (UDP_DataStruct& data) {
    std::string msg(1, static_cast<char>(data.header.type));
    /*
        todo:
            ctrl +d crash (unsolved mystery)
            cash messages when waiting for reply in auth state and send them afterwards
            the end state, not ending session when going to it?
            get back to main after session end to clear memory
            should we check if received message contains all necessary parts?
    */
    switch (data.header.type) {
        case CONFIRM:
            msg += get_str_msg_id(data.ref_msg_id);
            break;
        case AUTH:
            msg += get_str_msg_id(data.header.msg_id) + data.user_name + '\0' + data.display_name + '\0' + data.secret + '\0';
            break;
        case JOIN:
            msg += get_str_msg_id(data.header.msg_id) + data.channel_id + '\0' + data.display_name + '\0';
            break;
        case MSG:
            msg += get_str_msg_id(data.header.msg_id) + data.display_name + '\0' + data.message + '\0';
            break;
        case ERR:
            msg += get_str_msg_id(data.header.msg_id) + data.display_name + '\0' + data.message + '\0';
            break;
        case BYE:
            msg += get_str_msg_id(data.header.msg_id);
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
        .msg_id = (this->msg_id)++
    };
}
