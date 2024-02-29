#include "TCPClass.h"

TCPClass::TCPClass (std::map<std::string, std::string> data_map)
    : port            (4567),
      server_hostname (),
      socket_id       (-1),
      display_name    (),
      cur_state       (S_START),
      recv_thread     (),
      stop_recv       (false),
      line_vec        ()
{
    std::map<std::string, std::string>::iterator iter;
    // Look for init values in map to override init values
    if ((iter = data_map.find("ipaddr")) != data_map.end())
        this->server_hostname = iter->second;

    if ((iter = data_map.find("port")) != data_map.end())
        this->port = uint16_t{std::stoi(iter->second)};
}

/***********************************************************************************/
void TCPClass::open_connection () {
    // Create TCP socket
    if ((this->socket_id = socket(AF_INET, SOCK_STREAM, 0)) <= 0)
        throw ("TCP socket creation failed");

    struct hostent* server = gethostbyname(this->server_hostname.c_str());
    if (!server)
        throw ("Unknown or invalid hostname provided");

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
        session_end();
        throw ("Error connecting to TCP server");
    }

    // Create thread to listen to server msgs
    this->recv_thread = std::thread(&TCPClass::receive, this);
}
/***********************************************************************************/
void TCPClass::session_end () {
    this->stop_recv = true;
    // Stop the thread
    this->recv_thread.join();
    // Change state
    cur_state = S_END;

    shutdown(this->socket_id, SHUT_RD);
    shutdown(this->socket_id, SHUT_WR);
    shutdown(this->socket_id, SHUT_RDWR);
    // Close socket
    close(this->socket_id);
}
/***********************************************************************************/
void TCPClass::send_auth (std::string user_name, std::string display_name, std::string secret) {
    if (cur_state != S_START)
        throw ("Can't sendData message outside of open state");

    // Check for allowed sizes of params
    if (user_name.length() > USERNAME_MAX_LENGTH || display_name.length() > DISPLAY_NAME_MAX_LENGTH || secret.length() > SECRET_MAX_LENGTH)
        throw ("Prohibited length of param/s");

    // Update displayname
    this->display_name = display_name;

    // Move to auth state
    cur_state = S_AUTH;
    sendData(AUTH, (TCP_DataStruct){.user_name = user_name,
                                    .display_name = display_name,
                                    .secret = secret});
}

void TCPClass::send_msg (std::string msg) {
    if (cur_state != S_OPEN)
        throw ("Can't sendData message outside of open state");

    // Check for allowed sizes of params
    if (msg.length() > MESSAGE_MAX_LENGTH)
        throw  ("Prohibited length of param/s");

    sendData(MSG, (TCP_DataStruct){.message = msg,
                                   .display_name = this->display_name});
}

void TCPClass::send_join (std::string channel_id) {
    if (cur_state != S_OPEN)
        throw ("Can't process join outside of open state");

    // Check for allowed sizes of params
    if (channel_id.length() > CHANNEL_ID_MAX_LENGTH)
        throw  ("Prohibited length of param/s");

    sendData(JOIN, (TCP_DataStruct){.display_name = this->display_name,
                                    .channel_id = channel_id});
}

void TCPClass::send_rename (std::string new_display_name) {
    if (new_display_name.length() > DISPLAY_NAME_MAX_LENGTH)
        throw ("Prohibited length of param/s");
    else
        this->display_name = new_display_name;
}

void TCPClass::send_bye () {
    session_end();
}
/***********************************************************************************/
void TCPClass::sendData (uint8_t type, TCP_DataStruct send_data) {
    // Prepare data to sendData
    std::string message = convert_to_string(type, send_data);
    const char* out_buffer = message.c_str();

    // Send data
    ssize_t bytes_send = send(this->socket_id, out_buffer, strlen(out_buffer), 0);

    // Check for errors
    if (bytes_send <= 0) {
        session_end();
        throw ("Error while sending data to server");
    }
}
/***********************************************************************************/
MSG_TYPE TCPClass::get_msg_type (std::string first_msg_word) {
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
void TCPClass::receive () {
    char in_buffer[MAXLENGTH];

    while (!this->stop_recv) {
        ssize_t bytes_received = recv(socket_id, in_buffer, MAXLENGTH, 0);

        // Check for errors
        if (bytes_received <= 0) {
            session_end();
            throw ("Error while receiving data from server");
        }

        in_buffer[bytes_received] = '\0';
        std::string response(in_buffer);

        // Load whole message - each msg ends with "\r\n";
        get_line_words(response.substr(0, response.find("\r\n")), this->line_vec);

        TCP_DataStruct resp_data = deserialize_msg(get_msg_type(this->line_vec.at(0)), response);
        proces_response(get_msg_type(this->line_vec.at(0)), resp_data);
    }
}
/***********************************************************************************/
void TCPClass::proces_response (uint8_t resp, TCP_DataStruct& resp_data) {
    switch (cur_state) {
        case S_AUTH:
            switch (resp) {
                case REPLY:
                    // Output message
                    OutputClass::out_reply(resp_data.result, resp_data.message);
                    // Change state
                    cur_state = S_OPEN;
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
                    break;
                case MSG:
                    // Output message
                    OutputClass::out_msg(this->display_name, resp_data.message);
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
                    sendData(ERR, (TCP_DataStruct){.message = "Unexpected server message",
                                                   .display_name = this->display_name});
                    break;
            }
            break;
        case S_ERROR:
            // Switch to end state
            cur_state = S_END;
            send_bye();
            break;
        case S_START:
        case S_END:
            // Ignore everything
            break;
        default:
            // Not expected state, output error
            throw ("Unknown current state");
    }
}
/***********************************************************************************/
TCP_DataStruct TCPClass::deserialize_msg (uint8_t msg_type, std::string msg) {
    TCP_DataStruct out;

    switch (msg_type) {
        case REPLY: // REPLY IS {MessageContent}\r\n
            out.message = this->line_vec.at(2);
            break;
        case MSG: // MSG FROM {DisplayName} IS {MessageContent}\r\n
            out.display_name = this->line_vec.at(2);
            out.message = this->line_vec.at(4);
            break;
        case ERR: // ERROR FROM {DisplayName} IS {MessageContent}\r\n
            out.display_name = this->line_vec.at(2);
            out.message = this->line_vec.at(4);
            break;
        default:
            throw ("Unknown message type provided");
            break;
    }
    // Return deserialized message
    return out;
}
/***********************************************************************************/
std::string TCPClass::convert_to_string (uint8_t type, TCP_DataStruct& data) {
    std::string msg;

    switch (type) {
        case AUTH: // AUTH {Username} USING {Secret}\r\n
            msg = "AUTH " + data.user_name + " USING " + data.secret + "\r\n";
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
        default:
            throw ("Unknown message type provided");
            break;
    }
    // Return composed message
    return msg;
}
