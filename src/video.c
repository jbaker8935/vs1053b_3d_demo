#include "../include/video.h"

#include "f256lib.h"

#define DMA_FILL_VAL16   0xDF02
#define DMA_16_BIT      0x40
uint32_t bitmap_base[] = {0x6c000, 0x58000, 0x44000};

__attribute__((optnone,noinline))
void dmaBitmapClear(uint8_t layer) {

	asm("sei");
	POKE(DMA_CTRL, DMA_CTRL_FILL | DMA_CTRL_ENABLE | DMA_16_BIT);
	POKEA(DMA_DST_ADDR, bitmap_base[layer]);
	POKEA(DMA_COUNT, 0x12C00); 
	POKEW(DMA_FILL_VAL16, 0x0000);

	POKE(DMA_CTRL, PEEK(DMA_CTRL) | DMA_CTRL_START);
	// Wait for DMA to complete with timeout to avoid hangs
	uint16_t timeout = 12000; // generous safety margin
	while (timeout-- > 0) {
		if ((PEEK(0xDF01) & 0x80) == 0) { // Busy bit clear
			break;
		}
	}
	POKE(0xDF00, 0x00); // Stop DMA

	asm("cli");

}

void video_init(void) {
	textClear();
    textDefineForegroundColor(1,0xaa,0xaa,0xaa); // #aaaaaa Normal Text
    textDefineBackgroundColor(1, 40,40,40); 
	textEnableBackgroundColors(true);   
	textSetColor(1, 1);

	// Initialize Layer 0 with cockpit overlay
	graphicsSetLayerBitmap(0, 0);
	bitmapSetActive(0);
	bitmapSetCLUT(1); // Use CLUT 1 for cockpit overlay
	bitmapSetVisible(0, false);  // start hidden, will be shown in demo 3
	
	graphicsDefineColor(1, 0, 0, 0, 0);
	graphicsDefineColor(1, 1, 7, 7, 7);  // #070707 Very Dark Gray (used for cockpit background in demo 3)
	graphicsDefineColor(1, 2, 7, 3, 231);  // #0703e7 Bright Purple (used for cockpit details in demo 3)
	graphicsDefineColor(1, 3, 7, 3, 119);  // #070377 Bright Blue 
	graphicsDefineColor(1, 4, 7, 115, 119);   // #077377 Teal 
	graphicsDefineColor(1, 5, 119, 3, 7);  // #770307 Bright Red (used for cockpit details in demo 3)
	graphicsDefineColor(1, 6, 231, 227, 231);  // #e7e3e7 Light Purple (used for cockpit details in demo 3)
	graphicsDefineColor(1, 7, 7, 227, 7);  // #07e307 Bright Green (used for cockpit details in demo 3)
	graphicsDefineColor(1, 8, 7, 115, 7);  // #077307 Bright Green (used for cockpit details in demo 3)
	graphicsDefineColor(1, 9, 231, 227, 7);  // #e7e307 Light Yellow (used for cockpit details in demo 3)
	graphicsDefineColor(1, 10, 231, 3, 7); // #e70307 Light Red (used for cockpit details in demo 3)
	graphicsDefineColor(1, 11, 119, 115, 7); // #777307 Olive Green (used for cockpit details in demo 3)


	// Initialize Layer 1 for double buffering
	graphicsSetLayerBitmap(1, 1);
	bitmapSetActive(1);
	bitmapSetCLUT(0); // Use same CLUT
	bitmapSetVisible(1, true); // Start visible
	bitmapSetColor(0);
	bitmapClear();


	// Initialize Layer 2 for double buffering
	graphicsSetLayerBitmap(2, 2);
	bitmapSetActive(2);
	bitmapSetCLUT(0); // Use same CLUT
	bitmapSetVisible(2, false); // Start hidden
	bitmapSetColor(0);
	bitmapClear();	

	graphicsDefineColor(0, 0, 0, 0, 0);
	graphicsDefineColor(0, 1, 0, 255, 239);  // #00ffe0 Bright Cyan (used for near color in demo)
	graphicsDefineColor(0, 2, 255, 0, 150);  // #ff0096 Bright Magenta (used for far color in demo)
	graphicsDefineColor(0, 3, 180, 255, 0);  // #b4ff00 Bright Lime (used for near color in demo 4)
	graphicsDefineColor(0, 4, 255, 85, 0);   // #ff5500 Bright Orange (used for far color in demo 4)	
	graphicsDefineColor(0, 5, 110, 0, 255);  // #6e00ff Bright Purple (used for object color in demo 2)
	graphicsDefineColor(0, 6, 255, 110, 110); // #ff6e6e Light Red (used for object color in demo 2)
	graphicsDefineColor(0, 7, 0, 120, 255);  // #0078ff Bright Blue (used for object color in demo 2)
	graphicsDefineColor(0, 8, 255, 200, 0);  // #ffc800 Bright Yellow (used for object color in demo 2)
	graphicsDefineColor(0, 9, 0, 255, 150);  // #00ff96 Bright Green-Cyan (used for object color in demo 2)
	graphicsDefineColor(0, 10, 220, 20, 60); // #dc143c Crimson Red (used for object color in demo 2)
	graphicsDefineColor(0, 11, 95, 205, 228); // #5fcdE4 Light Blue
	graphicsDefineColor(0, 12, 61, 133, 148);  // #3D8594 Medium Blue
	graphicsDefineColor(0, 13, 30, 80, 100);   // #1E5064 Dark Blue
	graphicsDefineColor(0, 14, 255, 255, 255); // #FFFFFF White
	graphicsDefineColor(0, 15, 255, 255, 255); // #FFFFFF White (duplicate for potential use as highlight or special color)
	
	POKE(0xD00D, 0x33);
    POKE(0xD00E, 0x33);
    POKE(0xD00F, 0x33);

}

void video_wait_vblank(void) {

	while (PEEKW(RAST_ROW_L) >= 482u) {}          /* drain current vblank  */
	while (PEEKW(RAST_ROW_L) < 482u) {            /* wait for next vblank  */
	}
}
