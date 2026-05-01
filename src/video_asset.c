#include "f256lib.h"

#ifdef __OSCAR64__
/* oscar64 #embed is effectively capped at 64 KiB per object.
 * Split cockpit.bin into two contiguous chunks in memory. */
#pragma section( cockpit_sec_0, 0 )
#pragma region(  cockpit_reg_0, 0x6C000, 0x7C000, , , {cockpit_sec_0} )
#pragma data(cockpit_sec_0)
__export const char cockpit_bitmap[] = { 
    #embed "assets/cockpit_0.bin" 
};

#pragma section( cockpit_sec_1, 0 )
#pragma region(  cockpit_reg_1, 0x7C000, 0x7EC00, , , {cockpit_sec_1} )
#pragma data(cockpit_sec_1)
__export const char cockpit_bitmap_tail[] = {
    #embed "assets/cockpit_1.bin"
};

#pragma data(data)

#else /* llvm-mos */

EMBED(cockpit_bitmap, "assets/cockpit.bin", 0x6c000);

#endif /* __OSCAR64__ */
