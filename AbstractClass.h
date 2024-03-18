#ifndef ABSCLASS_H
#define ABSCLASS_H

#include "ConstsFile.h"

// Regex patterns for message values check
const regex username_pattern     ("[A-z0-9-]{1,20}");
const regex channel_id_pattern   ("[A-z0-9-.]{1,20}");
const regex secret_pattern       ("[A-z0-9-]{1,128}");
const regex display_name_pattern ("[\x21-\x7E]{1,20}");
const regex message_pattern      ("[\x20-\x7E]{1,1400}");

class AbstractClass {
    public:
        virtual void open_connection() = 0;
        virtual void send_bye() = 0;
        virtual void send_auth (std::string user_name, std::string display_name, std::string secret) = 0;
        virtual void send_join (std::string channel_id) = 0;
        virtual void send_rename (std::string new_display_name) = 0;
        virtual void send_msg (std::string msg) = 0;

        void get_line_words (std::string line, std::vector<std::string>& words_vec) {
            // Clear vector
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

        virtual ~AbstractClass () {}
};

#endif // ABSCLASS_H