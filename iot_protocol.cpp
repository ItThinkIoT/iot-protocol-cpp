#include "iot_protocol.h"

IoTProtocol::IoTProtocol(unsigned long timeout, uint32_t delay)
{
    this->timeout = timeout;
    this->delay = delay;

    this->onAliveRequestTimeout = [this](IoTRequest *request)
    {
        /* Close client */
        request->iotClient->client->stop();
        request->iotClient->requestResponse.clear();
        request->iotClient->multiPartControl.clear();

        this->resetRemainBuffer(request->iotClient);

        if (request->iotClient->onDisconnect != NULL)
        {
            (*(request->iotClient->onDisconnect))(request->iotClient);
        }
    };

    this->onBufferSizeResponse = [this](IoTRequest *response)
    {
        if (response->method != EIoTMethod::BUFFER_SIZE_RESPONSE)
            return;
        response->iotClient->bufferSize = (response->body[0] << 24) + (response->body[1] << 16) + (response->body[2] << 8) + response->body[3];
    };
}

void IoTProtocol::use(IoTMiddleware middleware)
{
    this->middlewares.push_back(middleware);
}

void IoTProtocol::runMiddleware(IoTRequest *request, int index = 0)
{
    if (index >= this->middlewares.size())
    {
        return;
    }

    Next _next = [this, request, index]()
    {
        this->runMiddleware(request, (index + 1));
    };

    this->middlewares.at(index)(request, &_next);
}

void IoTProtocol::listen(IoTClient *iotClient)
{
    if (iotClient->client == NULL)
    {
        throw "[IoTProtocol] Client of IoTClient is null";
    }

    iotClient->requestResponse = std::map<uint16_t, IoTRequestResponse>();
    iotClient->multiPartControl = std::map<uint16_t, IoTMultiPart>();
    iotClient->remainBuffer = NULL;
    iotClient->remainBufferLength = 0;
    iotClient->lockedForWrite = false;
    if (iotClient->aliveInterval == 0)
    {
        iotClient->aliveInterval = IOT_PROTOCOL_DEFAULT_ALIVE_INTERVAL;
    }
    this->scheduleNextAliveRequest(iotClient);

    if (iotClient->bufferSize == 0)
    {
        iotClient->bufferSize = IOT_PROTOCOL_DEFAULT_BUFFER_SIZE;
    }

    this->clients.insert(std::make_pair(iotClient->client, iotClient));
}

