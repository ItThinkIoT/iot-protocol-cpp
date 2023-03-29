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

#define IOT_VERSION (byte)1
#define IOT_PROTOCOL_MAX_READ_LENGTH 1024

enum class EIoTMethod : char
{
    SIGNAL = 'S',
    REQUEST = 'R',
    RESPONSE = 'r'
};

struct IoTRequest
{
    byte version;
    EIoTMethod method;
    uint16_t id;
    char *path;
    std::map<String, String> headers;

    uint8_t *body;
    size_t bodyLength;
    Client *client;
};

typedef std::function<void(void)> Next;
typedef void (*IoTMiddleware)(IoTRequest *, Next *);

enum class EIoTRequestPart : char
{
    BODY = 'B',
};

typedef std::function<void(IoTRequest *response)> OnResponse;
typedef std::function<void(IoTRequest *request)> OnTimeout;

struct IoTRequestResponse
{
    OnResponse *onResponse;
    OnTimeout *onTimeout;

    unsigned long timeout;

    IoTRequest request;
};

class IoTApp
{
private:
    std::vector<Client *> clients;
    void onData(Client *client, uint8_t *buffer, size_t bufLen);
    std::map<uint16_t, IoTRequestResponse> requestResponse = std::map<uint16_t, IoTRequestResponse>();

public:
    IoTApp();
    std::vector<IoTMiddleware> middlewares;

    /* Common methods */
    void use(IoTMiddleware middleware);
    void runMiddleware(IoTRequest *request, int index);
    void listen(Client *client);
    uint16_t generateRequestId();
    IoTRequest *signal(IoTRequest *request);
    IoTRequest *request(IoTRequest *request, IoTRequestResponse *requestResponse);
    IoTRequest *response(IoTRequest *request, uint8_t *body);
    IoTRequest *send(IoTRequest *request, IoTRequestResponse *requestResponse);

    /* Helpers methods */
    void resetClients();
    void loop();
};

// #ifdef __cplusplus
// }
// #endif

#endif