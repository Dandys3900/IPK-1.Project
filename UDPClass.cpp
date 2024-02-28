#include "UDPClass.h"

UDPClass::UDPClass (std::map<std::string, std::string> data_map)
    : msg_id          (0),
      port            (4567),
      server_hostname (),
      socket_id       (-1),
      recon_attempts  (3),
      timeout         (250),
      display_name    (),
      cur_state       (S_START),
      sock_str        ()
{
    std::map<std::string, std::string>::iterator iter;
    // Look for init values in map to override init values
    if ((iter = data_map.find("ipaddr")) != data_map.end())
        this->server_hostname = iter->second;

    if ((iter = data_map.find("port")) != data_map.end())
        this->port = uint16_t{std::stoi(iter->second)};

    if ((iter = data_map.find("reconcount")) != data_map.end())
        this->recon_attempts = uint8_t{std::stoi(iter->second)};

    if ((iter = data_map.find("timeout")) != data_map.end())
        this->timeout = uint16_t{std::stoi(iter->second)};
}

UDPClass::~UDPClass ()
{
}

/***********************************************************************************/
void UDPClass::open_connection () {
    // Create UDP socket
    if ((this->socket_id = socket(AF_INET, SOCK_DGRAM, 0)) <= 0) {
        OutputClass::out_err_intern("UDP socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct hostent* server = gethostbyname(this->server_hostname.c_str());
    if (!server) {
        OutputClass::out_err_intern("Unknown or invalid hostname provided");
        exit(EXIT_FAILURE);
    }
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
}
/***********************************************************************************/
void UDPClass::session_end () {
    // Change state
    cur_state = S_END;
    // Send bye message
    handle_send(BYE, (UDP_DataStruct){.type = BYE,
                                  .msg_id = (this->msg_id)++});
    // Close socket
    close(this->socket_id);
}
/***********************************************************************************/
void UDPClass::send_auth (UDP_DataStruct cmd_data) {
    if (cur_state != S_START)
        throw ("Can't send message outside of open state");

    // Check for allowed sizes of params
    if (cmd_data.user_name.length() > USERNAME_MAX_LENGTH ||
        cmd_data.display_name.length() > DISPLAY_NAME_MAX_LENGTH ||
        cmd_data.secret.length() > SECRET_MAX_LENGTH)
    {
        OutputClass::out_err_intern("Prohibited length of param/s");
        return;
    }

    // Update displayname
    this->display_name = cmd_data.display_name;

    // Each message being send must have msg_id and its type set in advance
    cmd_data.type = AUTH;
    cmd_data.msg_id = (this->msg_id)++;

    // Save auth data in case of resend
    this->auth_data = cmd_data;
    // Move to auth state
    cur_state = S_AUTH;
    handle_send(AUTH, cmd_data);
}

void UDPClass::send_msg (std::string msg) {
    if (cur_state != S_OPEN)
        throw ("Can't send message outside of open state");

    // Check for allowed sizes of params
    if (msg.length() > MESSAGE_MAX_LENGTH) {
        OutputClass::out_err_intern("Prohibited length of param/s");
        return;
    }

    handle_send(MSG, (UDP_DataStruct){.type = MSG,
                                  .msg_id = (this->msg_id)++,
                                  .message = msg,
                                  .display_name = this->display_name});
}

void UDPClass::send_join (std::string channel_id) {
    if (cur_state != S_OPEN)
        throw ("Can't process join outside of open state");

    // Check for allowed sizes of params
    if (channel_id.length() > CHANNEL_ID_MAX_LENGTH) {
        OutputClass::out_err_intern("Prohibited length of param/s");
        return;
    }

    handle_send(JOIN, (UDP_DataStruct){.type = JOIN,
                                   .msg_id = (this->msg_id)++,
                                   .display_name = this->display_name,
                                   .channel_id = channel_id});
}

void UDPClass::send_rename (std::string new_display_name) {
    if (new_display_name.length() > DISPLAY_NAME_MAX_LENGTH)
        OutputClass::out_err_intern("Prohibited length of param/s");
    else
        this->display_name = new_display_name;
}

void UDPClass::send_confirm () {
    handle_send(CONFIRM, (UDP_DataStruct){.type = CONFIRM,
                                      .ref_msg_id = this->msg_id});
}
void UDPClass::send_bye () {
    session_end();
}
/***********************************************************************************/
void UDPClass::handle_send (uint8_t msg_type, UDP_DataStruct send_data) {
    for (size_t atmpts = 0; atmpts < this->recon_attempts; ++atmpts) {
        try {
            // Send message to server
            sendData(msg_type, send_data);
            // Expect confirmation
            if (msg_type != CONFIRM && !wait_for_confirmation(this->msg_id))
                continue;
            if (msg_type != CONFIRM && msg_type != BYE) {
                // Reset timeout
                set_socket_timeout(0);
                receive();
            }
            break;
        }
        catch (std::string err_msg) {
            OutputClass::out_err_intern(err_msg);
            // Repeat, if possible
            continue;
        }
    }
}
/***********************************************************************************/
void UDPClass::set_socket_timeout (uint16_t timeout /*miliseconds*/) {
    struct timeval time = {
        .tv_sec = 0,
        .tv_usec = suseconds_t(timeout * 1000)
    };

    if (setsockopt(this->socket_id, SOL_SOCKET, SO_RCVTIMEO,
        (char*)&(time), sizeof(struct timeval)) < 0)
    {
        OutputClass::out_err_intern("Setting receive timeout failed");
    }
}
/***********************************************************************************/
void UDPClass::sendData (uint8_t type, UDP_DataStruct send_data) {
    // Prepare data to send
    std::string message = convert_to_string(type, send_data);
    const char* out_buffer = message.c_str();

    // Send data
    ssize_t bytes_send =
        sendto(this->socket_id, out_buffer, strlen(out_buffer), 0, (struct sockaddr*)&(this->sock_str), sizeof(this->sock_str));

    // Check for errors
    if (bytes_send < 0) {
        OutputClass::out_err_intern("Error while sending data to server");
        session_end();
        return;
    }

    // We expect confirm message, set socket timeout for that (miliseconds)
    set_socket_timeout(250);
}
/***********************************************************************************/
bool UDPClass::wait_for_confirmation (const uint16_t exp_msg_id) {
    char in_buffer[MAXLENGTH];
    socklen_t sock_len;

    while (true) {
        ssize_t bytes_received =
            recvfrom(socket_id, (char*)in_buffer, MAXLENGTH, 0, (struct sockaddr*)&(this->sock_str), &sock_len);
        // Timeout occured
        if (bytes_received <= 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
            return false;

        in_buffer[bytes_received] = '\0';
        std::string response(in_buffer);
        // Extract message type - 1 BYTE
        uint8_t msg_type = std::stoi(response.substr(0, 1));
        // Extract message ID - 2 BYTES
        uint16_t msg_id = std::stoi(response.substr(1, 2));

        // Case of confirm message, but with wrong ref_msg_id
        if (msg_type == CONFIRM && msg_id != exp_msg_id)
            continue;
        return true;
    }
}
/***********************************************************************************/
void UDPClass::receive () {
    char in_buffer[MAXLENGTH];
    socklen_t sock_len;

    while (true) {
        ssize_t bytes_received =
            recvfrom(socket_id, in_buffer, MAXLENGTH, 0, (struct sockaddr*)&(this->sock_str), &sock_len);

        // Check for errors
        if (bytes_received <= 0) {
            OutputClass::out_err_intern("Error while receiving data from server");
            session_end();
            return;
        }

        in_buffer[bytes_received] = '\0';
        std::string response(in_buffer);
        // Extract message type - 1 BYTE
        uint8_t msg_type = std::stoi(response.substr(0, 1));
        // Extract message ID - 2 BYTES
        uint16_t msg_id = std::stoi(response.substr(1, 2));

        // Check vector of already processed message IDs
        if ((std::find(processed_msgs.begin(), processed_msgs.end(), msg_id)) == processed_msgs.end()) {
            processed_msgs.push_back(msg_id);
            UDP_DataStruct data = deserialize_msg(msg_type, msg_id, response);
            proces_response(msg_type, data);
        }
        else // Only send confirmation to sender
            send_confirm();
        break;
    }
}
/***********************************************************************************/
void UDPClass::proces_response (uint8_t resp, UDP_DataStruct& resp_data) {
    switch (cur_state) {
        case S_AUTH:
            switch (resp) {
                case REPLY:
                    // Output message
                    OutputClass::out_reply(resp_data.result, resp_data.message);
                    // Check if negative or positive reply
                    if (resp_data.result) {
                        // Change state
                        cur_state = S_OPEN;
                        send_confirm();
                    }
                    else {
                        // Resend auth msg
                        handle_send(AUTH, this->auth_data);
                    }
                    break;
                default:
                    // Switch to end state
                    cur_state = S_END;
                    send_bye();
                    break;
            }
            break;
        case S_OPEN:
            switch (resp) {
                case REPLY:
                    // Output server reply
                    OutputClass::out_reply(resp_data.result, resp_data.message);
                    send_confirm();
                    break;
                case MSG:
                    // Output message
                    OutputClass::out_msg(this->display_name, resp_data.message);
                    send_confirm();
                    break;
                case ERR:
                    send_bye();
                case BYE:
                    // Switch to end state
                    cur_state = S_END;
                    break;
                default:
                    // Switch to err state
                    cur_state = S_ERROR;
                    handle_send(ERR, (UDP_DataStruct){.type = ERR,
                                                  .msg_id = (this->msg_id)++,
                                                  .message = "Unexpected server message",
                                                  .display_name = this->display_name});
                    break;
            }
            break;
        case S_ERROR:
            // Switch to end state
            cur_state = S_END;
            send_bye();
            break;
        case S_END:
            // Ignore
            break;
        default:
            // Not expected state, output error
            OutputClass::out_err_intern("Unknown current state");
            break;
    }
}
/***********************************************************************************/
UDP_DataStruct UDPClass::deserialize_msg (uint8_t msg_type, uint16_t msg_id, std::string msg) {
    UDP_DataStruct out;
    out.type = msg_type;

    size_t null_char_pos;
    // Keeps track of current position in msg from where value is loaded
    size_t pos_in_msg = 0;
    pos_in_msg += sizeof(out.type);

    switch (msg_type) {
        case CONFIRM:
            out.ref_msg_id = msg_id; // Message ID
            break;
        case REPLY:
            out.msg_id = msg_id; // Message ID
            pos_in_msg += sizeof(out.msg_id);
            // Result
            out.result = std::stoi(msg.substr(pos_in_msg, 1));
            pos_in_msg += sizeof(out.result);
            // Reference message ID
            out.ref_msg_id = std::stoi(msg.substr(pos_in_msg, 2));
            pos_in_msg += sizeof(out.ref_msg_id);
            // Message
            out.message = msg.substr(pos_in_msg);
            break;
        case MSG:
            out.msg_id = msg_id; // Message ID
            pos_in_msg += sizeof(out.msg_id);
            // Display name
            null_char_pos = msg.find_first_of('\0');
            out.display_name = msg.substr(pos_in_msg, (null_char_pos - pos_in_msg));
            pos_in_msg += out.display_name.length();
            // Message
            out.message = msg.substr(pos_in_msg);
            break;
        case ERR:
            out.msg_id = msg_id; // Message ID
            pos_in_msg += sizeof(out.msg_id);
            // Display name
            null_char_pos = msg.find_first_of('\0');
            out.display_name = msg.substr(pos_in_msg, (null_char_pos - pos_in_msg));
            pos_in_msg += out.display_name.length();
            // Message
            out.message = msg.substr(pos_in_msg);
            break;
        default:
            OutputClass::out_err_intern("Unknown message type provided");
            break;
    }
    // Return deserialized message
    return out;
}
/***********************************************************************************/
std::string UDPClass::convert_to_string (uint8_t type, UDP_DataStruct& data) {
    std::string msg;

    switch (type) {
        case CONFIRM:
            msg = std::to_string(data.type) + std::to_string(data.ref_msg_id);
            break;
        case AUTH:
            msg = std::to_string(data.type) + std::to_string(data.msg_id) + data.user_name + '\0' + this->display_name + '\0' + data.secret + '\0';
            break;
        case JOIN:
            msg = std::to_string(data.type) + std::to_string(data.msg_id) + data.channel_id + '\0' + this->display_name + '\0';
            break;
        case MSG:
            msg = std::to_string(data.type) + std::to_string(data.msg_id) + this->display_name + '\0' + data.message + '\0';
            break;
        case ERR:
            msg = std::to_string(data.type) + std::to_string(data.msg_id) + this->display_name + '\0' + data.message + '\0';
            break;
        case BYE:
            msg = std::to_string(data.type) + std::to_string(data.msg_id);
            break;
        default:
            OutputClass::out_err_intern("Unknown message type provided");
            break;
    }
    // Return composed message
    return msg;
}