void IoTProtocol::onData(IoTClient *iotClient, uint8_t *buffer, size_t bufLen)
{

    IoTRequest request = {
        0,
        EIoTMethod::SIGNAL,
        0,
        NULL,
        std::map<char *, char *>(),
        NULL,
        0,
        0,
        0,
        iotClient};

    size_t offset = 0;

    if (bufLen < offset)
        return;

    uint8_t MSCB = buffer[offset];
    uint8_t LSCB = buffer[++offset];

    request.version = MSCB >> 2;
    request.method = (EIoTMethod)(LSCB >> 2);

    /* Alive Method */
    if (request.method == EIoTMethod::ALIVE_REQUEST)
    {
        /* Respond the alive Request  */
        IoTRequest aliveResponse = {
            IOT_VERSION,
            EIoTMethod::ALIVE_REQUEST,
            0,
            NULL,
            std::map<char *, char *>(),
            NULL,
            0,
            0,
            0,
            iotClient};
        this->aliveResponse(&aliveResponse);

        /* Cancel next alive request and schedule another one from now */
        this->scheduleNextAliveRequest(iotClient);

        return this->freeRequest(&request);
    }

    /* ID */
    if (MSCB & IOT_MSCB_ID && bufLen >= offset + 2)
    {
        request.id = (buffer[++offset] << 8) + (buffer[++offset]);
    }

    /* PATH */
    if (MSCB & IOT_MSCB_PATH)
    {
        int indexEXT = indexOf(buffer, bufLen, IOT_ETX, ++offset);
        if (indexEXT > -1)
        {
            size_t pathLength = (indexEXT - offset);
            request.path = static_cast<char *>(malloc(pathLength * sizeof(char) + 1));
            memcpy(request.path, (buffer + offset), pathLength);
            request.path[pathLength] = '\0';

            offset = indexEXT;
        }
    }

    /* HEADER */
    if (LSCB & IOT_LSCB_HEADER)
    {
        offset++;
        int indexRS = -1;
        int indexEXT = -1;

        uint8_t headerSize = buffer[offset++];

        while ((indexRS = indexOf(buffer, bufLen, IOT_RS, offset)) &&
               ((indexEXT = indexOf(buffer, bufLen, IOT_ETX, offset + 1)) != -1) &&
               indexRS < indexEXT - 1)
        {
            size_t keyLength = (indexRS - offset);

            char *headerKey = (char *)malloc(keyLength * sizeof(char) + 1);
            memcpy(headerKey, (buffer + offset), keyLength);
            headerKey[keyLength] = '\0';

            size_t valueLength = (indexEXT - (indexRS + 1));
            char *headerValue = (char *)malloc(valueLength * sizeof(char) + 1);
            memcpy(headerValue, (buffer + indexRS + 1), valueLength);
            headerValue[valueLength] = '\0';

            request.headers.insert(std::make_pair(headerKey, headerValue));

            offset = indexEXT + 1;

            if (request.headers.size() == headerSize)
            {
                break;
            }
        }

        offset--;
    }

    /* BODY */
    bool requestCompleted = true;

    if (LSCB & IOT_LSCB_BODY)
    {
        uint8_t bodyLengthSize = 2;
        switch (request.method)
        {
        case EIoTMethod::SIGNAL:
        case EIoTMethod::BUFFER_SIZE_REQUEST:
        case EIoTMethod::BUFFER_SIZE_RESPONSE:
            bodyLengthSize = 1;
            break;
        case EIoTMethod::STREAMING:
            bodyLengthSize = 4;
            break;
        }

        request.bodyLength = 0;
        for (uint8_t i = bodyLengthSize; i > 0; i--)
        {
            request.bodyLength += buffer[++offset] << ((i - 1) * 8);
        }
        request.totalBodyLength = request.bodyLength;

        /* Single Request */
        /* ...(17) EXT (18) 0 (19) 30 | (20) B (21) B (22) B + ...25B + (48) B , (49) B , (50) */

        /* Multi Request */
        /* ...(17) EXT (18) 4 (19) 36 | (20) B (21) B (22) B + ...999B + (1022) B , (1023) B , (1024) */
        /* ...(17) EXT (18) 4 (19) 36 | (20) B (21) B (22) B + ...51B + (74) B , (75) B , (76) */

        offset++;

        size_t bodyIncomeLength = bufLen - offset;
        size_t bodyEndIndex = offset + request.bodyLength;

        auto multiPartControl = iotClient->multiPartControl.find(request.id);
        if ((multiPartControl != iotClient->multiPartControl.end()))
        {
            bodyEndIndex -= multiPartControl->second.received;
        }
        else
        {
            IoTMultiPart multiPart = {
                0,
                0,
                millis()};

            iotClient->multiPartControl.insert(std::make_pair(request.id, multiPart));
            multiPartControl = iotClient->multiPartControl.find(request.id);
        }

        if (bodyEndIndex > bufLen)
        {
            bodyEndIndex = bufLen;
        }

        request.bodyLength = bodyEndIndex - offset;

        multiPartControl->second.parts++;
        multiPartControl->second.received += request.bodyLength;
        multiPartControl->second.timeout += IOT_MULTIPART_TIMEOUT;

        if (multiPartControl->second.received < request.totalBodyLength)
        {
            requestCompleted = false;
        }
        else
        {
            iotClient->multiPartControl.erase(request.id);
        }

        if (bodyIncomeLength > request.bodyLength) /* Income more than one request, so keeps it on remainBuffer */
        {
            iotClient->remainBufferLength = bufLen - bodyEndIndex;
            iotClient->remainBuffer = (uint8_t *)(malloc(iotClient->remainBufferLength * sizeof(uint8_t) + 1));
            memcpy(iotClient->remainBuffer, (buffer + bodyEndIndex), iotClient->remainBufferLength);
            iotClient->remainBuffer[iotClient->remainBufferLength] = '\0';
        }

        request.body = (uint8_t *)(malloc((request.bodyLength) * sizeof(uint8_t) + 1));
        memcpy(request.body, (buffer + offset), request.bodyLength);
        request.body[request.bodyLength] = '\0';

        offset = bodyEndIndex - 1;
    }

    /* Request Response */
    auto rr = iotClient->requestResponse.find(request.id);
    if (rr != iotClient->requestResponse.end())
    {
        if (rr->second.onResponse != NULL)
        {
            (*(rr->second.onResponse))(&request);
        }

        if (requestCompleted)
        {
            iotClient->requestResponse.erase(request.id);
        }
        else
        {
            rr->second.timeout += this->timeout;
        }
    }
    else
    {
        if (request.method == EIoTMethod::SIGNAL ||
            request.method != EIoTMethod::REQUEST ||
            request.method != EIoTMethod::STREAMING)
        {
            /* Middleware */
            this->runMiddleware(&request);
        }
    }

    /* Cancel next alive request and schedule another one from now */
    this->scheduleNextAliveRequest(iotClient);

    /* Buffer Size */
    if (request.method == EIoTMethod::BUFFER_SIZE_REQUEST)
    {
        /* Set buffer size */
        iotClient->bufferSize = (request.body[0] << 24) + (request.body[1] << 16) + (request.body[2] << 8) + request.body[3];
        /* Respond buffer size */
        this->bufferSizeResponse(&request);
    }

    this->freeRequest(&request);
}

