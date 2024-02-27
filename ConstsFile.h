#ifndef CONSTSFILE_H
#define CONSTSFILE_H

#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <vector>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <algorithm>
#include <map>
#include <csignal>
#include <sstream>

#include "OutputClass.h"

#define MAXLENGTH 1024

#define USERNAME_MAX_LENGTH 20
#define CHANNEL_ID_MAX_LENGTH 20
#define SECRET_MAX_LENGTH 128
#define DISPLAY_NAME_MAX_LENGTH 20
#define USERNAME_MAX_LENGTH 20
#define MESSAGE_MAX_LENGTH 1400

// Enum for message types
enum MSG_TYPE {
    NO_TYPE = -1,
    CONFIRM  = 0x00,
    REPLY    = 0x01,
    AUTH     = 0x02,
    JOIN     = 0x03,
    MSG      = 0x04,
    ERR      = 0xFE,
    BYE      = 0xFF
};

// Enum FSM states
enum FSM_STATE {
    S_START = 0,
    S_AUTH,
    S_OPEN,
    S_ERROR,
    S_END
};

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
} DataStruct;

#endif // CONSTSFILE_H