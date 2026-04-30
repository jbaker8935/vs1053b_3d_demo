10 REM "Test VS1053b geometry kernel in Basic"
20 REM "(c) 2026 by John Baker"
30 REM "Main program"
40     scivalue = 0
50     hexval$ = ""
60     vgk_fn_addr = alloc(384): print "geometry kernel function address: ", vgk_fn_addr
70     print "assembly start":vgk_scrn_edges_get_asm(vgk_fn_addr): print "assembly complete"
80     print "plugin init start":vs1053_plugin_init():print "plugin init complete"
90     vgk_projection_params_init(240, 160, 120, -128): print "projection params init complete"
100     vgk_model_vertices_init(): print "model vertices init complete"
110     vgk_model_edges_init(): print "model edges init complete"
120     vgk_model_hidden_line_init(): print "model hidden line init complete"
130     vgk_check_slot(): print "slot check complete"
140     vgk_model_load(): print "model load complete"
150     vs1053_mem_write($36c3, 0): REM "disable descriptor"
160     vs1053_mem_write($36c1, 0): REM "hidden line off at startup"
170     visible_layer = 1
180     draw_layer = 2
190     active_bitmap = $58000: memcopy $58000,76800 poke $00
200     inactive_bitmap = $44000: : memcopy $58000,76800 poke $00
210     video_init(): print "video init complete"
220     vgk_cam_params_set(0, 0, 0, 0, 200, 1600)
230     obj_yaw = 0: hidden_line_mode = 0
240     while true
250         vgk_object_params_set(0, obj_yaw, 0, 128, 0, 0, 600)
260         obj_yaw = (obj_yaw + 2) & 255
270         if obj_yaw = 0
280             if hidden_line_mode = 0
290                 hidden_line_mode = 1:vs1053_mem_write($36c1,1)
300             else
310                 hidden_line_mode = 0:vs1053_mem_write($36c1,0)
320             endif
330         endif
340         vgk_trigger()
350         vgk_wait_complete()
360         REM "output screen edges to draw layer"
370         REM "vgk_scrn_edges_get(draw_layer, 10)"
380         poke $b0, draw_layer: poke $b1, 10
390         call vgk_fn_addr
400         REM "drawing complete"
410         if visible_layer = 1
420             visible_layer = 2: draw_layer = 1
430             active_bitmap = $44000: inactive_bitmap = $58000
440             ?$d108 = 0: ?$d110 = 1  REM "enable layer 2, disable layer 1"
450             memcopy inactive_bitmap, 76800 poke $00
460         else
470             visible_layer = 1: draw_layer = 2
480             active_bitmap = $58000: inactive_bitmap = $44000
490             ?$d108 = 1: ?$d110 = 0  REM "enable layer 1, disable layer 2"
500             memcopy inactive_bitmap, 76800 poke $00
510         endif
520     wend
600 proc vs1053_sci_write(addr, val)
610      if sgn(val) = -1:  REM "handle 32-bit signed values by converting to uint16_t unsigned equivalent"
620          val = val + 65536
630      endif
640      poke $d701, addr
650      pokew $d702, val 
660      poke $d700, $01
670      poke $d700, $00
680      while (peek($d700) & $80) <> 0: wend
690 endproc
700 REM "vs1053b write to WRAM"
800 proc vs1053_mem_write(wram_addr, val)
810     vs1053_sci_write($07, wram_addr)
820     vs1053_sci_write($06, val)
830 endproc
840 REM "Initiate vs1053b read.  scivalue variable will hold the result."
900 REM "Can be used for autoincrement WRAM reads by calling this proc repeatedly without writing to $07 again."
1000 proc vs1053_sci_read(addr)
1010     poke $d701, addr
1020     poke $d700, $03
1030     poke $d700, $00
1040     while (peek($d700) & $80) <> 0: wend
1050     scivalue = peek($d702) | (peek($d703) << 8)
1060 endproc 
1070 REM "vs1053b read from WRAM. global scivalue will hold the result."
1080 REM "sets base address for autoincrement WRAM reads and reads first word of data."
1100 proc vs1053_mem_read(wram_addr)
1110     vs1053_sci_write($07, wram_addr)
1120     vs1053_sci_read($06)
1130 endproc
1140 REM "Setup vs1053b geometry kernel"
1200 proc vs1053_plugin_init()
1210 REM "Load Plugin"
1220 REM "Disable DAC and Set Clock"
1230     bload "plugin.bin", $10000
1240     i = 0
1250     while i < 17102
1260         xpeek($10000 + i): lo = peekvalue
1270         xpeek($10000 + i + 1): hi = peekvalue
1280         sci_reg = lo | (hi << 8)
1290         xpeek($10000 + i + 2): lo = peekvalue
1300         xpeek($10000 + i + 3): hi = peekvalue
1310         n = lo | (hi << 8)
1320         i = i + 4
1330         if (n & $8000)
1340             n = n & $7fff
1350             xpeek($10000 + i): lo = peekvalue
1360             xpeek($10000 + i + 1): hi = peekvalue
1370             val = lo | (hi << 8)
1380             i = i + 2
1390             while n > 0
1400                 vs1053_sci_write(sci_reg, val)
1410                 n = n - 1
1420             wend
1430         else
1440             while n > 0
1450                 xpeek($10000 + i): lo = peekvalue
1460                 xpeek($10000 + i + 1): hi = peekvalue
1470                 val = lo | (hi << 8)
1480                 vs1053_sci_write(sci_reg, val)
1490                 i = i + 2
1500                 n = n - 1
1510             wend
1520         endif
1530     wend
1540     REM "Mute DAC"
1550     vs1053_sci_write($0B, $fefe)
1560     REM "Disable DAC"
1570     vs1053_mem_read($c01a)
1580     vs1053_mem_write($c01a, scivalue & $fffe)
1590     REM "Boost clock"
1600     vs1053_sci_write($03, $c000) 
1610     vs1053_mem_write($36b6, $0000)
1620     vs1053_sci_write($0A, $0050)
1630 endproc 
1640 REM "XPEEK - value is stored in peekvalue variable"
1700 proc xpeek(addr)
1710    local block:block=addr\8192:local prevblock
1720    local offset:offset=addr&$1fff
1730    ?0=179:prevblock=?$E:?$E=block:?1=4
1740    peekvalue=peek($C000+offset)
1750    ?1=0:?$E=prevblock
1760 endproc
1770 REM "XPOKE"
1800 proc xpoke(addr,value)
1810    local block:block=addr\8192:local prevblock
1820    local offset:offset=addr&$1fff
1830    ?0=179:prevblock=?$E:?$E=block:?1=4
1840    ?($C000+offset)=value
1850    ?1=0:?$E=prevblock
1860 endproc
1900 proc vgk_projection_params_init(focal, half_w, half_h, near_z)
1910     vs1053_mem_write($36b9, focal)
1920     vs1053_sci_write($06, half_w)
1930     vs1053_sci_write($06, half_h)
1940     vs1053_sci_write($06, near_z)
1950     vs1053_projection_enable()
1960 endproc
2000 proc vs1053_projection_enable()
2010     vs1053_mem_write($36b7, $0001)
2020     vs1053_sci_write($06, $0001)
2030 endproc
2100 proc vgk_model_vertices_init()
2110     vs1053_mem_write($2000, 15)
2120     vs1053_sci_write($07, $2001)
2130     for i = 1 to 15
2140         read x, y, z
2150         vs1053_sci_write($06, x)
2160         vs1053_sci_write($06, y)
2170         vs1053_sci_write($06, z)
2180     next
2190 endproc
2200 proc vgk_model_edges_init()
2210     vs1053_mem_write($20b5, 25)
2220     vs1053_sci_write($07, $20b6)
2230     for i = 1 to 25
2240         read v1, v2
2250         vs1053_sci_write($06, v2 << 8 | v1)
2260     next
2270 endproc
2300 proc vgk_model_hidden_line_init()
2310     vs1053_sci_write($07, $2111)
2320     for i = 1 to 25
2330         read e1, e2
2340         vs1053_sci_write($06, e2 << 8 | e1)
2350     next
2360     vs1053_sci_write($07, $216b)
2370     for i = 1 to 12
2380         read x,y,z
2390         vs1053_sci_write($06, x)
2400         vs1053_sci_write($06, y)
2410         vs1053_sci_write($06, z)
2420     next
2430     vs1053_mem_write($2110, 12)
2440 endproc
2500 proc vgk_trigger()
2510     vs1053_sci_write($0c, $cafe)
2520 endproc
2600 proc vgk_reset()
2610     vs1053_mem_write($36b6, $0000)
2620 endproc
2700 proc vgk_wait_complete()
2710     elapsed = 0
2720     waitresult = 0
2730     while (elapsed < 10000) & (waitresult = 0)
2740         elapsed = elapsed + 1
2750         vs1053_mem_read($36b6)
2760         if scivalue = $abcd
2770             waitresult = 1
2780         endif
2790         if (scivalue = $e201) | (scivalue = $e202)
2800             waitresult = 2
2810         endif
2820     wend
2830     if waitresult = 0
2840         print "Error: Wait for geometry kernel timed out"
2850     endif
2860     if waitresult = 2
2870         print "Error: Geometry kernel reported an error with code ", scivalue
2880     endif
2890 endproc
2900 REM "output screen edges"
3000 proc vgk_scrn_edges_get(layer, edge_color)
3010     poke $d181, edge_color
3020     poke $d00a, $01
3030     vs1053_mem_read($063f)
3040     edgecount = scivalue
3050     for e = 1 to edgecount
3060         poke $d180, (layer << 2) | $01
3070         vs1053_sci_read($06): x0=scivalue
3080         vs1053_sci_read($06): x1=scivalue
3090         vs1053_sci_read($06): y=scivalue
3100         pokew $d182, x0
3110         pokew $d184, x1
3120         pokew $d186, y
3130         poke $d180, (layer << 2) | $03
3140         while (peek($d182) | peek($d183)) <> 0: wend
3150     next
3160     poke $d180, 0
3170     poke $d00a, 0
3180 endproc
3200 proc vgk_model_load()
3210     vgk_reset()
3220     vs1053_sci_write($0E, $00)
3230     vgk_wait_complete()
3240 endproc
3250 REM "Anaconda Vertices"
3260 data 0,14,-116, -86,-26,-74, -52,-94,-6, 52,-94,-6, 86,-26,-74
3270 data 0,96,-98, -138,30,-30, -86,-78,80, 86,-78,80, 138,30,-30
3280 data -86,106,-46, -138,-2,64, 0,0,508, 138,-2,64, 86,106,-46
3290 REM "Anaconda Edges"
3300 data 0,1, 0,4, 0,5, 1,2, 1,6, 2,3, 2,7, 3,4, 3,8
3310 data 4,9, 5,10, 5,14, 6,10, 6,11, 7,11, 7,12, 8,12, 8,13
3320 data 9,13, 9,14, 10,12, 10,14, 11,12, 12,13, 12,14
3330 REM "Anaconda Edge Faces"
3340 data 0,1, 0,5, 1,5, 0,2, 1,2, 0,3, 2,3, 0,4, 3,4
3350 data 4,5, 1,6, 5,6, 1,7, 2,7, 2,8, 3,8, 3,9, 4,9
3360 data 4,10, 5,10, 7,11, 6,11, 7,8, 9,10, 10,11
3370 REM "Anaconda Face Normals"
3380 data 0,-11815,-11351, -8157,2879,-13915, -12917,-9562,-3187
3390 data 0,-16131,2868, 12917,-9562,-3187, 8157,2879,-13915
3400 data 0,16124,-2905, -12904,9578,3193, -13397,-8435,4218
3410 data 13397,-8435,4218, 12904,9578,3193, 0,16092,3081
3420 REM "LUT Data"
3430 data 1,0,0,0,0, 1,1,7,7,7, 1,2,7,3,231, 1,3,7,3,119
3440 data 1,4,7,115,119, 1,5,119,3,7, 1,6,231,227,231, 1,7,7,227,7
3450 data 1,8,7,115,7, 1,9,231,227,7, 1,10,231,3,7, 1,11,119,115,7
3460 data 0,0,0,0,0, 0,1,0,255,239, 0,2,255,0,150, 0,3,180,255,0
3470 data 0,4,255,85,0, 0,5,110,0,255, 0,6,255,110,110, 0,7,0,120,255
3480 data 0,8,255,200,0, 0,9,0,255,150, 0,10,220,20,60, 0,11,95,205,228
3490 data 0,12,61,133,148, 0,13,30,80,100, 0,14,255,255,255, 0,15,255,255,255
3500 proc vgk_object_params_set(pitch, yaw, roll, scale, x, y, z)
3510     vs1053_mem_write($1801, pitch<<8 | yaw)
3520     vs1053_sci_write($06, roll<<8 | scale)
3530     vs1053_sci_write($06, x)
3540     vs1053_sci_write($06, y)
3550     vs1053_sci_write($06, z)
3560 endproc
3600 proc vgk_cam_params_set(pitch, yaw, roll, x, y, z)
3610     vs1053_mem_write($1806, pitch<<8 | yaw)
3620     vs1053_sci_write($06, roll<<8 | $80)
3630     vs1053_sci_write($06, x)
3640     vs1053_sci_write($06, y)
3650     vs1053_sci_write($06, z)
3660 endproc
3700 proc video_init()
3710    poke $d000, $0f
3720    REM "setup layer 1 and 2 for bitmap output"
3730    REM "layer 1 at $58000, layer 2 at $44000"
3740    pokel $d109, $58000
3750    pokel $d111, $44000
3760    ?$d002 = (?$d002 & $0f) | ($10)
3770    ?$d003 = $02
3780    REM "Both layers use CLUT 0"
3790    ?$d108 = 0: ?$d110 = 0
3800    REM "Setup LUT colors for both LUTs"
3810    ?1 = 1  
3820    for i = 1 to 28
3830        read lut, slot, r, g, b
3840        addr = $d000 + (lut * $400) + (slot << 2)
3850        poke addr, b
3860        poke addr + 1, g
3870        poke addr + 2, r
3880        poke addr + 3, $ff
3890    next
3900    ?1 = 0
3910    REM "Background color"
3920    poke $d00d, $33: poke $d00e, $33: poke $d00f, $33
3930 endproc
4000 proc vgk_check_slot()
4010    vs1053_mem_read($2000): vertices=scivalue
4020    vs1053_mem_read($20b5): edges=scivalue
4030    vs1053_mem_read($2110): faces=scivalue
4040    print "Vertices: ", vertices, "Edges: ", edges, "Faces: ", faces
4050    if (vertices <> 15) | (edges <> 25) | (faces <> 12)
4060        print "Error: Slot check failed - expected 15 vertices, 25 edges, and 12 faces, but read ", vertices, " vertices, ", edges, " edges, and ", faces, " faces"
4070    endif
4080 endproc   
4100 proc as_hex(val)
4110     hibyte = (val >> 8) & $ff
4120     lobyte = val & $ff
4130     hihex$ = mid$("0123456789abcdef", (hibyte \ 16) + 1, 1)+ mid$("0123456789abcdef", (hibyte % 16) + 1, 1)
4140     lohex$ = mid$("0123456789abcdef", (lobyte \ 16) + 1, 1)+ mid$("0123456789abcdef", (lobyte % 16) + 1, 1)
4150     hexval$ = hihex$ + lohex$
4160     print hexval$
4170 endproc
4200 proc vgk_scrn_edges_get_asm(asm_addr)
4210     REM "2 bytes argument: layer zp B0, edge_color zp B1"
4220     REM "edge count B2"
4230     for pass = 0 to 1
4240         assemble asm_addr, pass
4250         lda $b1
4260         sta $d181
4270         lda #$01
4280         sta $d00a
4290         lda #$3f
4300         ldx #$06  
4310         jsr mem_read
4320         sta $b2
4330         asl $b0
4340         asl $b0
4350 .vs_s:  lda $b0
4360         ora #$01
4370         sta $d180
4380         ldx #$06
4390         stx $d701
4400         ldx #$03
4410         stx $d700
4420         stz $d700
4430 .vs_2:  ldx $d700
4440         bmi vs_2
4450         ldx $d702
4460         stx $d182
4470         ldx $d703
4480         stx $d183
4490         ldx #$06
4500         stx $d701
4510         ldx #$03
4520         stx $d700
4530         stz $d700
4540 .vs_3:  ldx $d700
4550         bmi vs_3
4560         ldx $d702
4570         stx $d184
4580         ldx $d703
4590         stx $d185
4600         ldx #$06
4610         stx $d701
4620         ldx #$03
4630         stx $d700
4640         stz $d700
4650 .vs_4:  ldx $d700
4660         bmi vs_4
4670         ldx $d702
4680         stx $d186
4690         ldx $d703
4700         stx $d187
4710         lda $b0
4720         ora #$03
4730         sta $d180
4740 .vs_5:  lda $d182
4750         ora $d183
4760         bne vs_5
4770         dec $b2
4780         bne vs_s
4790         stz $d180
4800         stz $d00a
4810         rts
4820 .mem_read: ldy	#$7
4830         sty	$d701
4840         sta	$d702
4850         stx	$d703
4860         ldx	#$1
4870         stx	$d700
4880         stz	$d700
4890 .mr_1:   ldx	$d700
4900         bmi	mr_1
4910         ldx	#$6
4920         stx	$d701
4930         ldx	#$3
4940         stx	$d700
4950         stz	$d700
4960 .mr_2:   ldx	$d700
4970         bmi	mr_2
4980         ldx	$d703
4990         lda	$d702
5000         rts
5010     next
5020 endproc
