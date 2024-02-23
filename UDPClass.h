#ifndef UDPCLASS_H
#define UDPCLASS_H

#include "ConstsFile.h"

class UDPClass {
    private:
        // Transport data
        uint16_t msg_id;
        uint16_t port;
        std::string ip_addr;
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

        DataStruct auth_data;

        // Helper methods
        void session_end ();
        void send_confirm ();
        void set_socket_timeout (uint16_t timeout);
        std::string convert_to_string (uint8_t type, DataStruct& data);
        DataStruct deserialize_msg (uint8_t msg_type, uint16_t msg_id, std::string msg);

        // Send/receive methos
        void send (uint8_t type, DataStruct send_data);
        void handle_send (uint8_t msg_type, DataStruct send_data);
        bool wait_for_confirmation (const uint16_t exp_msg_id);
        void receive ();
        void proces_response (uint8_t resp, DataStruct& resp_data);

    public:
        UDPClass (std::map<std::string, std::string> data_map);
        ~UDPClass ();

        void start_listening ();
        void start_sending ();

        void send_auth (DataStruct cmd_data);
        void send_msg (std::string msg);
        void send_join (std::string channel_id);
        void send_rename (std::string new_display_name);
        void send_bye ();
};

#endif // UDPCLASS_H