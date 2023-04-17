#include "iot_protocol.h"

IoTProtocol::IoTProtocol(unsigned long timeout, uint32_t delay)
{
    this->timeout = timeout;
    this->delay = delay;
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

void IoTProtocol::listen(Client *client)
{
    this->clients.push_back(client);
}

void IoTProtocol::onData(Client *client, uint8_t *buffer, size_t bufLen)
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

            // String key = String((buffer + offset), );
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

        auto multiPartControl = this->multiPartControl.find(request.id);
        if ((multiPartControl != this->multiPartControl.end()))
        {
            bodyEndIndex -= multiPartControl->second.received;
        }
        else
        {
            IoTMultiPart multiPart = {
                0,
                0,
                millis()};

            this->multiPartControl.insert(std::make_pair(request.id, multiPart));
            multiPartControl = this->multiPartControl.find(request.id);
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
            this->multiPartControl.erase(request.id);
        }

        if (bodyIncomeLength > request.bodyLength) /* Income more than one request, so keeps it on remainBuffer */
        {
            this->remainBufferLength = bufLen - bodyEndIndex;
            this->remainBuffer = (uint8_t *)(malloc(remainBufferLength * sizeof(uint8_t) + 1));
            memcpy(this->remainBuffer, (buffer + bodyEndIndex), this->remainBufferLength);
            this->remainBuffer[this->remainBufferLength] = '\0';
        }

        request.body = (uint8_t *)(malloc((request.bodyLength) * sizeof(uint8_t) + 1));
        memcpy(request.body, (buffer + offset), request.bodyLength);
        request.body[request.bodyLength] = '\0';

        offset = bodyEndIndex - 1;
    }

    /* Request Response */
    auto rr = this->requestResponse.find(request.id);
    if (rr != this->requestResponse.end())
    {
        if (rr->second.onResponse != NULL)
        {
            (*(rr->second.onResponse))(&request);
        }

        if (requestCompleted)
        {
            this->requestResponse.erase(request.id);
        }
        else
        {
            rr->second.timeout += this->timeout;
        }
    }
    else
    {
        if (request.method != EIoTMethod::RESPONSE)
        {
            /* Middleware */
            this->runMiddleware(&request);
        }
    }

    this->freeRequest(&request);
}

uint16_t IoTProtocol::generateRequestId()
{
    vTaskDelay(1);
    uint16_t id = (uint16_t)(millis() % 10000);
    if (this->requestResponse.find(id) != this->requestResponse.end() || id == 0)
    {
        return this->generateRequestId();
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
    uint8_t headerSize = request->headers.size() & 255;
    if (LSCB & IOT_LSCB_HEADER)
    {
        for (auto header = request->headers.begin(); header != request->headers.end(); ++header)
        {
            headersLength += strlen(header->first) + strlen(header->second) + 2; /* + 1 (RS) + 1 (EXT) */
        }
        dataLength += headersLength + 1; /* +1 (headerSize) */
    }

    if ((pathLength + headersLength) > IOT_PROTOCOL_BUFFER_SIZE - 8)
    {
        throw "Path and Headers too big.";
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

    std::function<size_t(size_t, size_t)> writeBodyPart = [&writeBodyPart, this, request, prefixDataIndex, &data](size_t i = 0, size_t parts = 0)
    {
        size_t indexData = prefixDataIndex + 1; // 42
        /* Body */
        if (request->bodyLength > 0) // 1060 > 0
        {
            size_t bodyBufferRemain = (request->bodyLength - i);
            size_t bodyUntilIndex = ((bodyBufferRemain + indexData) > IOT_PROTOCOL_BUFFER_SIZE) ? i + (IOT_PROTOCOL_BUFFER_SIZE - indexData) : i + bodyBufferRemain;
            for (; i < bodyUntilIndex; i++)
            {
                *(data + (indexData++)) = *(request->body + i);
            }
        }

        *(data + (indexData)) = '\0';
        request->client->write(data, indexData);

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
            requestResponse->timeout = this->timeout;
        }
        requestResponse->timeout += millis();
        requestResponse->request = *request;

        this->requestResponse.insert(std::make_pair(request->id, *requestResponse));
    }

    this->readClient(request->client);

    return request;
}

void IoTProtocol::resetRemainBuffer()
{
    this->remainBufferLength = 0;
    if (this->remainBuffer != NULL)
    {
        free(this->remainBuffer);
    }
    this->remainBuffer = NULL;
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

void IoTProtocol::resetClients()
{
    this->clients.clear();
    this->resetRemainBuffer();
}

void IoTProtocol::readClient(Client *client)
{
    if (!(client->connected()))
    {
        return;
    }
    uint8_t buffer[IOT_PROTOCOL_BUFFER_SIZE + 1];
    size_t bufferLength = 0;

    if (this->remainBuffer != NULL)
    {
        for (uint32_t i = 0; i < this->remainBufferLength; i++)
        {
            buffer[bufferLength++] = *(this->remainBuffer + i);
        }

        this->resetRemainBuffer();
    }

    while (client->available() && bufferLength < IOT_PROTOCOL_BUFFER_SIZE)
    {
        buffer[bufferLength++] = client->read();
    }

    if (bufferLength > 0)
    {
        buffer[bufferLength] = '\0';
        this->onData(client, buffer, (bufferLength));
    }
}

void IoTProtocol::loop()
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
            if (onTimeout != NULL)
            {
                (*onTimeout)(&(rr->second.request));
            }

            this->requestResponse.erase(rr->second.request.id);
            continue;
        }
    }

    /* MultiPart Timeout */
    for (auto mpc = this->multiPartControl.begin(); mpc != this->multiPartControl.end(); ++mpc)
    {
        unsigned long timeout = mpc->second.timeout;
        if (now >= timeout)
        {
            this->requestResponse.erase(mpc->first);
            continue;
        }
    }
}