uint16_t IoTProtocol::generateRequestId(IoTClient *iotClient)
{
    vTaskDelay(1);
    uint16_t id = (uint16_t)(millis() % 10000);
    if (iotClient->requestResponse.find(id) != iotClient->requestResponse.end() || id == 0)
    {
        return this->generateRequestId(iotClient);
    }
    return id;
}

IoTRequest *IoTProtocol::signal(IoTRequest *request)
{
    request->method = EIoTMethod::SIGNAL;
    return this->send(request, NULL);
}

IoTRequest *IoTProtocol::request(IoTRequest *request, IoTRequestResponse *requestResponse)
{
    request->method = EIoTMethod::REQUEST;
    return this->send(request, requestResponse);
}

IoTRequest *IoTProtocol::response(IoTRequest *request)
{
    request->method = EIoTMethod::RESPONSE;
    return this->send(request, NULL);
}

IoTRequest *IoTProtocol::streaming(IoTRequest *request, IoTRequestResponse *requestResponse)
{
    request->method = EIoTMethod::STREAMING;
    return this->send(request, requestResponse);
}

IoTRequest *IoTProtocol::aliveRequest(IoTRequest *request, IoTRequestResponse *requestResponse)
{
    request->method = EIoTMethod::ALIVE_REQUEST;
    request->id = 0;
    freeRequest(request);
    request->bodyLength = 0;
    request->totalBodyLength = 0;
    request->parts = 0;
    return this->send(request, requestResponse);
}

IoTRequest *IoTProtocol::aliveResponse(IoTRequest *request)
{
    request->method = EIoTMethod::ALIVE_RESPONSE;
    request->id = 0;
    freeRequest(request);
    request->bodyLength = 0;
    request->totalBodyLength = 0;
    request->parts = 0;
    return this->send(request, NULL);
}

