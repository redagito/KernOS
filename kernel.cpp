#include "vga.h"
#include "print.h"

using namespace kernos;

// Support for pure virtual functions
extern "C" void __cxa_pure_virtual()
{
	// Nothing to do
}

// Constructors for global objects
using constructor = void(*)();

// Start of global constructors
extern "C" constructor start_ctors;

// End of global constructors
extern "C" constructor end_ctors;

extern "C" void call_constructors()
{
	// Call all constructors
	for (auto c = start_ctors; c != end_ctors; ++c)
	{
		(*c)();
	}
}

extern "C" void kernel_main(void* multi_boot, unsigned int magic_number)
{
	// Init
    terminal_initialize();
    
    // 
    print_info("Hello World!");
    print_error("Hello World!");
    print_ok("Hello World!");
	print_debug("Hello World!");

    while (true)
    {

    }
}