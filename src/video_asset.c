#include "f256lib.h"

#ifdef __OSCAR64__

#pragma section( cockpit_sec, 0 )
#pragma region(  cockpit_reg, 0x6C000, 0x7EC00, , , {cockpit_sec} )
#pragma data(cockpit_sec)
__export const char cockpit_bitmap[] = { 
    #embed 76800 0 "assets/cockpit.bin" 
};
#pragma data(data)

#else /* llvm-mos */

EMBED(cockpit_bitmap, "assets/cockpit.bin", 0x6c000);

#endif /* __OSCAR64__ */
