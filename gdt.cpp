#include "gdt.h"

namespace kernos
{

GlobalDescriptorTable::SegmentDescriptor::SegmentDescriptor(uint32_t base, uint32_t limit, uint8_t flags)
{
    uint8_t* target = (uint8_t*) this;

    if (limit <= 64 * 1024 * 1024)
    {
        // 64 kB limit
        target[6] = 0x40;
    }
    else
    {
        if ((limit & 0xFFF) != 0xFFF)
        {
            limit = (limit >> 12) - 1;
        }
        else
        {
            limit = limit >> 12;
        }
        target[6] = 0xC0;
    }

    target[0] = limit & 0xFF;
    target[1] = (limit >> 8) & 0xFF;
    target[6] |= (limit >> 16) & 0xF;

    target[2] = base & 0xFF;
    target[3] = (base >> 8) & 0xFF;
    target[4] = (base >> 16) & 0xFF;
    target[7] = (base >> 24) & 0xFF;

    // Access rights
    target[5] = flags;
}

uint32_t GlobalDescriptorTable::SegmentDescriptor::getBase() const
{
    uint8_t* target = (uint8_t*) this;

    uint32_t result = target[7];
    result = (result << 8) + target[4];
    result = (result << 8) + target[3];
    result = (result << 8) + target[2];

    return result;
}

uint32_t GlobalDescriptorTable::SegmentDescriptor::getLimit() const
{
    uint8_t* target = (uint8_t*) this;

    uint32_t result = target[6] & 0xF;
    result = (result << 8) + target[1];
    result = (result << 8) + target[0];

    if ((target[6] & 0xC0) == 0xC0)
    {
        result = (result << 12) |0xFFF;
    }
    return result;
}

GlobalDescriptorTable::GlobalDescriptorTable()
    : m_nullSegmentSelector(0, 0, 0)
    , m_unusedSegmentSelector(0, 0, 0)
    , m_codeSegmentSelector(0, 64 * 1024 * 1024, 0x9A)
    , m_dataSegmentSelector(0, 64 * 1024 * 1024, 0x92)
{
    // Tell processor to use the GDT
    uint32_t i[2];
    // Size of global descriptor table as high byte
    i[0] = sizeof(GlobalDescriptorTable) << 16;
    i[1] = (uint32_t) this;

    asm volatile("lgdt (%0)": :"p" (((uint8_t*) i) +2));
}

GlobalDescriptorTable::~GlobalDescriptorTable()
{
    // Nothing to do
    // Should unload GDT
}

uint16_t GlobalDescriptorTable::codeSegmentSelector()
{
    return (uint8_t*) &m_dataSegmentSelector - (uint8_t*)this;
}

uint16_t GlobalDescriptorTable::dataSegmentSelector()
{
    return (uint8_t*) &m_codeSegmentSelector - (uint8_t*)this;
}

}