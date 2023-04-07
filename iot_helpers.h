#pragma once

#ifndef __IOT_HELPERS_H__
#define __IOT_HELPERS_H__

#include "Arduino.h"

int indexOf(uint8_t *buffer, size_t bufLen, uint8_t value, size_t start = (size_t)0);

#endif