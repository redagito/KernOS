#pragma once

#include "types.h"

namespace kernos
{

/**
 * @return Size of the string excluding null terminator
 */
size_t string_length(const char* str);

void memory_copy(void* from, void* to, size_t size);

}