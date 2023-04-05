#include "iot_protocol.h"

IoTApp::IoTApp()
{
}

void IoTApp::use(IoTMiddleware middleware)
{
    this->middlewares.push_back(middleware);
}

void IoTApp::runMiddleware(IoTRequest *request, int index = 0)
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

    /* If is the last middleware */
    if (index == this->middlewares.size() - 1)
    {
        /* Free memory */
        free(request->path);
        free(request->body);
    }
}

void IoTApp::listen(Client *client)
{
    this->clients.push_back(client);
}

void IoTApp::onData(Client *client, uint8_t *buffer, size_t bufLen)
{

    IoTRequest request = {
        0,
        EIoTMethod::SIGNAL,
        0,
        NULL,
        std::map<String, String>(),
        NULL,
        0,
        client};

    int foundN = 0;
    int indexStart = 0; // 9
    int indexN = -1;
    bool isBody = false;
    uint8_t *remainBuffer = NULL; /* Reamins data on buffe rto be processed */
    size_t remainBufferSize = 0;

    bool invalidRequest = false;

    do
    {
        indexN = indexOf(buffer, bufLen, '\n', indexStart);

        uint8_t *bufferPart = buffer + indexStart;
        size_t bufferPartLength = ((indexN == -1 || isBody) ? (bufLen + 1) : indexN) - indexStart;

        switch (foundN)
        {
        case 0:
            if (bufferPartLength > 1)
            {
                invalidRequest = true;
                continue;
            }
            request.version = static_cast<byte>((char)bufferPart[0]);
            break;
        case 1:
            /* Fix: ID = [\n, *] | [*, \n] | [\n,\n] */
            if (bufferPartLength != 3)
            {
                indexN = indexStart + 3;
                if (buffer[indexN] == ((uint8_t)(0xA)) /* (0xA = \n) */)
                {
                    bufferPartLength = 3;
                }
                else
                {
                    invalidRequest = true;
                    continue;
                }
            }

            request.method = static_cast<EIoTMethod>((char)bufferPart[0]);
            request.id = (bufferPart[1] << 8) + bufferPart[2];
            break;
        case 2:
            request.path = static_cast<char *>(malloc(bufferPartLength * sizeof(char) + 1));
            memcpy(request.path, bufferPart, bufferPartLength);
            request.path[bufferPartLength] = '\0';
            break;
        default:
            // B:1 como header mas está vindo como body  ?soluttion:  add auto B+_ ?
            /* Body */
            if (isBody == false && static_cast<EIoTRequestPart>((char)bufferPart[0]) == EIoTRequestPart::BODY && bufferPartLength <= 3)
            {
                /* Fix: BODY length = [\n, *] | [*, \n] | [\n,\n] */
                if (bufferPartLength != 3)
                {
                    indexN = indexStart + 3;
                    if (buffer[indexN] == ((uint8_t)(0xA)) /* (0xA = \n) */)
                    {
                        bufferPartLength = 3;
                    }
                    else
                    {
                        invalidRequest = true;
                        continue;
                    }
                }

                request.bodyLength = (bufferPart[1] << 8) + bufferPart[2];
                isBody = true;
                break;
            }
            /* Headers */
            int indexKeyValue = indexOf(bufferPart, bufferPartLength, ':');
            if (indexKeyValue > 1 && isBody == false)
            {
                /* |B|0|58 || |B|58|0 */
                char cBufferPart[bufferPartLength];
                memcpy(cBufferPart, bufferPart, bufferPartLength);

                String allHeader = String(cBufferPart, bufferPartLength);

                String key = allHeader.substring(0, indexKeyValue);
                key.trim();
                String value = allHeader.substring(indexKeyValue + 1);
                value.trim();

                request.headers.insert(std::make_pair(key, value));

                break;
            }

            if (isBody)
            {
                request.body = (uint8_t *)(malloc(request.bodyLength * sizeof(uint8_t) + 1));
                memcpy(request.body, bufferPart, request.bodyLength);
                request.body[request.bodyLength] = '\0';
                if (request.bodyLength < bufferPartLength)
                {
                    remainBuffer = (bufferPart + request.bodyLength);
                    remainBufferSize = bufferPartLength - request.bodyLength;
                }
                break;
            }
        }

        foundN++;
        indexStart = indexN + 1;

    } while (indexN >= 0 && invalidRequest == false && remainBuffer == NULL);

    if (invalidRequest)
        return;

    /* Response */
    if (request.method == EIoTMethod::RESPONSE)
    {
        auto rr = this->requestResponse.find(request.id);
        if (rr != this->requestResponse.end())
        {
            if (rr->second.onResponse != NULL)
            {
                (*(rr->second.onResponse))(&request);
                // (rr->second.onResponse)(&request);

                /* free body and path ?! hehe */
                free(request.path);
                free(request.body);
            }
            this->requestResponse.erase(request.id);
        }
    }
    else
    {
        /* Middleware */
        this->runMiddleware(&request);
    }

    if (remainBuffer != NULL)
    {
        this->onData(client, remainBuffer, remainBufferSize);
    }
}

uint16_t IoTApp::generateRequestId()
{
    vTaskDelay(1);
    uint16_t id = (uint16_t)(millis() % 10000);
    if (this->requestResponse.find(id) != this->requestResponse.end() || id == 0)
    {
        return this->generateRequestId();
    }
    return id;
}

