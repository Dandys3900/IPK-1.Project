#ifndef UDPCLASS_H
#define UDPCLASS_H

#define HEADER_SIZE 3

#include "AbstractClass.h"

// Enum for possible thread events
enum THREAD_EVENT {
    NO_EVENT = 0,
    TIMEOUT,
    CONFIRMATION
};

typedef struct {
    uint8_t type    = NO_TYPE; // 1 byte
    uint16_t msg_id = 0;       // 2 bytes
} UDP_Header;

typedef struct {
    UDP_Header header;                  // 3 bytes (msg_type + msg_id)
    uint16_t ref_msg_id      = 0;       // 2 bytes
    bool result              = false;   // 1 byte
    std::string message      = "";      // N bytes
    std::string user_name    = "";      // N bytes
    std::string display_name = "";      // N bytes
    std::string secret       = "";      // N bytes
    std::string channel_id   = "";      // N bytes
} UDP_DataStruct;

class UDPClass : public AbstractClass {
    private:
        // Transport data
        uint16_t msg_id;
        uint16_t port;
        uint8_t recon_attempts;
        uint16_t timeout;

        int socket_id;
        int retval;

        std::string server_hostname;
        std::string display_name;

        std::atomic<FSM_STATE> cur_state;
        std::atomic<uint16_t> replying_to_id;
        std::atomic<uint16_t> latest_sent_id;

        UDP_DataStruct auth_data;
        struct sockaddr_in sock_str;

        std::mutex send_mutex;
        std::mutex session_end_mutex;
        std::mutex editing_front_mutex;

        std::jthread send_thread;
        std::jthread recv_thread;

        // Vector to store all msg_ids already received
        std::vector<uint16_t> processed_msgs;

        std::queue<std::pair<UDP_DataStruct, uint>> messages_to_send;
        std::condition_variable send_cond_var;

        bool stop_send;
        bool stop_recv;

        void send_message (UDP_DataStruct data);
        void send_data (UDP_DataStruct data);
        void send_err (std::string err_msg);
        void send_confirm (uint16_t confirm_to_id);
        void session_end ();
        void handle_send ();                              // Thread for sending data to the server
        void handle_receive ();                           // Thread for receiving messages from server
        /* Helper methods */
        void set_socket_timeout (uint16_t timeout);
        void deserialize_msg (UDP_DataStruct& out_str, const char* msg);
        void get_msg_part (const char* input, size_t& input_pos, std::string& store_to);
        void check_msg_valid (UDP_DataStruct& data);
        void invalid_reply_id ();
        void thread_event (THREAD_EVENT event, uint16_t confirm_to_id = -1);
        std::string convert_to_string (UDP_DataStruct data);
        UDP_Header create_header (uint8_t type);

        //delete
        void safePrint (const string& msg);
        std::string get_msg_type (uint8_t type);
        std::mutex print_mutex;

    public:
        UDPClass (std::map<std::string, std::string> data_map);
        ~UDPClass () {};

        void open_connection () override;

        void send_auth (std::string user_name, std::string display_name, std::string secret) override;
        void send_msg (std::string msg) override;
        void send_join (std::string channel_id) override;
        void send_rename (std::string new_display_name) override;
        void send_bye () override;
};

#endif // UDPCLASS_H