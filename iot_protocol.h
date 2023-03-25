#pragma once

#ifndef __IOT_PROTOCOL_H__
#define __IOT_PROTOCOL_H__

// #ifdef __cplusplus
// extern "C"
// {
// #endif

#include "Arduino.h"
#include <vector>
#include <functional>
#include <map>
#include <algorithm>

#include "iot_helpers.h"

#define IOT_PROTOCOL_MAX_READ_LENGTH 1024

    enum class EIoTMethod : char
    {
        SIGNAL = 'S',
        REQUEST = 'R'
    };

    enum class EIoTRequestPart : char
    {
        BODY = 'B',
    };

    struct IoTRequest
    {
        byte version;
        EIoTMethod method;
        char* path;
        std::map<String, String> params;
        std::map<String, String> headers;
        uint8_t * body;
        size_t bodyLength;
        Client * client;
    };

    typedef std::function<void(void)> Next;
    
    typedef void (*IoTMiddleware)(IoTRequest *, Next*);

    class IoTApp
    {
    private: 
        std::vector<Client *> clients;
        void onData(Client* client, uint8_t* buffer, size_t bufLen);
    public:
        std::vector<IoTMiddleware> middlewares;

        void use(IoTMiddleware middleware);

        void runMiddleware(IoTRequest *request, int index);

        void listen(Client *client);
        
        void resetClients();

        void loop();

    };

// #ifdef __cplusplus
// }
// #endif

#endif