IoTRequest *IoTProtocol::bufferSizeRequest(IoTClient *iotClient, uint32_t size)
{
    // 2048 : [0, 0 , 8, 0]
    uint8_t body[4];
    for (uint8_t i = 0; i < 4; i++)
    {
        body[i] = (size >> (24 - (i * 8))) & (0xFF);
    }
    // body[0] = size >> 24 & (0xFF);
    // body[1] = size >> 16 & (0xFF);
    // body[2] = size >> 8 & (0xFF);
    // body[3] = size & (0xFF);

    IoTRequest request = {
        IOT_VERSION,
        EIoTMethod::BUFFER_SIZE_REQUEST,
        0,
        NULL,
        std::map<char *, char *>(),
        body,
        4,
        0,
        0,
        iotClient};
    IoTRequestResponse onResponse = {
        &(this->onBufferSizeResponse),
        NULL};
    return this->send(&request, &onResponse);
}

IoTRequest *IoTProtocol::bufferSizeResponse(IoTRequest *request)
{
    IoTRequest response = {
        IOT_VERSION,
        EIoTMethod::BUFFER_SIZE_RESPONSE,
        request->id,
        NULL,
        std::map<char *, char *>(),
        request->body,
        request->bodyLength,
        0,
        0,
        request->iotClient};

    return this->send(&response, NULL);
}

