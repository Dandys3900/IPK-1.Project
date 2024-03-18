#ifndef TCPCLASS_H
#define TCPCLASS_H

#include "AbstractClass.h"

typedef struct {
    uint8_t type             = NO_TYPE; // 1 byte
    bool result              = false;   // 1 byte
    std::string message      = "";      // N bytes
    std::string user_name    = "";      // N bytes
    std::string display_name = "";      // N bytes
    std::string secret       = "";      // N bytes
    std::string channel_id   = "";      // N bytes
} TCP_DataStruct;

class TCPClass : public AbstractClass {
    private:
        // Transport data
        uint16_t port;

        int socket_id;
        int retval;

        std::string display_name;
        std::string server_hostname;

        std::atomic<FSM_STATE> cur_state;

        TCP_DataStruct auth_data;

        std::mutex editing_front_mutex;

        std::jthread send_thread;
        std::jthread recv_thread;

        bool stop_send;
        bool stop_recv;

        // Vector for storing words of received message
        std::vector<std::string> line_vec;

        std::queue<TCP_DataStruct> messages_to_send;

        void send_message (TCP_DataStruct& data);
        void send_data (TCP_DataStruct& data);
        void send_err (std::string err_msg);
        void session_end ();
        void handle_send ();
        void handle_receive ();
         /* Helper methods */
        void set_socket_timeout ();
        void switch_to_error (std::string err_msg);
        void thread_event (THREAD_EVENT event);
        MSG_TYPE get_msg_type (std::string first_msg_word);
        std::string convert_to_string (TCP_DataStruct& data);
        void deserialize_msg(TCP_DataStruct& out_str);
        std::string load_rest (size_t start_from);

    public:
        TCPClass (std::map<std::string, std::string> data_map);
        ~TCPClass () {};

        void open_connection () override;

        void send_auth (std::string user_name, std::string display_name, std::string secret) override;
        void send_msg (std::string msg) override;
        void send_join (std::string channel_id) override;
        void send_rename (std::string new_display_name) override;
        void send_bye () override;
    };

#endif // TCPCLASS_H