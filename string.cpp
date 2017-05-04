#include "string.h"

namespace kernos
{

size_t string_length(const char* str) 
{
	size_t len = 0;
	while (str[len] != '\0')
    {
		len++;
    }
	return len;
}

void memory_copy(void* from, void* to, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        ((byte_t*)to)[i] = ((byte_t*)from)[i];
    }
}

}