IoTRequest *IoTProtocol::send(IoTRequest *request, IoTRequestResponse *requestResponse)
{

    if (request->version == 0)
    {
        request->version = IOT_VERSION;
    }

    uint8_t MSCB = request->version << 2;
    uint8_t LSCB = (uint8_t)(request->method) << 2;

    uint8_t bodyLengthSize = 2;

    LSCB += (((request->headers.size() > 0) ? IOT_LSCB_HEADER : 0) + ((request->body != NULL) ? IOT_LSCB_BODY : 0));

    switch (request->method)
    {
    case EIoTMethod::SIGNAL:
        MSCB += (((request->path != NULL) ? IOT_MSCB_PATH : 0));

        bodyLengthSize = 1;
        break;
    case EIoTMethod::REQUEST:
        MSCB += ((IOT_MSCB_ID) + ((request->path != NULL) ? IOT_MSCB_PATH : 0));
        break;
    case EIoTMethod::RESPONSE:
        MSCB += ((IOT_MSCB_ID));
        break;
    case EIoTMethod::STREAMING:
        MSCB += ((IOT_MSCB_ID) + ((request->path != NULL) ? IOT_MSCB_PATH : 0));

        bodyLengthSize = 4;
        break;
    case EIoTMethod::ALIVE_REQUEST:
    case EIoTMethod::ALIVE_RESPONSE:
        bodyLengthSize = 0;
        break;
    case EIoTMethod::BUFFER_SIZE_REQUEST:
    case EIoTMethod::BUFFER_SIZE_RESPONSE:
        bodyLengthSize = 1;
    }

    /* Sum Total Data Length */

    size_t dataLength = 2;

    if (MSCB & IOT_MSCB_ID)
    {
        if (request->id == 0)
        {
            request->id = this->generateRequestId(request->iotClient);
        }
        dataLength += 2;
    }

    size_t pathLength = 0;
    if (MSCB & IOT_MSCB_PATH)
    {
        pathLength = strlen(request->path);
        dataLength += pathLength + 1 /* (EXT) */;
    }

    size_t headersLength = 0;
    uint8_t headerSize = request->headers.size() & 255;
    if (LSCB & IOT_LSCB_HEADER)
    {
        if (request->headers.size() > 255)
        {
            throw "[IoTProtocol] Too many headers. Maximum Headers is 255.";
        }

        for (auto header = request->headers.begin(); header != request->headers.end(); ++header)
        {
            headersLength += strlen(header->first) + strlen(header->second) + 2; /* + 1 (RS) + 1 (EXT) */
        }
        dataLength += headersLength + 1; /* +1 (headerSize) */
    }

    if ((pathLength + headersLength) > ((request->iotClient->bufferSize) - 8))
    {
        throw "[IoTProtocol] Path and Headers too big.";
    }

    if (LSCB & IOT_LSCB_BODY)
    {
        dataLength += bodyLengthSize + request->bodyLength;
    }
    else
    {
        bodyLengthSize = 0;
    }

    if (dataLength > request->iotClient->bufferSize)
    {
        dataLength = request->iotClient->bufferSize;
    }

    /* Record Data */

    uint8_t data[dataLength + 1]; /* +1 => (\0) */
    size_t nextIndex = 0;

    data[nextIndex] = MSCB;
    data[++nextIndex] = LSCB;

    /* ID */
    if (MSCB & IOT_MSCB_ID)
    {
        data[++nextIndex] = request->id >> 8;  /* Id as Big Endian - (MSB first) */
        data[++nextIndex] = request->id & 255; /* Id as Big Endian - (LSB last)  */
    }

    /* PATH */
    if (MSCB & IOT_MSCB_PATH)
    {
        for (size_t i = 0; i < pathLength; i++)
        {
            data[++nextIndex] = *(request->path + i);
        }
        data[++nextIndex] = IOT_ETX;
    }

    /* HEADERs */
    if (LSCB & IOT_LSCB_HEADER)
    {
        data[++nextIndex] = headerSize;

        for (auto header = request->headers.begin(); header != request->headers.end(); ++header)
        {
            /* Key */
            size_t keyLength = strlen(header->first);
            for (size_t i = 0; i < keyLength; i++)
            {
                data[++nextIndex] = header->first[i];
            }

            /* RS */
            data[++nextIndex] = IOT_RS;

            /* Value */
            size_t valueLength = strlen(header->second);
            for (size_t i = 0; i < valueLength; i++)
            {
                data[++nextIndex] = header->second[i];
            }

            /* EXT */
            data[++nextIndex] = IOT_ETX;
        }
    }

    /* BODY */
    if (LSCB & IOT_LSCB_BODY)
    {
        /* Body Length */
        for (uint8_t i = bodyLengthSize; i > 0; i--) /* Body Length as Big Endian */
        {
            data[++nextIndex] = (request->bodyLength >> ((i - 1) * 8)) & 255;
        }
    }

    size_t prefixDataIndex = nextIndex;

    std::function<size_t(size_t, size_t)> writeBodyPart = [&writeBodyPart, this, request, prefixDataIndex, &data, requestResponse](size_t i = 0, size_t parts = 0)
    {
        size_t indexData = prefixDataIndex + 1; // 42
        /* Body */
        if (request->bodyLength > 0) // 1060 > 0
        {
            size_t bodyBufferRemain = (request->bodyLength - i);
            size_t bodyUntilIndex = ((bodyBufferRemain + indexData) > request->iotClient->bufferSize) ? i + ((request->iotClient->bufferSize) - indexData) : i + bodyBufferRemain;
            for (; i < bodyUntilIndex; i++)
            {
                *(data + (indexData++)) = *(request->body + i);
            }
        }

        *(data + (indexData)) = '\0';

        if (parts > 1) /* Schedule next alive request after send all data only if is a multipart */
        {
            /* Cancel and Schedule next alive request */
            this->scheduleNextAliveRequest(request->iotClient);
        }

        request->iotClient->client->write(data, indexData);

        if(requestResponse != NULL && requestResponse->onPartSent != NULL) {
            (*(requestResponse->onPartSent))(request, i, parts);
        }

        parts++;
        if (i >= request->bodyLength)
        {
            return parts;
        }
        else
        {
            return writeBodyPart(i, parts);
        }
    };

    while (request->iotClient->lockedForWrite)
    {
        vTaskDelay(this->delay);
    }
    request->iotClient->lockedForWrite = true;
    request->parts = writeBodyPart(0, 0);
    request->iotClient->lockedForWrite = false;

    if (requestResponse != NULL)
    {
        if (requestResponse->timeout == 0)
        {
            requestResponse->timeout = this->timeout;
        }
        requestResponse->timeout += millis();
        requestResponse->request = *request;

        request->iotClient->requestResponse.insert(std::make_pair(request->id, *requestResponse));
    }

    this->readClient(request->iotClient);

    return request;
}

void IoTProtocol::resetRemainBuffer(IoTClient *iotClient)
{
    iotClient->remainBufferLength = 0;
    if (iotClient->remainBuffer != NULL)
    {
        free(iotClient->remainBuffer);
    }
    iotClient->remainBuffer = NULL;
}

