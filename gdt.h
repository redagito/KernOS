#pragma once

#include "types.h"

namespace kernos
{

class GlobalDescriptorTable
{
private:

    class SegmentDescriptor
    {
    private:
        uint16_t limit_lo = 0;
        uint16_t base_lo = 0;
        uint8_t base_hi = 0;
        uint8_t flags = 0;
        uint8_t flags_limit_hi = 0;
        uint8_t base_vhi = 0;

    public:
        SegmentDescriptor(uint32_t base, uint32_t limit, uint8_t flags);
        uint32_t getBase() const;
        uint32_t getLimit() const;

    } __attribute__((packed));

    SegmentDescriptor m_nullSegmentSelector;
    SegmentDescriptor m_unusedSegmentSelector;
    SegmentDescriptor m_codeSegmentSelector;
    SegmentDescriptor m_dataSegmentSelector;

public:
    GlobalDescriptorTable();
    ~GlobalDescriptorTable();

    uint16_t codeSegmentSelector();
    uint16_t dataSegmentSelector();
};

}