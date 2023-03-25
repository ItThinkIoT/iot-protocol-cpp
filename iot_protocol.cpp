#include "iot_protocol.h"

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
    if(index == this->middlewares.size()-1) {
        /* Free memory */
        free(request->path);
        free(request->body);
    }
}

void IoTApp::listen(Client *client)
{
    this->clients.push_back(client);
}

void IoTApp::resetClients() {
    this->clients.clear();
}

void IoTApp::onData(Client *client, uint8_t *buffer, size_t bufLen)
{

    IoTRequest request = {
        1,
        EIoTMethod::SIGNAL,
        NULL,
        std::map<String, String>(),
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

        uint8_t *bufferPartStart = buffer + indexStart;
        size_t bufferPartLength = ((indexN == -1 || isBody) ? (bufLen + 1) : indexN) - indexStart;

        uint8_t bufferPart[bufferPartLength];
        for (size_t i = 0; i < bufferPartLength; i++)
        {
            bufferPart[i] = bufferPartStart[i];
        }

        // [ 0 \n 1 2 \n 3 4 5 \n 6 7 8 9]
        switch (foundN)
        {
        case 0:
            if(bufferPartLength > 1) {
                invalidRequest = true;
                continue;
            }
            request.version = static_cast<byte>((char)bufferPart[0]);
            break;
        case 1:
            request.method = static_cast<EIoTMethod>((char)bufferPart[0]);
            break;
        case 2:
            request.path = static_cast<char *>(malloc(bufferPartLength * sizeof(char) + 1));
            memcpy(request.path, bufferPartStart, bufferPartLength);
            request.path[bufferPartLength] = '\0';
            /* @TODO: define params */
            break;
        default:

            /* Headers */
            //  const indexKeyValue = bufferPart.indexOf(":");
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
                memcpy(request.body, bufferPartStart, bufferPartLength);
                request.body[bufferPartLength] = '\0';
                request.bodyLength = bufferPartLength;
                isBody = false;
                break;
            }
        }

        foundN++;
        indexStart = indexN + 1;

    } while (indexN >= 0 && invalidRequest == false);

    if (invalidRequest) return;


    this->runMiddleware(&request);
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
}