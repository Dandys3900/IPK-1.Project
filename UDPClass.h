#ifndef UDPCLASS_H
#define UDPCLASS_H

#include "ConstsFile.h"

typedef struct {
    uint8_t type             = NO_TYPE; // 1 byte
    uint16_t msg_id          = 0;       // 2 bytes
    uint16_t ref_msg_id      = 0;       // 2 bytes
    bool result              = false;   // 1 byte
    std::string message      = "";      // N bytes
    std::string user_name    = "";      // N bytes
    std::string display_name = "";      // N bytes
    std::string secret       = "";      // N bytes
    std::string channel_id   = "";      // N bytes
} UDP_DataStruct;

class UDPClass {
    private:
        // Transport data
        uint16_t msg_id;
        uint16_t port;
        std::string server_hostname;
        int socket_id;

        // Behaviour
        uint8_t recon_attempts;
        uint16_t timeout;

        // Inner values
        std::string display_name;
        FSM_STATE cur_state;
        struct sockaddr_in sock_str;

        // Vector to store all msg_ids already received
        std::vector<uint16_t> processed_msgs;

        UDP_DataStruct auth_data;

        // Helper methods
        void session_end ();
        void send_confirm ();
        void set_socket_timeout (uint16_t timeout);
        std::string convert_to_string (uint8_t type, UDP_DataStruct& data);
        UDP_DataStruct deserialize_msg (uint8_t msg_type, uint16_t msg_id, std::string msg);

        // Send/receive methos
        void sendData (uint8_t type, UDP_DataStruct send_data);
        void handle_send (uint8_t msg_type, UDP_DataStruct send_data);
        bool wait_for_confirmation (const uint16_t exp_msg_id);
        void receive ();
        void proces_response (uint8_t resp, UDP_DataStruct& resp_data);

    public:
        UDPClass (std::map<std::string, std::string> data_map);
        ~UDPClass ();

        void open_connection ();

        void send_auth (UDP_DataStruct cmd_data);
        void send_msg (std::string msg);
        void send_join (std::string channel_id);
        void send_rename (std::string new_display_name);
        void send_bye ();
};

#endif // UDPCLASS_H