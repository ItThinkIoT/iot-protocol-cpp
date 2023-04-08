#include "iot_protocol.h"

IoTApp::IoTApp(uint32_t delay)
{
    this->delay = delay;
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
        for (auto header = request->headers.begin(); header != request->headers.end(); header++)
        {
            free(header->first);
            free(header->second);
        }
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
        std::map<char *, char *>(),
        NULL,
        0,
        0,
        client};

    size_t offset = 0;

    if (bufLen < offset)
        return;

    uint8_t MSCB = buffer[offset];
    uint8_t LSCB = buffer[++offset];

    request.version = MSCB >> 2;
    request.method = (EIoTMethod)(LSCB >> 2);

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

        while ((indexRS = indexOf(buffer, bufLen, IOT_RS, offset)) && ((indexEXT = indexOf(buffer, bufLen, IOT_ETX, offset + 1)) != -1) && indexRS < indexEXT - 1)
        {
            size_t keyLength = (indexRS - offset);

            char *headerKey = (char *)malloc(keyLength * sizeof(char) + 1);
            memcpy(headerKey, (buffer + offset), keyLength);
            headerKey[keyLength] = '\0';

            size_t valueLength = (indexEXT - (indexRS + 1));
            char *headerValue = (char *)malloc(valueLength * sizeof(char) + 1);
            memcpy(headerValue, (buffer + indexRS + 1), valueLength);
            headerValue[valueLength] = '\0';

            // String key = String((buffer + offset), );
            request.headers.insert(std::make_pair(headerKey, headerValue));

            offset = indexEXT + 1;
        }

        offset--;
    }

    /* BODY */
    uint8_t *remainBuffer = NULL; /* Reamins data on buffe rto be processed */
    size_t remainBufferSize = 0;

    if (LSCB & IOT_LSCB_BODY)
    {

        uint8_t bodyLengthSize = 2;
        switch (request.method)
        {
        case EIoTMethod::SIGNAL:
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

        /* Single Request */
        /* ...(17) EXT (18) 0 (19) 30 | (20) B (21) B (22) B + ...25B + (48) B , (49) B , (50) */

        /* Multi Request */
        /* ...(17) EXT (18) 4 (19) 36 | (20) B (21) B (22) B + ...999B + (1022) B , (1023) B , (1024) */
        /* ...(17) EXT (18) 4 (19) 36 | (20) B (21) B (22) B + ...51B + (74) B , (75) B , (76) */

        offset++; //20 | 20
        size_t bodyEndIndex = offset + request.bodyLength;
        size_t bodyIncomeLength = (bufLen - offset); 

        if (bodyIncomeLength > request.bodyLength) /* Income more than one request, so forward to next onData(remainBuffer) */
        {
            remainBuffer = (buffer + bodyEndIndex);
            remainBufferSize = bufLen - bodyEndIndex;
        }
        else if (bodyIncomeLength < request.bodyLength)
        { /* Part Body data */
            bodyEndIndex = bufLen;
        }

        request.body = (uint8_t *)(malloc(bodyIncomeLength * sizeof(uint8_t) + 1));
        memcpy(request.body, (buffer + offset), bodyIncomeLength);
        request.body[bodyIncomeLength] = '\0';

        offset = bodyEndIndex - 1;
    }

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

                /* Free Request */
                free(request.path);
                free(request.body);
                for (auto header = request.headers.begin(); header != request.headers.end(); header++)
                {
                    free(header->first);
                    free(header->second);
                }
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

IoTRequest *IoTApp::signal(IoTRequest *request) {
    request->method = EIoTMethod::SIGNAL;
    return this->send(request, NULL);
}

IoTRequest *IoTApp::send(IoTRequest *request, IoTRequestResponse *requestResponse)
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
    }

    /* Sum Total Data Length */

    size_t dataLength = 2;

    if (MSCB & IOT_MSCB_ID)
    {
        if (request->id == 0)
        {
            request->id = this->generateRequestId();
        }
        dataLength += 2;
    }

    size_t pathLength = 0;
    if (MSCB & IOT_MSCB_PATH)
    {
        pathLength = strlen(request->path);
        dataLength += pathLength + 1 /* (EXT) */;
    }

    String headers = "";
    size_t headersLength = 0;
    if (LSCB & IOT_LSCB_HEADER)
    {
        for (auto header = request->headers.begin(); header != request->headers.end(); ++header)
        {
            headersLength += strlen(header->first) + strlen(header->second) + 2; /* + 1 (RS) + 1 (EXT) */
        }

        dataLength += headersLength;
    }

    if ((pathLength + headersLength) > IOT_PROTOCOL_BUFFER_SIZE - 8)
    {
        throw "Path and Headers too big";
    }

    if (LSCB & IOT_LSCB_BODY)
    {
        dataLength += bodyLengthSize + request->bodyLength;
    }
    else
    {
        bodyLengthSize = 0;
    }

    if (dataLength > IOT_PROTOCOL_BUFFER_SIZE)
    {
        dataLength = IOT_PROTOCOL_BUFFER_SIZE;
    }

    /* Record Data */

    uint8_t data[dataLength + 1]; /* +1 => (\0) */
    size_t nextIndex = 0;

    data[nextIndex] = MSCB;
    data[++nextIndex] = LSCB;

    /* ID */
    if (MSCB & IOT_MSCB_ID)
    {
        data[++nextIndex] = request->id >> 8;                     /* Id as Big Endian - (MSB first) */
        data[++nextIndex] = request->id - (data[nextIndex] << 8); /* Id as Big Endian - (LSB last)  */
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

    std::function<size_t(size_t, size_t)> writeBodyPart = [&writeBodyPart, this, request, prefixDataIndex, &data](size_t i = 0, size_t parts = 0)
    {
        size_t indexData = prefixDataIndex + 1; // 42
        /* Body */
        if (request->bodyLength > 0) // 1060 > 0
        {
            size_t bodyBufferRemain = (request->bodyLength - i);                                                                                                     // 1984  //
            size_t bodyUntilIndex = ((bodyBufferRemain + indexData) > IOT_PROTOCOL_BUFFER_SIZE) ? i + (IOT_PROTOCOL_BUFFER_SIZE - indexData) : i + bodyBufferRemain; // 1004 //1060
            for (; i <= bodyUntilIndex; i++)                                                                                                                         //[0-1004] //[1005 - 1060]
            {
                // i = 1005 indexData = 21 ... i = 1060  indexData = 76
                *(data + (indexData++)) = *(request->body + i);
                // i = 1006 indexData = 22 ... i = 1061  indexData = 77
            }
            i--;
        }

        *(data + (indexData)) = '\0';
        request->client->write(data, indexData - 1);
        request->client->flush();
        vTaskDelay(this->delay);

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

    request->parts = writeBodyPart(0, 0);

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

    return request;
}

void IoTApp::resetClients()
{
    this->clients.clear();
}

void IoTApp::readClient(Client *client)
{
    if (!(client->connected()))
    {
        return;
    }
    uint8_t buffer[IOT_PROTOCOL_BUFFER_SIZE + 1];
    size_t bufferLength = 0;
    while (client->available() && bufferLength < IOT_PROTOCOL_BUFFER_SIZE)
    {
        buffer[bufferLength++] = client->read();
    }

    if (bufferLength > 0)
    {
        buffer[bufferLength] = '\0';
        this->onData(client, buffer, (bufferLength));
    }

    if (bufferLength >= IOT_PROTOCOL_BUFFER_SIZE)
    {
        return this->readClient(client);
    }
}

void IoTApp::loop()
{
    /* Read Clients */
    for (auto client : this->clients)
    {
        this->readClient(client);
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