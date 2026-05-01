#include "f256lib.h"

#ifdef __OSCAR64__
/* oscar64: place each asset into its own section/region at the fixed
 * physical address that matches the _ADDR constants in vgm_assets.h.     */

#pragma section( vgm_kick_center_sec, 0 )
#pragma region(  vgm_kick_center_reg, 0x16000, 0x17FFF, , , {vgm_kick_center_sec} )
#pragma data(vgm_kick_center_sec)
__export const char kick_center[] = { 
    #embed "assets/kick_center.vgm" 
};
#pragma data(data)

#pragma section( vgm_kick_left_sec, 0 )
#pragma region(  vgm_kick_left_reg, 0x18000, 0x19FFF, , , {vgm_kick_left_sec} )
#pragma data(vgm_kick_left_sec)
__export const char kick_left[] = { 
    #embed "assets/kick_left.vgm" 
};
#pragma data(data)

#pragma section( vgm_kick_right_sec, 0 )
#pragma region(  vgm_kick_right_reg, 0x1A000, 0x1BFFF, , , {vgm_kick_right_sec} )
#pragma data(vgm_kick_right_sec)
__export const char kick_right[] = { 
    #embed "assets/kick_right.vgm" 
};
#pragma data(data)

#pragma section( vgm_water_sec, 0 )
#pragma region(  vgm_water_reg, 0x20000, 0x30000, , , {vgm_water_sec} )
#pragma data(vgm_water_sec)
__export const char water_vgm[] = { 
    #embed "assets/water_0.vgm" 
};

#pragma section( vgm_water_sec_1, 0 )
#pragma region(  vgm_water_reg_1, 0x30000, 0x356A5, , , {vgm_water_sec_1} )
#pragma data(vgm_water_sec_1)
__export const char water_vgm_tail[] = {
    #embed "assets/water_1.vgm"
};

#pragma data(data)

#else /* llvm-mos */

EMBED(kick_center, "assets/kick_center.vgm", 0x16000);
EMBED(kick_left, "assets/kick_left.vgm", 0x18000);
EMBED(kick_right, "assets/kick_right.vgm", 0x1a000);
EMBED(water_vgm, "assets/water.vgm", 0x20000);

#endif /* __OSCAR64__ */