void IoTProtocol::scheduleNextAliveRequest(IoTClient *iotClient)
{
    if (iotClient->aliveInterval == 0)
        return;

    iotClient->aliveNextRequest = millis() + (iotClient->aliveInterval * 1000);
}

void IoTProtocol::freeRequest(IoTRequest *request)
{
    /* Free Request */
    free(request->path);
    free(request->body);
    for (auto header = request->headers.begin(); header != request->headers.end(); header++)
    {
        free(header->first);
        free(header->second);
    }
}

// void IoTProtocol::resetClients()
// {
//     for (auto client = this->clients.begin(); client != this->clients.end(); ++client)
//     {
//         this->resetRemainBuffer(client->second);
//     }

//     this->clients.clear();
// }

void IoTProtocol::readClient(IoTClient *iotClient)
{
    if (!(iotClient->client->connected()))
    {
        return;
    }

    uint8_t buffer[(iotClient->bufferSize) + 1];
    size_t bufferLength = 0;

    if (iotClient->remainBuffer != NULL)
    {
        for (uint32_t i = 0; i < iotClient->remainBufferLength; i++)
        {
            buffer[bufferLength++] = *(iotClient->remainBuffer + i);
        }

        this->resetRemainBuffer(iotClient);
    }

    while (iotClient->client->available() && bufferLength < iotClient->bufferSize)
    {
        buffer[bufferLength++] = iotClient->client->read();
    }

    if (bufferLength > 0)
    {
        buffer[bufferLength] = '\0';
        this->onData(iotClient, buffer, (bufferLength));
    }
}

void IoTProtocol::loop()
{
    unsigned long now = millis();

    /* Read Clients */

    for (auto iotClient = this->clients.begin(); iotClient != this->clients.end(); iotClient++)
    {
        this->readClient(iotClient->second);

        /* Alive Request */
        if (now >= iotClient->second->aliveNextRequest)
        {
            /* Send Alive Request */
            IoTRequest aliveRequest = {
                IOT_VERSION,
                EIoTMethod::ALIVE_REQUEST,
                0,
                NULL,
                std::map<char *, char *>(),
                NULL,
                0,
                0,
                0,
                iotClient->second};
            IoTRequestResponse aliveRequestResponse = {
                NULL,
                &this->onAliveRequestTimeout,
                NULL,
                this->timeout};

            this->aliveRequest(&aliveRequest, &aliveRequestResponse);

            /* Schedule the next alive request */
            this->scheduleNextAliveRequest(iotClient->second);

            /* Read again for alive response */
            this->readClient(iotClient->second);
        }

        /* Timeout */
        if (iotClient->second != NULL)
        {
            IoTClient *c = iotClient->second;
            for (auto rr = iotClient->second->requestResponse.begin(); (c != nullptr) && (c->requestResponse.contains(rr->first)) && rr != c->requestResponse.end();)
            {
                unsigned long timeout = rr->second.timeout;
                if (now >= timeout)
                {
                    OnTimeout *onTimeout = rr->second.onTimeout;
                    if (onTimeout != NULL)
                    {
                        (*onTimeout)(&(rr->second.request));
                    }

                    c->requestResponse.erase(rr->first);

                    continue;
                }

                rr++;
            }

            /* MultiPart Timeout */
            auto multiPartControl = iotClient->second->multiPartControl;
            for (auto mpc = multiPartControl.begin(); mpc != multiPartControl.end(); mpc++)
            {
                unsigned long timeout = mpc->second.timeout;
                if (now >= timeout)
                {
                    iotClient->second->requestResponse.erase(mpc->first);
                    continue;
                }
            }
        }
    }
}

const char *IoTProtocol::getHeader(IoTRequest *request, const char *headerKey)
{
    for (auto header = request->headers.begin(); header != request->headers.end(); ++header)
    {
        if (strcmp(header->first, headerKey) == 0)
        {
            return header->second;
        }
    }

    return nullptr;
}