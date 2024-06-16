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

#define IOT_MULTIPART_TIMEOUT 5000

enum class EIoTMethod : uint8_t
{
    SIGNAL = 0x1,
    REQUEST = 0x2,
    RESPONSE = 0x3,
    STREAMING = 0x4,
    ALIVE_REQUEST = 0x5,
    ALIVE_RESPONSE = 0x6
};

struct IoTClient;
struct IoTRequest
{
    uint8_t version;
    EIoTMethod method;
    uint16_t id;
    char *path;
    std::map<char *, char *> headers;
    uint8_t *body;
    size_t bodyLength;
    size_t totalBodyLength;
    size_t parts;
    IoTClient *iotClient;
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

struct IoTMultiPart
{
    uint32_t parts;    /* Number of Parts */
    uint32_t received; /* Bytes received */
    unsigned long timeout;
};

typedef std::function<void(IoTClient *iotClient)> OnDisconnect;

struct IoTClient
{
    Client *client;
    std::map<uint16_t, IoTRequestResponse> requestResponse;
    std::map<uint16_t, IoTMultiPart> multiPartControl;
    uint8_t *remainBuffer; /* Remain data on buffer o be processed */
    size_t remainBufferLength;
    bool lockedForWrite;
    /* Alive */
    uint16_t aliveInterval;
    unsigned long aliveNextRequest;

    OnDisconnect *onDisconnect;
};

class IoTProtocol
{
private:
    std::map<Client *, IoTClient *> clients = std::map<Client *, IoTClient *>();
    void onData(IoTClient *iotClient, uint8_t *buffer, size_t bufLen);

    /* Alive Request Response Timeout */
    OnTimeout onAliveRequestTimeout;

public:
    IoTProtocol(unsigned long timeout = 1000, uint32_t delay = 300);
    uint32_t delay = 300;
    unsigned long timeout = 1000;

    std::vector<IoTMiddleware> middlewares;

    /* Common methods */
    void use(IoTMiddleware middleware);
    void runMiddleware(IoTRequest *request, int index);
    void listen(IoTClient *iotClient);
    uint16_t generateRequestId(IoTClient *iotClient);
    IoTRequest *signal(IoTRequest *request);
    IoTRequest *request(IoTRequest *request, IoTRequestResponse *requestResponse);
    IoTRequest *response(IoTRequest *request);
    IoTRequest *streaming(IoTRequest *request, IoTRequestResponse *requestResponse);
    IoTRequest *aliveRequest(IoTRequest *request, IoTRequestResponse *requestResponse);
    IoTRequest *aliveResponse(IoTRequest *request);
    IoTRequest *send(IoTRequest *request, IoTRequestResponse *requestResponse);
    void resetRemainBuffer(IoTClient *iotClient);
    void scheduleNextAliveRequest(IoTClient *iotClient);

    /* Helper methods */
    void freeRequest(IoTRequest *request);
    // void resetClients();
    void readClient(IoTClient *iotClient);
    void loop();

    /* Utils for app layer */
    const char *getHeader(IoTRequest *request, const char *headerKey);
};

// #ifdef __cplusplus
// }
// #endif

#endif