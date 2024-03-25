#include "UDPClass.h"

UDPClass::UDPClass (std::map<std::string, std::string> data_map)
    : ClientClass      (),
      msg_id           (0),
      recon_attempts   (3),
      timeout          (250),
      latest_sent_id   (1) // Initially must be different from msg_id (0)
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

    this->display_name = "pokus";
}
/***********************************************************************************/
void UDPClass::open_connection () {
    // Create UDP socket
    if ((this->socket_id = socket(AF_INET, SOCK_DGRAM, 0)) <= 0)
        throw std::logic_error("UDP socket creation failed");

    struct hostent* server = gethostbyname(this->server_hostname.c_str());
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
    this->stop_recv = true;
    this->stop_send = true;
    // Notify send thread
    this->send_cond_var.notify_one();
    // Change state
    this->cur_state = S_END;
    // Close socket
    close(this->socket_id);
    // Exit the program by notifying main function
    this->end_program = true;
    this->cond_var.notify_one();
}
/***********************************************************************************/
void UDPClass::send_auth (std::string user, std::string display, std::string secret) {
    // Update display name
    if (send_rename(display) == false)
        return;

    UDP_DataStruct data = {
        .header = create_header(AUTH),
        .user_name = user,
        .display_name = display,
        .secret = secret
    };
    send_message(data);
}
/***********************************************************************************/
void UDPClass::send_msg (std::string msg) {
    UDP_DataStruct data = {
        .header = create_header(MSG),
        .message = msg,
        .display_name = this->display_name
    };
    send_message(data);
}
/***********************************************************************************/
void UDPClass::send_join (std::string channel_id) {
    UDP_DataStruct data = {
        .header = create_header(JOIN),
        .display_name = this->display_name,
        .channel_id = channel_id
    };
    send_message(data);
}
/***********************************************************************************/
bool UDPClass::send_rename (std::string new_display_name) {
    if (regex_match(new_display_name, display_name_pattern) == false) {
        OutputClass::out_err_intern("Invalid new value for display name");
        return false;
    }
    // Update display name
    this->display_name = new_display_name;
    return true;
}
/***********************************************************************************/
void UDPClass::send_confirm (uint16_t confirm_to_id) {
    UDP_DataStruct data = {
        .header = create_header(CONFIRM),
        .ref_msg_id = confirm_to_id
    };
    // Send confirm immediately
    send_data(data);
}
/***********************************************************************************/
void UDPClass::send_bye () {
    // Send bye message
    UDP_DataStruct data = {
        .header = create_header(BYE)
    };
    send_message(data);
}
/***********************************************************************************/
void UDPClass::send_priority_bye () {
    this->high_priority = true;
    // Switch to END state
    this->cur_state = S_END;
    send_bye();
}
/***********************************************************************************/
void UDPClass::send_err (std::string err_msg) {
    // Switch to err state
    this->cur_state = S_ERROR;

    UDP_DataStruct data = {
        .header = create_header(ERR),
        .message = err_msg,
        .display_name = this->display_name
    };
    send_message(data);
}
/***********************************************************************************/
void UDPClass::set_socket_timeout (uint16_t timeout /*miliseconds*/) {
    struct timeval time = {
        .tv_sec = 0,
        .tv_usec = suseconds_t(timeout * 1000)
    };
    // Set timeout for created socket
    if (setsockopt(this->socket_id, SOL_SOCKET, SO_RCVTIMEO, (char*)&(time), sizeof(struct timeval)) < 0)
        throw std::logic_error("Setting receive timeout failed");
}
/***********************************************************************************/
void UDPClass::send_message (UDP_DataStruct data) {
    // Check for message validity
    if (check_valid_msg<UDP_DataStruct>(data.header.type, data) == false) {
        OutputClass::out_err_intern("Invalid content of message provided, wont send");
        return;
    }

    std::cout << "ADDING MSG TO QUEUE: " << std::to_string(data.header.type) << std::endl;

    // Avoid racing between main and response thread
    {
        std::lock_guard<std::mutex> lock(this->editing_front_mutex);
        if (this->high_priority == true) {
            this->high_priority = false;
            // Erase the queue and place there the BYE msg
            this->messages_to_send = {};
            this->messages_to_send.push({data, this->recon_attempts});
        }
        else // Add new message to the queue with given resend count
            this->messages_to_send.push({data, this->recon_attempts});
    }
}
/***********************************************************************************/
void UDPClass::send_data (UDP_DataStruct& data) {
    // Store msg_id to avoid multiple sends of the same msg in handle_send
    this->latest_sent_id = data.header.msg_id;

    // Prepare data to send
    std::string message = convert_to_string(data);
    const char* out_buffer = message.data();

    std::cout << "SENDING MSG: " << std::to_string(data.header.type) << std::endl;

    // Send data
    ssize_t bytes_send =
        sendto(this->socket_id, out_buffer, message.size(), 0, (struct sockaddr*)&(this->sock_str), sizeof(this->sock_str));

    // Check for errors
    if (bytes_send < 0)
        OutputClass::out_err_intern("Error while sending data to server");
}
/***********************************************************************************/
void UDPClass::handle_send () {
    while (this->stop_send == false) {
        std::unique_lock<std::mutex> sendlock(this->send_mutex);
        this->send_cond_var.wait(sendlock, [&] { return (!this->messages_to_send.empty() || this->stop_send); });

        // Avoid racing when reading from queue
        std::unique_lock<std::mutex> lock(this->editing_front_mutex);

        if (this->messages_to_send.empty() == true || this->cur_state == S_AUTH_CONFD)
            continue;

        // Stop sending if requested
        if (this->stop_send == true)
            break;

        // Load message to send from queue front
        auto to_send = this->messages_to_send.front();

        // Avoid sending multiple msgs with the same msg_id
        if (to_send.first.header.msg_id != this->latest_sent_id) {
            // Check if given message can be send in client's current state
            if (check_msg_context(to_send.first.header.type, this->cur_state) == false) {
                OutputClass::out_err_intern("Sending this type of message is prohibited for current client state");
                // Remove this message from queue
                this->messages_to_send.pop();
                continue;
            }

            // Send it to server
            send_data(to_send.first);

            // Store its id to check for matching reply ref_msg_id from server
            this->to_reply_ids.push_back(to_send.first.header.msg_id);
        }
    }
}
/***********************************************************************************/
void UDPClass::thread_event (THREAD_EVENT event, uint16_t confirm_to_id) {
    bool no_server_response = false;
    // Timeout happened when waiting for REPLY in confirmed AUTH state -> end connection
    if (event == TIMEOUT && this->cur_state == S_AUTH_CONFD)
        no_server_response = true;

    {
        std::lock_guard<std::mutex> lock(this->editing_front_mutex);
        if (this->messages_to_send.empty() == false && no_server_response == false) {
            // Skip if messages queue is empty as nothing to deal with
            auto& front_msg = this->messages_to_send.front();

            // There is/are msgs in queue, but not send yet, ignore and avoid their resend count decrement
            if (front_msg.first.header.msg_id != this->latest_sent_id) {}
            else if (event == TIMEOUT) { // Timeout event occured
                std::cout << "TIMEOUT EVENT" << std::endl;
                // Decrease front msg resend count
                if (front_msg.second > 1) {
                    // Decrease resend count
                    front_msg.second -= 1;
                    // Ensure msg_id uniqueness by changing it each time
                    front_msg.first.header = create_header(front_msg.first.header.type);
                }
                else { // Pop it from queue and continue with another message (if any)
                    // BYE msg to server timeouted -> end connection
                    if (front_msg.first.header.type == BYE)
                        session_end();
                    else {
                        // Remove message as lost in transmit
                        this->messages_to_send.pop();
                        // No response from server, send BYE and move to END state
                        if (this->cur_state == S_AUTH || this->cur_state == S_ERROR)
                            no_server_response = true;
                    }
                }
            }
            else if (event == CONFIRMATION) { // Confirmation event occured
                if (front_msg.first.header.msg_id == confirm_to_id) { // Pop it from queue and continue with another message (if any)
                    // Confirmed BYE msg -> end connection
                    std::cout << "MESSAGE CONFIRMED" << std::endl;
                    if (front_msg.first.header.type == BYE)
                        session_end();
                    else {
                        if (front_msg.first.header.type == AUTH) {
                            this->cur_state = S_AUTH_CONFD;
                            this->messages_to_send.pop();
                            return;
                        }
                        this->messages_to_send.pop();
                    }
                }
                else
                    OutputClass::out_err_intern("Confirmation to unexpected message received");
            }
        }
    }
    // Corresponds to the FMS when going from AUTH/ERROR -> BYE, the "_/BYE" option
    if (no_server_response)
        send_priority_bye();
    // Notify waiting thread (if any)
    this->send_cond_var.notify_one();
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

        if (bytes_received < 3) { // Smaller then compulsory header size (3B)
            if ((errno == EWOULDBLOCK || errno == EAGAIN)) // Timeout event
                thread_event(TIMEOUT);
            else if (bytes_received < 0) // Output error
                OutputClass::out_err_intern("Error while receiving data from server");
            else { // 0 <= size < 3 -> send ERR, BYE and end connection
                std::string err_msg = "Unsufficient lenght of message received";
                OutputClass::out_err_intern(err_msg);
                // Invalid message from server -> end connection
                send_err(err_msg);
                send_priority_bye();
            }
            // No reason for processing unsufficient-size response, repeat
            continue;
        }

        // Store received data
        UDP_DataStruct data;
        // Load message header
        std::memcpy(&data.header, in_buffer, sizeof(UDP_Header));
        // Convert msg_id to correct indian
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
            deserialize_msg(data, in_buffer, bytes_received);
        } catch (const std::logic_error& e) {
            // Output error and avoid further message processing
            OutputClass::out_err_intern(e.what());
            // Invalid message from server -> end connection
            send_err(e.what());
            send_priority_bye();
            continue;
        }

        // Process response
        switch (this->cur_state) {
            case S_AUTH: // AUTH not confirmed yet, continue
                continue;
            case S_AUTH_CONFD:
                switch (data.header.type) {
                    case REPLY:
                        // Replying to unexpected message id
                        if ((std::find(to_reply_ids.begin(), to_reply_ids.end(), data.ref_msg_id)) == to_reply_ids.end()) {
                            switch_to_error("Reply message has invalid ref_id");
                            // End session
                            send_priority_bye();
                            break;
                        }
                        // Output message
                        OutputClass::out_reply(data.result, data.message);

                        if (data.result == true) // Positive reply - switch to open
                            this->cur_state = S_OPEN;

                        this->send_cond_var.notify_one();
                        // Negative reply -> stay in AUTH state and allow user to re-authenticate
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
                switch (data.header.type) {
                    case REPLY:
                        // Replying to unexpected message id
                        if ((std::find(to_reply_ids.begin(), to_reply_ids.end(), data.ref_msg_id)) == to_reply_ids.end()) {
                            switch_to_error("Reply message has invalid ref_id");
                            // End session
                            send_priority_bye();
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
                        send_priority_bye();
                        break;
                    case BYE: // End connection
                        session_end();
                        return;
                    default: // Transition to error state
                        switch_to_error("Unexpected message received");
                        break;
                }
                break;
            case S_ERROR: // Switch to end state
                this->cur_state = S_END;
                send_priority_bye();
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
void UDPClass::get_msg_part (const char* input, size_t& input_pos, size_t max_size, std::string& store_to) {
    for (size_t pos = 0; pos < max_size && input[pos] != '\0'; ++pos, ++input_pos)
        store_to += input[pos];
    // Skip currently found null byte
    ++input_pos;
}

void UDPClass::deserialize_msg (UDP_DataStruct& out_str, const char* msg, size_t total_size) {
    std::cout << "DESERIALIZING MSG " << std::to_string(out_str.header.type) << std::endl;
    size_t msg_pos = sizeof(UDP_Header);
    switch (out_str.header.type) {
        case REPLY:
            if (total_size < 6) // Not enough for loading compulsory result (1B) + ref_msg_id (2B)
                throw std::logic_error("Unsufficient lenght of REPLY message received");
            // Result
            std::memcpy(&out_str.result, msg + msg_pos, sizeof(out_str.result));
            msg_pos += sizeof(out_str.result);
            // Ref msg_id
            std::memcpy(&out_str.ref_msg_id, msg + msg_pos, sizeof(out_str.ref_msg_id));
            msg_pos += sizeof(out_str.ref_msg_id);
            // Message
            get_msg_part(msg + msg_pos, msg_pos, (total_size - msg_pos), out_str.message);
            break;
        case ERR:
        case MSG:
            // Display name
            get_msg_part(msg + msg_pos, msg_pos, (total_size - msg_pos), out_str.display_name);
            // Message
            get_msg_part(msg + msg_pos, msg_pos, (total_size - msg_pos), out_str.message);
            break;
        case BYE:
            break;
        default:
            throw std::logic_error("Unknown message type provided");
    }
    // Check for msg integrity
    if (check_valid_msg<UDP_DataStruct>(out_str.header.type, out_str) == false)
        throw std::logic_error("Invalid message provided");

    // Convert ref_msg_id to correct indian
    out_str.ref_msg_id = htons(out_str.ref_msg_id);
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
    // Return constructed string holding correct value of msg_id
    return retval;
}
/***********************************************************************************/
std::string UDPClass::convert_to_string (UDP_DataStruct& data) {
    std::string msg(1, static_cast<char>(data.header.type));
    /*
        todo:
            sledovat pocty bye poslanych pro ruzne cases
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
