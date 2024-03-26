#ifndef UDPCLASS_H
#define UDPCLASS_H

#define HEADER_SIZE 3

#include "ClientClass.h"

#pragma pack(push, 1)
typedef struct {
    uint8_t type    = NO_TYPE; // 1 byte
    uint16_t msg_id = 0;       // 2 bytes
} UDP_Header;
#pragma pack(pop)

typedef struct {
    UDP_Header header;                  // 3 bytes (msg_type + msg_id)
    uint16_t ref_msg_id      = 0;       // 2 bytes
    bool result              = false;   // 1 byte
    std::string message      = "";      // N bytes
    std::string user_name    = "";      // N bytes
    std::string display_name = "";      // N bytes
    std::string secret       = "";      // N bytes
    std::string channel_id   = "";      // N bytes
    bool sent                = false;   // 1 bytes
} UDP_DataStruct;

class UDPClass : public ClientClass {
    private:
        // Transport data
        std::atomic<uint16_t> msg_id;
        uint8_t recon_attempts;
        uint16_t timeout;

        struct sockaddr_in sock_str;

        std::mutex send_mutex;

        // Vector to store all msg_ids already received
        std::vector<uint16_t> processed_msgs;
        // Vector to store all sent msgs and possible reply values in ref_msg_id
        std::vector<uint16_t> to_reply_ids;

        std::queue<std::pair<UDP_DataStruct, uint>> messages_to_send;

        void send_message (UDP_DataStruct data);
        void send_data (UDP_DataStruct& data);
        void send_err (std::string err_msg);
        void send_confirm (uint16_t confirm_to_id);
        void handle_send ();    // Thread for sending data to the server
        void handle_receive (); // Thread for receiving messages from server
        /* Helper methods */
        void set_socket_timeout (uint16_t timeout);
        void deserialize_msg (UDP_DataStruct& out_str, const char* msg, size_t total_size);
        void get_msg_part (const char* input, size_t& input_pos, size_t max_size, std::string& store_to);
        void switch_to_error (std::string err_msg);
        void thread_event (THREAD_EVENT event, uint16_t confirm_to_id = 0);
        std::string convert_to_string (UDP_DataStruct& data);
        std::string get_str_msg_id (uint16_t msg_id);
        UDP_Header create_header (uint8_t type);

    public:
        UDPClass (std::map<std::string, std::string> data_map);
        ~UDPClass () {};

        void open_connection () override;

        void send_auth (std::string user_name, std::string display_name, std::string secret) override;
        void send_msg (std::string msg) override;
        void send_join (std::string channel_id) override;
        void send_bye () override;
        void send_priority_bye () override;
        void session_end () override;
        bool send_rename (std::string new_display_name) override;
    };

#endif // UDPCLASS_H