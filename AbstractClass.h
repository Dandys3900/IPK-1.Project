#ifndef ABSCLASS_H
#define ABSCLASS_H

#include "ConstsFile.h"

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

            while (ss >> line_word)
                words_vec.push_back(line_word);
        }

        bool str_alphanums (std::string input) {
            const regex pattern("[a-zA-Z0-9-]+");
            return regex_match(input, pattern);
        }

        bool str_printable (std::string input, bool incl_space) {
            const regex pattern((incl_space) ? "[ -~]+" : "[!-~]+");
            return regex_match(input, pattern);
        }

        virtual ~AbstractClass () {}
};

#endif // ABSCLASS_H