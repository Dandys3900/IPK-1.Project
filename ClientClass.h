#ifndef CLIENTCLASS_H
#define CLIENTCLASS_H

#include "ConstsFile.h"

// Regex patterns for message values check
const regex username_pattern     ("^[A-z0-9-]{1,20}$");
const regex channel_id_pattern   ("^[A-z0-9-.]{1,20}$");
const regex secret_pattern       ("^[A-z0-9-]{1,128}$");
const regex display_name_pattern ("^[\x21-\x7E]{1,20}$");
const regex message_pattern      ("^[\x20-\x7E]{1,1400}$");

class ClientClass {
    public:
        ClientClass ()
        : port            (4567),
          socket_id       (-1),
          server_hostname (""),
          display_name    (""),
          stop_send       (false),
          stop_recv       (false),
          high_priority   (false),
          wait_for_reply  (false),
          end_program     (false),
          cur_state       (S_START)
        {
        }

        virtual ~ClientClass ()
        {
        }
        // Pure virtual methods implemented by both child classes (UDPClass and TCPClass)
        // Tries opening connection (UDP/TCP) and starting support threads for server and user actions handling
        virtual void open_connection ()                                                              = 0;
        // Appends BYE message to the client queue of messages being send to server
        virtual void send_bye ()                                                                     = 0;
        // Appends BYE message to the client queue of messages being send to server while clearing all other messages from queue to ensure priority sending
        virtual void send_priority_bye ()                                                            = 0;
        // Appends AUTH message with provided values to the client queue of messages being send to server
        virtual void send_auth (std::string user_name, std::string display_name, std::string secret) = 0;
        // Appends JOIN message with provided value to the client queue of messages being send to server
        virtual void send_join (std::string channel_id)                                              = 0;
        // Appends MSG message with provided values to the client queue of messages being send to server
        virtual void send_msg (std::string msg)                                                      = 0;
        // Closes the socket, stops running support threads and notifies main with conditional variable
        virtual void session_end ()                                                                  = 0;
        // Setter for client display_name attribute
        virtual bool send_rename (std::string new_display_name)                                      = 0;
        // Finish executing once both client's threads finished their work
        void wait_for_threads () {
            this->send_thread.join();
            this->recv_thread.join();
        }
        // Returns value indicating whether child class already closed connection
        bool stop_program () {
            return this->end_program;
        }
        // Getter for conditional variable
        std::condition_variable& get_cond_var () {
            return this->cond_var;
        }
        // Splits given string line into given string vector while using delim as separator
        void split_to_vec (std::string line, std::vector<std::string>& words_vec, char delim) {
            // Initially clear vector
            words_vec.clear();

            // Create string stream from input line
            std::stringstream ss(line);
            std::string line_word;

            // Insert each word to vector
            while(getline(ss, line_word, delim))
                words_vec.push_back(line_word);
        }
        // Return true if given message struct contains valid values, false otherwise
        template <typename strType>
        bool check_valid_msg (uint8_t type, strType& data) {
            switch (type) {
                case AUTH:
                    return regex_match(data.user_name, username_pattern) &&
                           regex_match(data.display_name, display_name_pattern) &&
                           regex_match(data.secret, secret_pattern);
                case ERR:
                case MSG:
                    return regex_match(data.display_name, display_name_pattern) &&
                           regex_match(data.message, message_pattern);
                case REPLY:
                    return regex_match(data.message, message_pattern);
                case JOIN:
                    return regex_match(data.channel_id, channel_id_pattern) &&
                           regex_match(data.display_name, display_name_pattern);
                case BYE:
                case CONFIRM:
                    return true;
                default: // Unexpected state, shouldnt happen
                    return false;
            }
        }
        // Checks for copatibility between current client's state and message requested to send, true if compatible, false if not
        bool check_msg_context (uint8_t msg_type, std::atomic<FSM_STATE>& cur_state) {
            switch (msg_type) {
                case AUTH:
                    if (cur_state != S_START && cur_state != S_AUTH)
                        return false;
                    // Move to auth state for handling reply msgs
                    cur_state = S_AUTH;
                    return true;
                case MSG:
                case JOIN:
                    if (cur_state != S_OPEN)
                        return false;
                    return true;
                case CONFIRM:
                case BYE:
                    // Switch to end state
                    cur_state = S_END;
                    return true;
                case ERR:
                    // Switch to error state
                    cur_state = S_ERROR;
                    return true;
                default:
                    return false;
            }
        }

    protected:
        // Transport data
        uint16_t port;
        int socket_id;
        // Mutex avoid race conditions when accessing and working with the queue
        std::mutex editing_front_mutex;
        // Supporting threads for send and receive
        std::jthread send_thread;
        std::jthread recv_thread;

        std::string server_hostname;
        std::string display_name;

        bool stop_send;
        bool stop_recv;
        bool high_priority;

        std::atomic<bool> wait_for_reply;
        std::atomic<bool> end_program;
        std::atomic<FSM_STATE> cur_state;

        std::condition_variable cond_var;
        std::condition_variable send_cond_var;
};

#endif // CLIENTCLASS_H