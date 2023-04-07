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

#define IOT_VERSION (uint8_t)1

#define IOT_ETX 0x3
#define IOT_RS 0x1E

#define IOT_MSCB_ID 0b00000010
#define IOT_MSCB_PATH 0b00000001
#define IOT_LSCB_HEADER 0b00000010
#define IOT_LSCB_BODY 0b00000001

#define IOT_PROTOCOL_BUFFER_SIZE 1024

enum class EIoTMethod : uint8_t
{
    SIGNAL = 0x1,
    REQUEST = 0x2,
    RESPONSE = 0x3,
    STREAMING = 0x4
};
struct IoTRequest
{
    uint8_t version;
    EIoTMethod method;
    uint16_t id;
    char *path;
    std::map<char *, char *> headers;
    uint8_t *body;
    size_t bodyLength;
    Client *client;
};

typedef std::function<void(void)> Next;
typedef void (*IoTMiddleware)(IoTRequest *, Next *);

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
    IoTApp(uint32_t delay = 300);
    uint32_t delay = 300;

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
    void readClient(Client *client);
    void loop();
};

// #ifdef __cplusplus
// }
// #endif

#endif