#ifndef TCPCLASS_H
#define TCPCLASS_H

#include "ConstsFile.h"

typedef struct {
    bool result              = false;   // 1 byte
    std::string message      = "";      // N bytes
    std::string user_name    = "";      // N bytes
    std::string display_name = "";      // N bytes
    std::string secret       = "";      // N bytes
    std::string channel_id   = "";      // N bytes
} TCP_DataStruct;

class TCPClass {
    private:
        // Transport data
        std::string msg_id;
        uint16_t port;
        std::string server_hostname;
        int socket_id;

        // Inner values
        std::string display_name;
        FSM_STATE cur_state;

        // Vector for storing words of received message
        std::vector<std::string> line_vec;

        // Helper methods
        void session_end ();
        MSG_TYPE get_msg_type (std::string first_msg_word);
        std::string convert_to_string (uint8_t type, TCP_DataStruct& data);
        TCP_DataStruct deserialize_msg (uint8_t msg_type, std::string msg);

        // Send/receive methos
        void sendData (uint8_t type, TCP_DataStruct send_data);
        void receive ();
        void proces_response (uint8_t resp, TCP_DataStruct& resp_data);

    public:
        TCPClass (std::map<std::string, std::string> data_map);
        ~TCPClass ();

        void open_connection ();

        void send_auth (TCP_DataStruct cmd_data);
        void send_msg (std::string msg);
        void send_join (std::string channel_id);
        void send_rename (std::string new_display_name);
        void send_bye ();
};

#endif // TCPCLASS_H