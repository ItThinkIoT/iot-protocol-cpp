#include "iot_helpers.h"


int indexOf(uint8_t * buffer, size_t bufLen, char c, size_t start) {
    int index = -1;

    for(size_t i = start; (i<bufLen && index == -1); i++) {
        if(buffer[i] == c) {
            index = i;
        }
    }

    return index;
}