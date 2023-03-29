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
    // client->onData((this->handleData), NULL);
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

            /* Headers */
            int indexKeyValue = indexOf(bufferPart, bufferPartLength, ':');
            if (indexKeyValue > 0 && isBody == false)
            {
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

            /* Body */
            if (isBody == false && bufferPartLength == 1 && static_cast<EIoTRequestPart>((char)bufferPart[0]) == EIoTRequestPart::BODY)
            {
                isBody = true;
                break;
            }

            if (isBody)
            {
                request.body = (uint8_t *)(malloc(bufferPartLength * sizeof(uint8_t) + 1));
                memcpy(request.body, bufferPart, bufferPartLength);
                request.body[bufferPartLength] = '\0';
                request.bodyLength = bufferPartLength;
                isBody = false;
                break;
            }
        }

        foundN++;
        indexStart = indexN + 1;

    } while (indexN >= 0 && invalidRequest == false);

    if (invalidRequest)
        return;

    /* Response */
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
        return;
    }
    if (request.method == EIoTMethod::RESPONSE)
    {
        return;
    }

    /* Middleware */
    this->runMiddleware(&request);
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

    if (request->id == 0)
    {
        request->id = this->generateRequestId();
    }

    bool shouldFreePath = false;
    if (request->path == NULL)
    {
        request->path = static_cast<char *>(malloc(1 * sizeof(char) + 1));
        request->path[0] = '/';
        request->path[1] = '\0';
        shouldFreePath = true;
    }
    size_t pathLength = strlen(request->path);

    String headers = "";
    for (auto header = request->headers.begin(); header != request->headers.end(); ++header)
    {
        headers += header->first + ":" + header->second + "\n";
    }

    size_t dataLength = 7 + pathLength + headers.length(); /* 1 + 1 + 1 + 2 + 1  (version+\n+method+id+\n) + pathLength + 1 (\n) + headers.length() */

    if (request->body != NULL)
    {
        dataLength += 2 + request->bodyLength; /* 1 + 1 (B+\n) + request->bodyLength */
    }

    uint8_t data[dataLength + 1]; /* +1 (\0) */
    data[0] = request->version;
    data[1] = '\n';
    data[2] = static_cast<uint8_t>((char)request->method);
    data[3] = request->id >> 8;             /* Id as Big Endian - (MSB first) */
    data[4] = request->id - (data[3] << 8); /* Id as Big Endian - (LSB last)  */
    data[5] = '\n';

    size_t nextIndex = 5;

    for (size_t i = 0; i < pathLength; i++)
    {
        data[++nextIndex] = *(request->path + i);
    }

    data[++nextIndex] = '\n';
    
    if (headers.length() > 0)
    {
        for (size_t i = 0; i < headers.length(); i++)
        {
            data[++nextIndex] = (headers.charAt(i));
        }
    }

    if (request->body != NULL)
    {
        data[++nextIndex] = 'B';
        data[++nextIndex] = '\n';
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
    
    if (shouldFreePath)
    {
        free(request->path);
    }

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
        while (client->available())
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