IoTRequest *IoTApp::send(IoTRequest *request, IoTRequestResponse *requestResponse)
{

    if (request->version == 0)
    {
        request->version = IOT_VERSION;
    }

    uint8_t MCB = request->version << 2;
    uint8_t LCB = (uint8_t)(request->method) << 2;

    uint8_t bodyLengthSize = 2;

    LCB += (((request->headers.size() > 0) ? IOT_LCB_HEADER : 0) + ((request->body != NULL) ? IOT_LCB_BODY : 0));

    switch (request->method)
    {
    case EIoTMethod::SIGNAL:
        MCB += (((request->path != NULL) ? IOT_MCB_PATH : 0));

        bodyLengthSize = 1;
        break;
    case EIoTMethod::REQUEST:
        MCB += ((IOT_MCB_ID) + ((request->path != NULL) ? IOT_MCB_PATH : 0));
        break;
    case EIoTMethod::RESPONSE:
        MCB += ((IOT_MCB_ID));
        break;
    case EIoTMethod::STREAMING:
        MCB += ((IOT_MCB_ID) + ((request->path != NULL) ? IOT_MCB_PATH : 0));

        bodyLengthSize = 4;
        break;
    }

    /* Sum Total Data Length */

    size_t dataLength = 2;

    if (MCB & IOT_MCB_ID)
    {
        if (request->id == 0)
        {
            request->id = this->generateRequestId();
        }
        dataLength += 2;
    }

    size_t pathLength = 0;
    if (MCB & IOT_MCB_PATH)
    {
        pathLength = strlen(request->path);
        dataLength += pathLength + 1 /* (EXT) */;
    }

    String headers = "";
    size_t headersLength = 0;
    if (LCB & IOT_LCB_HEADER)
    {
        for (auto header = request->headers.begin(); header != request->headers.end(); ++header)
        {
            headersLength += header->first.length() + header->second.length() + 2; /* + 1 (RS) + 1 (EXT) */
        }

        dataLength += headersLength;
    }

    if (LCB & IOT_LCB_BODY)
    {
        dataLength += bodyLengthSize + request->bodyLength;
    }

    /* Record Data */

    uint8_t data[dataLength + 1]; /* +1 => (\0) */
    size_t nextIndex = 0;

    data[nextIndex] = MCB;
    data[++nextIndex] = LCB;

    /* ID */
    if (MCB & IOT_MCB_ID)
    {
        data[++nextIndex] = request->id >> 8;                     /* Id as Big Endian - (MSB first) */
        data[++nextIndex] = request->id - (data[nextIndex] << 8); /* Id as Big Endian - (LSB last)  */
    }

    /* PATH */
    if (MCB & IOT_MCB_PATH)
    {
        for (size_t i = 0; i < pathLength; i++)
        {
            data[++nextIndex] = *(request->path + i);
        }
        data[++nextIndex] = IOT_ETX;
    }

    /* HEADERs */
    if (LCB & IOT_LCB_HEADER)
    {
        for (auto header = request->headers.begin(); header != request->headers.end(); ++header)
        {
            /* Key */
            size_t keyLength = header->first.length();
            for (size_t i = 0; i < keyLength; i++)
            {
                data[++nextIndex] = header->first.charAt(i);
            }

            /* RS */
            data[++nextIndex] = IOT_RS;
            
            /* Value */
            size_t valueLength = header->second.length();
            for (size_t i = 0; i < valueLength; i++)
            {
                data[++nextIndex] = header->second.charAt(i);
            }

            /* EXT */
            data[++nextIndex] = IOT_ETX;
        }
    }

    /* BODY */
    if (LCB & IOT_LCB_BODY)
    {
        switch (bodyLengthSize)
        {
        case 1: /* 0b00000000 */
            data[++nextIndex] = request->bodyLength;
            break;
        case 4:                                            /* 0b00000000 00000000 00000000 00000000 */
            data[++nextIndex] = request->bodyLength >> 24; /* Body Length as Big Endian - (MSB first) */
            data[++nextIndex] = (request->bodyLength >> 16) & (255);
            data[++nextIndex] = (request->bodyLength >> 8) & (255);
            data[++nextIndex] = (request->bodyLength) & (255); /* Body Length as Big Endian - (LSB last)  */
            break;
        default:                                               /* 0b00000000 00000000 */
            data[++nextIndex] = request->bodyLength >> 8;      /* Body Length as Big Endian - (MSB first) */
            data[++nextIndex] = (request->bodyLength) & (255); /* Body Length as Big Endian - (LSB last)  */
            break;
        }

        for (size_t i = 0; i < request->bodyLength; i++)
        {
            data[++nextIndex] = *(request->body + i);
        }
    }

    data[++nextIndex] = '\0';

    if (requestResponse != NULL)
    {
        if (requestResponse->timeout == 0)
        {
            requestResponse->timeout = 1000;
        }
        requestResponse->timeout += millis();
        requestResponse->request = *request;

        this->requestResponse.insert(std::make_pair(request->id, *requestResponse));
    }

    request->client->write(data, dataLength);

    return request;
}

void IoTApp::resetClients()
{
    this->clients.clear();
}

void IoTApp::loop()
{

    for (auto client : this->clients)
    {
        uint8_t buffer[IOT_PROTOCOL_MAX_READ_LENGTH];
        int indexBuffer = 0;
        while (client->available() && indexBuffer < IOT_PROTOCOL_MAX_READ_LENGTH - 1)
        {
            buffer[indexBuffer] = client->read();
            indexBuffer++;
        }

        if (indexBuffer > 0)
        {
            buffer[indexBuffer] = '\0';
            this->onData(client, buffer, (indexBuffer - 1));
        }
    }

    /* Timeout */
    unsigned long now = millis();
    for (auto rr = this->requestResponse.begin(); rr != this->requestResponse.end(); ++rr)
    {
        unsigned long timeout = rr->second.timeout;
        if (now >= timeout)
        {
            OnTimeout *onTimeout = rr->second.onTimeout;
            (*onTimeout)(&(rr->second.request));

            this->requestResponse.erase(rr->second.request.id);
            continue;
        }
    }
}