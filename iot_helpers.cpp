#include "iot_helpers.h"

int indexOf(uint8_t *buffer, size_t bufLen, uint8_t value, size_t start)
{
    int index = -1;

    for (size_t i = start; (i < bufLen && index == -1); i++)
    {
        if (buffer[i] == value)
        {
            index = i;
        }
    }

    return index;
}