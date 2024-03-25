#ifndef CLIENTCLASS_H
#define CLIENTCLASS_H

#include "ConstsFile.h"

// Regex patterns for message values check
const regex username_pattern     ("[A-z0-9-]{1,20}");
const regex channel_id_pattern   ("[A-z0-9-.]{1,20}");
const regex secret_pattern       ("[A-z0-9-]{1,128}");
const regex display_name_pattern ("[\x21-\x7E]{1,20}");
const regex message_pattern      ("[\x20-\x7E]{1,1400}");

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
          end_program     (false),
          cur_state       (S_START)
        {
        }

        virtual ~ClientClass ()
        {
        }

        virtual void open_connection ()                                                              = 0;
        virtual void send_bye ()                                                                     = 0;
        virtual void send_priority_bye ()                                                            = 0;
        virtual void send_auth (std::string user_name, std::string display_name, std::string secret) = 0;
        virtual void send_join (std::string channel_id)                                              = 0;
        virtual void send_msg (std::string msg)                                                      = 0;
        virtual void session_end ()                                                                  = 0;
        virtual bool send_rename (std::string new_display_name)                                      = 0;

        void wait_for_threads () {
            // Wait for send and receive threads to finish
            this->send_thread.join();
            this->recv_thread.join();
        }

        bool stop_program () {
            return this->end_program;
        }

        std::condition_variable& get_cond_var () {
            return this->cond_var;
        }

        void get_line_words (std::string line, std::vector<std::string>& words_vec) {
            // Initially clear vector
            words_vec.clear();

            // Create string stream from input line
            std::stringstream ss(line);
            std::string line_word;

            // Insert each word to vector
            while (ss >> line_word)
                words_vec.push_back(line_word);
        }

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

        bool check_msg_context (uint8_t msg_type, std::atomic<FSM_STATE>& cur_state) {
            switch (msg_type) {
                case AUTH:
                    if (cur_state != S_START && cur_state != S_AUTH && cur_state != S_AUTH_CONFD)
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
                default:
                    return false;
            }
        }

    protected:
        // Transport data
        uint16_t port;
        int socket_id;

        std::mutex editing_front_mutex;

        std::jthread send_thread;
        std::jthread recv_thread;

        std::string server_hostname;
        std::string display_name;

        bool stop_send;
        bool stop_recv;
        bool high_priority;

        std::atomic<bool> end_program;
        std::atomic<FSM_STATE> cur_state;

        std::condition_variable cond_var;
        std::condition_variable send_cond_var;
};

#endif // CLIENTCLASS_H