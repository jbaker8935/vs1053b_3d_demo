; emit_edges_asm.s — Assembly read of SCI and direct line-draw emit for screen edges.
;
; This is a WIP assembly implementation.  Many 6502 optimizations are left on the table.
;
; F256 line-draw hardware registers:
;   $D181 = color
;   $D182/$D183 = x0 lo/hi
;   $D184/$D185 = x1 lo/hi
;   $D186 = y0
;   $D187 = y1
;   $D180 = control (layer<<2|1 = enable; |0x02 = clock; 0x00 = disable)
;   $D00A = 0x01 = line-draw mode enable; 0x00 = disable
;
; Edge flag bits (uint8_t per edge in g_edge_buf_flags):
;   bit 0 = VISIBLE  (0x01)
;   bit 1 = CLIP_V0  (0x02)
;   bit 2 = CLIP_V1  (0x04)
;   bit 3 = NEAR     (0x08)
;

        .include "build/struct_offsets.inc"

; ---------------------------------------------------------------------------
; Private staging variables 
; ---------------------------------------------------------------------------
        .section .bss
vgk_v0:          .space 1        ; v0 raw vertex index from packed edge word
vgk_v1:          .space 1        ; v1 raw vertex index from packed edge word
vgk_sx0_lo:      .space 1        ; resolved x0 low byte
vgk_sx0_hi:      .space 1        ; resolved x0 high byte
vgk_sy0:         .space 1        ; resolved y0 (low byte only; screen Y ≤ 255)
vgk_sx1_lo:      .space 1        ; resolved x1 low byte
vgk_sx1_hi:      .space 1        ; resolved x1 high byte
vgk_sy1:         .space 1        ; resolved y1
vgk_edge_color:  .space 1        ; selected color (near or far) 
vgk_edge_flags:  .space 1        ; raw flags byte saved for CLIP_V0/V1 detection
vgk_edge_idx:    .space 1        ; edge index into g_edge_buf_flags (uint8_t index)
vgk_edge_byte2:  .space 1        ; edge index * 2 (byte offset into g_edge_buf_packed)
vgk_fifo_outer:  .space 1        ; FIFO wait outer counter (keeps X/Y free for vertex indexing)

; ---------------------------------------------------------------------------
; Externals
; ---------------------------------------------------------------------------
        .section .text
        .extern g_emit_edge_count       ; uint8_t __zp — total output edges
        .extern g_emit_visible_count    ; uint8_t __zp — edges emitted
        .extern g_emit_n_input          ; uint8_t __zp — number of original vertices
        .extern g_emit_n_clip           ; uint8_t __zp — number of clip vertices (≤16)
        .extern g_emit_near_color       ; uint8_t __zp — near color byte
        .extern g_emit_far_color        ; uint8_t __zp — far color byte
        .extern g_emit_layer_ctrl       ; uint8_t __zp — (layer<<2)|1
        .extern g_emit_edge_a           ; const uint8_t __zp * — ZP ptr → model->edge_a
        .extern g_emit_edge_b           ; const uint8_t __zp * — ZP ptr → model->edge_b
        .extern scr_x_lo                ; uint8_t[32] — screen X lo bytes
        .extern scr_x_hi                ; uint8_t[32] — screen X hi bytes
        .extern scr_y                   ; uint8_t[32] — screen Y bytes
        .extern clip_x_lo               ; uint8_t[16] — clip X lo bytes
        .extern clip_x_hi               ; uint8_t[16] — clip X hi bytes
        .extern clip_y                  ; uint8_t[16] — clip Y bytes
        .extern g_edge_buf_packed       ; uint16_t[36] — lo=v0, hi=v1 per edge
        .extern g_edge_buf_flags        ; uint8_t[36]  — VISIBLE/NEAR/CLIP flags
        .extern g_line_fifo_timeouts    ; uint16_t — FIFO stall counter
        .extern vgk_no_near_far_coloring    ; bool — non-zero → single flat color, fast path eligible
        .extern vgk_hidden_line_active    ; bool — non-zero → hidden-line on, suppress fast path

; ===========================================================================
; vgk_scrn_edges_get_asm
;
; Entry:
;   A                   = layer (0 or 1)
;   g_emit_n_input      = model->vertex_count
;   g_emit_edge_count   = edge count (clamped to VGK_MAX_EDGES)
;   g_emit_near_color   = near color byte
;   g_emit_far_color    = far color byte  (== near when no_near_far_coloring)
;   g_emit_visible_count= 0  (reset by C wrapper)
;   g_emit_edge_a       = pointer to model->edge_a[]
;   g_emit_edge_b       = pointer to model->edge_b[]
;
; Exit:
;   A                   = visible edge count
;   $D180=0, $D00A=0    (line-draw mode cleared)
;   g_line_fifo_timeouts incremented (16-bit) on any FIFO stall timeout
;
; llvm-mos C calling convention: single uint8_t arg in A; return in A.
; ===========================================================================
        .globl vgk_scrn_edges_get_asm

vgk_scrn_edges_get_asm:

; ---------------------------------------------------------------------------
; Compute layer control byte (layer<<2)|1 and store.
; ---------------------------------------------------------------------------
        asl
        asl
        ora     #$01
        sta     g_emit_layer_ctrl

; ---------------------------------------------------------------------------
; Inline SCI burst-read of screen vertex coords.
;   Set WRAMADDR = VGK_SCREEN_COORDS (0x36C0)
;   Then loop g_emit_n_input times reading [sx_lo, sx_hi, sy_lo] per vertex.
;  Screen coords are written to scr_x_lo[], scr_x_hi[], scr_y[] as interleaved lo/hi/lo bytes. 
;  Clipping attributes are checked later to determine if screen vertex is overridden by clip vertex coords.
; ---------------------------------------------------------------------------

        ; Set WRAMADDR = 0x36C0
        lda     #$07                    ; SCI_WRAMADDR
        sta     $D701
        lda     #$C0                    ; lo of 0x36C0
        sta     $D702
        lda     #$36                    ; hi
        sta     $D703
        lda     #$01                    ; CTRL_Start (write)
        sta     $D700
        stz     $D700
gsf_wramaddr_scrn_wait:
        lda     $D700
        bmi     gsf_wramaddr_scrn_wait  ; bit 7 = CTRL_Busy

        ; Point SCI address register at SCI_WRAM (0x06) once — burst increments on VS1053b
        lda     #$06
        sta     $D701

        ldy     #$00
gsf_scrn_loop:
        ; Read sx word
        lda     #$03                    ; CTRL_Start | CTRL_RWn
        sta     $D700
        stz     $D700
gsf_sci_sx_wait:
        lda     $D700
        bmi     gsf_sci_sx_wait
        lda     $D702                   ; sx lo
        sta     scr_x_lo, y
        lda     $D703                   ; sx hi
        sta     scr_x_hi, y

        ; Read sy word
        lda     #$03
        sta     $D700
        stz     $D700
gsf_sci_sy_wait:
        lda     $D700
        bmi     gsf_sci_sy_wait
        lda     $D702                   ; sy (lo byte; hi byte unused for screen Y)
        sta     scr_y, y

        iny
        cpy     g_emit_n_input
        bne     gsf_scrn_loop

; ---------------------------------------------------------------------------
; Read n_clip from WRAM.
;   VGK_N_CLIP_VERTS = 0x3770
; ---------------------------------------------------------------------------
        lda     #$07                    ; SCI_WRAMADDR
        sta     $D701
        lda     #$70                    ; lo of 0x3770
        sta     $D702
        lda     #$37                    ; hi
        sta     $D703
        lda     #$01
        sta     $D700
        stz     $D700
gsf_nclip_addr_wait:
        lda     $D700
        bmi     gsf_nclip_addr_wait

        lda     #$06                    ; SCI_WRAM
        sta     $D701
        lda     #$03
        sta     $D700
        stz     $D700
gsf_nclip_rd_wait:
        lda     $D700
        bmi     gsf_nclip_rd_wait
        lda     $D702                   ; n_clip lo byte
        cmp     #17                     ; clamp to <= 16
        bcc     gsf_nclip_ok
        lda     #16
gsf_nclip_ok:
        sta     g_emit_n_clip

; ---------------------------------------------------------------------------
; Fast path check.
;   Conditions: no_near_far_coloring != 0, vgk_hidden_line_active == 0,
;               g_emit_n_clip == 0, n_out == g_emit_edge_count.
;   If all pass -> JMP gsf_fast_path.
; Fast path: all edges are visible and can be emitted directly without clipping or hidden-line checks.
;   Allows using the CPU object edge definitions without having to read edges from SCI.
;   Useful for most arcade wireframe animations that do not use hidden lines
;   Improves frame rate.
; ---------------------------------------------------------------------------
        lda     vgk_no_near_far_coloring
        beq     gsf_skip_fast           ; not set -> can't use fast path

        lda     vgk_hidden_line_active
        bne     gsf_skip_fast           ; hidden-line on -> need VISIBLE flags

        lda     g_emit_n_clip
        bne     gsf_skip_fast           ; clip verts present -> need clip array

        ; Read VGK_N_OUTPUT_EDGES (0x3720) and compare with g_emit_edge_count.
        lda     #$07
        sta     $D701
        lda     #$20                    ; lo of 0x3720
        sta     $D702
        lda     #$37                    ; hi
        sta     $D703
        lda     #$01
        sta     $D700
        stz     $D700
gsf_nout_addr_wait:
        lda     $D700
        bmi     gsf_nout_addr_wait

        lda     #$06
        sta     $D701
        lda     #$03
        sta     $D700
        stz     $D700
gsf_nout_rd_wait:
        lda     $D700
        bmi     gsf_nout_rd_wait
        lda     $D702                   ; n_out lo byte
        cmp     g_emit_edge_count
        bne     gsf_skip_fast           ; counts differ -> slow path
        jmp     gsf_fast_path           ; all edges output -> fast path

gsf_skip_fast:

; ---------------------------------------------------------------------------
; Clip vertex coords (if n_clip > 0).
;   Reads in clip vertex coordinates which will replace clipped edge vertices
; ---------------------------------------------------------------------------
        lda     g_emit_n_clip
        beq     gsf_no_clip

        lda     #$07
        sta     $D701
        lda     #$00                    ; lo of 0x3700
        sta     $D702
        lda     #$37                    ; hi
        sta     $D703
        lda     #$01
        sta     $D700
        stz     $D700
gsf_clip_addr_wait:
        lda     $D700
        bmi     gsf_clip_addr_wait

        lda     #$06
        sta     $D701

        ldy     #$00
gsf_clip_loop:
        ; Read clip sx word
        lda     #$03
        sta     $D700
        stz     $D700
gsf_sci_cx_wait:
        lda     $D700
        bmi     gsf_sci_cx_wait
        lda     $D702
        sta     clip_x_lo, y
        lda     $D703
        sta     clip_x_hi, y

        ; Read clip sy word
        lda     #$03
        sta     $D700
        stz     $D700
gsf_sci_cy_wait:
        lda     $D700
        bmi     gsf_sci_cy_wait
        lda     $D702
        sta     clip_y, y

        iny
        cpy     g_emit_n_clip
        bne     gsf_clip_loop

gsf_no_clip:

; ---------------------------------------------------------------------------
; Edge data burst-reads for cases where Edge data needs to be retrieved
;
;   Read VGK_N_OUTPUT_EDGES -> update g_emit_edge_count.
;       VGK_N_OUTPUT_EDGES = 0x3720
;   Burst-read VGK_OUTPUT_EDGE_FLAGS (0x3721, packed 2 flags/word)
;       -> unpack to g_edge_buf_flags[] (one byte per edge).
;   Burst-read VGK_OUTPUT_EDGE_PACKED (0x3733)
;       -> g_edge_buf_packed[] as raw uint16_t LE pairs.
; ---------------------------------------------------------------------------

        ; read N_OUTPUT_EDGES
        lda     #$07
        sta     $D701
        lda     #$20                    ; lo of 0x3720
        sta     $D702
        lda     #$37
        sta     $D703
        lda     #$01
        sta     $D700
        stz     $D700
gsf_nout2_addr_wait:
        lda     $D700
        bmi     gsf_nout2_addr_wait

        lda     #$06
        sta     $D701
        lda     #$03
        sta     $D700
        stz     $D700
gsf_nout2_rd_wait:
        lda     $D700
        bmi     gsf_nout2_rd_wait
        lda     $D702
        sta     g_emit_edge_count

        ; read output edge flags -> g_edge_buf_flags[]
        ;     Flags are packed 2 per WRAM word: lo byte = even-index edge, hi byte = odd.
        ;     VGK_OUTPUT_EDGE_FLAGS = 0x3721
        lda     #$07
        sta     $D701
        lda     #$21                    
        sta     $D702
        lda     #$37
        sta     $D703
        lda     #$01
        sta     $D700
        stz     $D700
gsf_flags_addr_wait:
        lda     $D700
        bmi     gsf_flags_addr_wait

        lda     #$06
        sta     $D701

        ; Compute n_flag_words = (edge_count + 1) >> 1.
        lda     g_emit_edge_count
        clc
        adc     #1
        lsr                             
        tay                             
        beq     gsf_flags_done          
        ldx     #$00                    

gsf_flags_loop:
        lda     #$03
        sta     $D700
        stz     $D700
gsf_sci_flags_wait:
        lda     $D700
        bmi     gsf_sci_flags_wait
        lda     $D702                   
        sta     g_edge_buf_flags, x
        inx
        cpx     g_emit_edge_count
        bcs     gsf_flags_done          
        lda     $D703                   
        sta     g_edge_buf_flags, x
        inx
        dey
        bne     gsf_flags_loop

gsf_flags_done:

        ; burst-read packed v0v1 -> g_edge_buf_packed[]
        ;     VGK_OUTPUT_EDGE_PACKED = 0x3733
        ;     Each WRAM word: lo byte = v0, hi byte = v1.
        lda     #$07
        sta     $D701
        lda     #$33                    
        sta     $D702
        lda     #$37
        sta     $D703
        lda     #$01
        sta     $D700
        stz     $D700
gsf_packed_addr_wait:
        lda     $D700
        bmi     gsf_packed_addr_wait

        lda     #$06
        sta     $D701

        ldy     g_emit_edge_count
        beq     gsf_packed_done
        ldx     #$00                    

gsf_packed_loop:
        lda     #$03
        sta     $D700
        stz     $D700
gsf_sci_packed_wait:
        lda     $D700
        bmi     gsf_sci_packed_wait
        lda     $D702                   
        sta     g_edge_buf_packed, x
        lda     $D703                   
        sta     g_edge_buf_packed + 1, x
        inx
        inx
        dey
        bne     gsf_packed_loop

gsf_packed_done:

; ---------------------------------------------------------------------------
; Branch to near/far two-pass or single-pass.
;    draw far edges first in case near and far edges overlap
;    could be skipped when hidden lines are enabled but will have some
;    drawing inconsistency at vertex where far and near edges meet.
;    likely overkill processing tbh.
; ---------------------------------------------------------------------------
        lda     vgk_no_near_far_coloring
        beq     gsf_do_twopass          ; zero -> two-pass near/far
        jmp     gsf_single_pass         ; non-zero -> single-pass flat color
gsf_do_twopass:

; ===========================================================================
; Two-pass near/far direct-emit.
;
; Far pass first (NEAR flag clear), then near pass (NEAR flag set).
; Each pass iterates g_edge_buf_flags/packed from index 0 to edge_count-1.
; Visible edges are written directly to F256 line-draw hardware ($D181..$D187).
;
; Register usage:
;   Y  = edge index (0..edge_count-1)
;   X  = vertex index for scr_*/clip_* lookups
;   BSS: vgk_edge_flags, vgk_v0, vgk_v1, vgk_sx0_lo/hi, vgk_sy0,
;        vgk_sx1_lo/hi, vgk_sy1, vgk_fifo_outer, vgk_edge_byte2
; ===========================================================================

        lda     g_emit_edge_count
        bne     gsf_twopass_has_edges
        jmp     gsf_twopass_done        ; edge_count==0, nothing to draw
gsf_twopass_has_edges:

        lda     #$01
        sta     $D00A                   ; enter line-draw mode
        lda     g_emit_layer_ctrl
        sta     $D180                   

        ; ----- FAR PASS -----
        ldy     #$00                    
        stz     vgk_edge_byte2           

gsf_far_edge_loop:
        lda     g_edge_buf_flags, y
        sta     vgk_edge_flags

        lda     vgk_edge_byte2
        clc
        adc     #2
        sta     vgk_edge_byte2

        lda     vgk_edge_flags
        lsr                             ; C = VISIBLE
        bcs     gsf_far_visible
        jmp     gsf_far_next_edge       ; not visible -> skip
gsf_far_visible:

        ; Skip NEAR edges in far pass
        lda     vgk_edge_flags
        and     #$08
        beq     gsf_far_is_far          
        jmp     gsf_far_next_edge       
gsf_far_is_far:

        ; Load v0/v1 from g_edge_buf_packed[vgk_edge_byte2 - 2]
        lda     vgk_edge_byte2
        sec
        sbc     #2
        tax
        lda     g_edge_buf_packed, x    
        sta     vgk_v0
        lda     g_edge_buf_packed + 1, x 
        sta     vgk_v1

        ; Resolve V0
        ldx     vgk_v0
        cpx     g_emit_n_input
        bcc     gsf_far_v0_lt_ninput
        ; Offset-encoded clip: ci = v0 - n_input
        txa
        sec
        sbc     g_emit_n_input
        tax
        cpx     g_emit_n_clip
        bcs     gsf_far_v0_zero
        lda     clip_x_lo, x
        sta     vgk_sx0_lo
        lda     clip_x_hi, x
        sta     vgk_sx0_hi
        lda     clip_y, x
        sta     vgk_sy0
        bra     gsf_far_v0_done
gsf_far_v0_zero:
        stz     vgk_sx0_lo
        stz     vgk_sx0_hi
        stz     vgk_sy0
        bra     gsf_far_v0_done
gsf_far_v0_lt_ninput:
        lda     vgk_edge_flags
        and     #$02                    ; CLIP_V0
        beq     gsf_far_v0_original
        cpx     g_emit_n_clip
        bcs     gsf_far_v0_zero
        lda     clip_x_lo, x
        sta     vgk_sx0_lo
        lda     clip_x_hi, x
        sta     vgk_sx0_hi
        lda     clip_y, x
        sta     vgk_sy0
        bra     gsf_far_v0_done
gsf_far_v0_original:
        lda     scr_x_lo, x
        sta     vgk_sx0_lo
        lda     scr_x_hi, x
        sta     vgk_sx0_hi
        lda     scr_y, x
        sta     vgk_sy0
gsf_far_v0_done:

        ; Resolve V1
        ldx     vgk_v1
        cpx     g_emit_n_input
        bcc     gsf_far_v1_lt_ninput
        txa
        sec
        sbc     g_emit_n_input
        tax
        cpx     g_emit_n_clip
        bcs     gsf_far_v1_zero
        lda     clip_x_lo, x
        sta     vgk_sx1_lo
        lda     clip_x_hi, x
        sta     vgk_sx1_hi
        lda     clip_y, x
        sta     vgk_sy1
        bra     gsf_far_v1_done
gsf_far_v1_zero:
        stz     vgk_sx1_lo
        stz     vgk_sx1_hi
        stz     vgk_sy1
        bra     gsf_far_v1_done
gsf_far_v1_lt_ninput:
        lda     vgk_edge_flags
        and     #$04                    ; CLIP_V1
        beq     gsf_far_v1_original
        cpx     g_emit_n_clip
        bcs     gsf_far_v1_zero
        lda     clip_x_lo, x
        sta     vgk_sx1_lo
        lda     clip_x_hi, x
        sta     vgk_sx1_hi
        lda     clip_y, x
        sta     vgk_sy1
        bra     gsf_far_v1_done
gsf_far_v1_original:
        lda     scr_x_lo, x
        sta     vgk_sx1_lo
        lda     scr_x_hi, x
        sta     vgk_sx1_hi
        lda     scr_y, x
        sta     vgk_sy1
gsf_far_v1_done:

        ; Emit far edge
        lda     vgk_sx0_lo
        sta     $D182
        lda     vgk_sx0_hi
        sta     $D183
        lda     vgk_sx1_lo
        sta     $D184
        lda     vgk_sx1_hi
        sta     $D185
        lda     vgk_sy0
        sta     $D186
        lda     vgk_sy1
        sta     $D187
        lda     g_emit_far_color
        sta     $D181
        lda     g_emit_layer_ctrl
        ora     #$02
        sta     $D180

        lda     #$40
        sta     vgk_fifo_outer
gsf_far_fifo_outer:
        ldx     #$ff
gsf_far_fifo_inner:
        lda     $D182
        ora     $D183
        beq     gsf_far_fifo_ok
        dex
        bne     gsf_far_fifo_inner
        dec     vgk_fifo_outer
        bne     gsf_far_fifo_outer
        ; Timeout
        inc     g_line_fifo_timeouts
        bne     gsf_far_fifo_bail
        inc     g_line_fifo_timeouts + 1
gsf_far_fifo_bail:
        lda     #$00
        sta     $D180
        sta     $D00A
        lda     g_emit_visible_count
        rts
gsf_far_fifo_ok:
        inc     g_emit_visible_count
        lda     g_emit_layer_ctrl
        sta     $D180                   ; re-arm for next line

gsf_far_next_edge:
        iny
        cpy     g_emit_edge_count
        beq     gsf_far_pass_done       
        jmp     gsf_far_edge_loop
gsf_far_pass_done:

        ; ----- NEAR PASS -----
        lda     g_emit_layer_ctrl
        sta     $D180                   
        ldy     #$00
        stz     vgk_edge_byte2

gsf_near_edge_loop:
        lda     g_edge_buf_flags, y
        sta     vgk_edge_flags

        lda     vgk_edge_byte2
        clc
        adc     #2
        sta     vgk_edge_byte2

        lda     vgk_edge_flags
        lsr                             ; C = VISIBLE
        bcs     gsf_near_visible
        jmp     gsf_near_next_edge      ; not visible -> skip
gsf_near_visible:

        lda     vgk_edge_flags
        and     #$08
        bne     gsf_near_is_near        
        jmp     gsf_near_next_edge      
gsf_near_is_near:

        lda     vgk_edge_byte2
        sec
        sbc     #2
        tax
        lda     g_edge_buf_packed, x
        sta     vgk_v0
        lda     g_edge_buf_packed + 1, x
        sta     vgk_v1

        ; Resolve V0
        ldx     vgk_v0
        cpx     g_emit_n_input
        bcc     gsf_near_v0_lt_ninput
        txa
        sec
        sbc     g_emit_n_input
        tax
        cpx     g_emit_n_clip
        bcs     gsf_near_v0_zero
        lda     clip_x_lo, x
        sta     vgk_sx0_lo
        lda     clip_x_hi, x
        sta     vgk_sx0_hi
        lda     clip_y, x
        sta     vgk_sy0
        bra     gsf_near_v0_done
gsf_near_v0_zero:
        stz     vgk_sx0_lo
        stz     vgk_sx0_hi
        stz     vgk_sy0
        bra     gsf_near_v0_done
gsf_near_v0_lt_ninput:
        lda     vgk_edge_flags
        and     #$02
        beq     gsf_near_v0_original
        cpx     g_emit_n_clip
        bcs     gsf_near_v0_zero
        lda     clip_x_lo, x
        sta     vgk_sx0_lo
        lda     clip_x_hi, x
        sta     vgk_sx0_hi
        lda     clip_y, x
        sta     vgk_sy0
        bra     gsf_near_v0_done
gsf_near_v0_original:
        lda     scr_x_lo, x
        sta     vgk_sx0_lo
        lda     scr_x_hi, x
        sta     vgk_sx0_hi
        lda     scr_y, x
        sta     vgk_sy0
gsf_near_v0_done:

        ; Resolve V1
        ldx     vgk_v1
        cpx     g_emit_n_input
        bcc     gsf_near_v1_lt_ninput
        txa
        sec
        sbc     g_emit_n_input
        tax
        cpx     g_emit_n_clip
        bcs     gsf_near_v1_zero
        lda     clip_x_lo, x
        sta     vgk_sx1_lo
        lda     clip_x_hi, x
        sta     vgk_sx1_hi
        lda     clip_y, x
        sta     vgk_sy1
        bra     gsf_near_v1_done
gsf_near_v1_zero:
        stz     vgk_sx1_lo
        stz     vgk_sx1_hi
        stz     vgk_sy1
        bra     gsf_near_v1_done
gsf_near_v1_lt_ninput:
        lda     vgk_edge_flags
        and     #$04
        beq     gsf_near_v1_original
        cpx     g_emit_n_clip
        bcs     gsf_near_v1_zero
        lda     clip_x_lo, x
        sta     vgk_sx1_lo
        lda     clip_x_hi, x
        sta     vgk_sx1_hi
        lda     clip_y, x
        sta     vgk_sy1
        bra     gsf_near_v1_done
gsf_near_v1_original:
        lda     scr_x_lo, x
        sta     vgk_sx1_lo
        lda     scr_x_hi, x
        sta     vgk_sx1_hi
        lda     scr_y, x
        sta     vgk_sy1
gsf_near_v1_done:

        ; Emit near edge
        lda     vgk_sx0_lo
        sta     $D182
        lda     vgk_sx0_hi
        sta     $D183
        lda     vgk_sx1_lo
        sta     $D184
        lda     vgk_sx1_hi
        sta     $D185
        lda     vgk_sy0
        sta     $D186
        lda     vgk_sy1
        sta     $D187
        lda     g_emit_near_color
        sta     $D181
        lda     g_emit_layer_ctrl
        ora     #$02
        sta     $D180

        lda     #$40
        sta     vgk_fifo_outer
gsf_near_fifo_outer:
        ldx     #$ff
gsf_near_fifo_inner:
        lda     $D182
        ora     $D183
        beq     gsf_near_fifo_ok
        dex
        bne     gsf_near_fifo_inner
        dec     vgk_fifo_outer
        bne     gsf_near_fifo_outer
        ; Timeout
        inc     g_line_fifo_timeouts
        bne     gsf_near_fifo_bail
        inc     g_line_fifo_timeouts + 1
gsf_near_fifo_bail:
        lda     #$00
        sta     $D180
        sta     $D00A
        lda     g_emit_visible_count
        rts
gsf_near_fifo_ok:
        inc     g_emit_visible_count
        lda     g_emit_layer_ctrl
        sta     $D180

gsf_near_next_edge:
        iny
        cpy     g_emit_edge_count
        beq     gsf_twopass_done        ; all near edges done -> exit
        jmp     gsf_near_edge_loop

gsf_twopass_done:
        lda     #$00
        sta     $D180
        sta     $D00A
        lda     g_emit_visible_count
        rts

; ===========================================================================
; Single-pass emit (vgk_no_near_far_coloring set).
;
;   single pass over all VISIBLE edges regardless of NEAR flag
; ===========================================================================
gsf_single_pass:
        lda     g_emit_edge_count
        bne     gsf_single_has_edges    ; count > 0 -> proceed
        jmp     gsf_single_done         ; no edges
gsf_single_has_edges:

        lda     #$01
        sta     $D00A
        lda     g_emit_layer_ctrl
        sta     $D180

        stz     vgk_edge_idx
        stz     vgk_edge_byte2

gsf_single_edge_loop:
        ldx     vgk_edge_byte2
        lda     g_edge_buf_packed, x
        sta     vgk_v0
        lda     g_edge_buf_packed + 1, x
        sta     vgk_v1

        ldx     vgk_edge_idx
        lda     g_edge_buf_flags, x
        sta     vgk_edge_flags

        inc     vgk_edge_idx
        clc
        lda     vgk_edge_byte2
        adc     #2
        sta     vgk_edge_byte2

        lda     vgk_edge_flags
        lsr                             ; C = VISIBLE
        bcs     gsf_single_visible
        jmp     gsf_single_next_edge
gsf_single_visible:

        ldx     vgk_v0
        cpx     g_emit_n_input
        bcc     gsf_s_v0_lt
        txa
        sec
        sbc     g_emit_n_input
        tax
        cpx     g_emit_n_clip
        bcs     gsf_s_v0_zero
        lda     clip_x_lo, x
        sta     vgk_sx0_lo
        lda     clip_x_hi, x
        sta     vgk_sx0_hi
        lda     clip_y, x
        sta     vgk_sy0
        bra     gsf_s_v0_done
gsf_s_v0_zero:
        stz     vgk_sx0_lo
        stz     vgk_sx0_hi
        stz     vgk_sy0
        bra     gsf_s_v0_done
gsf_s_v0_lt:
        lda     vgk_edge_flags
        and     #$02
        beq     gsf_s_v0_orig
        cpx     g_emit_n_clip
        bcs     gsf_s_v0_zero
        lda     clip_x_lo, x
        sta     vgk_sx0_lo
        lda     clip_x_hi, x
        sta     vgk_sx0_hi
        lda     clip_y, x
        sta     vgk_sy0
        bra     gsf_s_v0_done
gsf_s_v0_orig:
        lda     scr_x_lo, x
        sta     vgk_sx0_lo
        lda     scr_x_hi, x
        sta     vgk_sx0_hi
        lda     scr_y, x
        sta     vgk_sy0
gsf_s_v0_done:

        ldx     vgk_v1
        cpx     g_emit_n_input
        bcc     gsf_s_v1_lt
        txa
        sec
        sbc     g_emit_n_input
        tax
        cpx     g_emit_n_clip
        bcs     gsf_s_v1_zero
        lda     clip_x_lo, x
        sta     vgk_sx1_lo
        lda     clip_x_hi, x
        sta     vgk_sx1_hi
        lda     clip_y, x
        sta     vgk_sy1
        bra     gsf_s_v1_done
gsf_s_v1_zero:
        stz     vgk_sx1_lo
        stz     vgk_sx1_hi
        stz     vgk_sy1
        bra     gsf_s_v1_done
gsf_s_v1_lt:
        lda     vgk_edge_flags
        and     #$04
        beq     gsf_s_v1_orig
        cpx     g_emit_n_clip
        bcs     gsf_s_v1_zero
        lda     clip_x_lo, x
        sta     vgk_sx1_lo
        lda     clip_x_hi, x
        sta     vgk_sx1_hi
        lda     clip_y, x
        sta     vgk_sy1
        bra     gsf_s_v1_done
gsf_s_v1_orig:
        lda     scr_x_lo, x
        sta     vgk_sx1_lo
        lda     scr_x_hi, x
        sta     vgk_sx1_hi
        lda     scr_y, x
        sta     vgk_sy1
gsf_s_v1_done:

        lda     vgk_sx0_lo
        sta     $D182
        lda     vgk_sx0_hi
        sta     $D183
        lda     vgk_sx1_lo
        sta     $D184
        lda     vgk_sx1_hi
        sta     $D185
        lda     vgk_sy0
        sta     $D186
        lda     vgk_sy1
        sta     $D187
        lda     g_emit_near_color
        sta     $D181
        lda     g_emit_layer_ctrl
        ora     #$02
        sta     $D180

        lda     #$40
        sta     vgk_fifo_outer
gsf_s_fifo_outer:
        ldx     #$ff
gsf_s_fifo_inner:
        lda     $D182
        ora     $D183
        beq     gsf_s_fifo_ok
        dex
        bne     gsf_s_fifo_inner
        dec     vgk_fifo_outer
        bne     gsf_s_fifo_outer
        ; Timeout
        inc     g_line_fifo_timeouts
        bne     gsf_s_fifo_bail
        inc     g_line_fifo_timeouts + 1
gsf_s_fifo_bail:
        lda     #$00
        sta     $D180
        sta     $D00A
        lda     g_emit_visible_count
        rts
gsf_s_fifo_ok:
        inc     g_emit_visible_count
        lda     g_emit_layer_ctrl
        sta     $D180

gsf_single_next_edge:
        dec     g_emit_edge_count
        beq     gsf_single_done 
        jmp     gsf_single_edge_loop

gsf_single_done:
        lda     #$00
        sta     $D180
        sta     $D00A
        lda     g_emit_visible_count
        rts

; ===========================================================================
; Fast path
;   (vgk_no_near_far_coloring + no hidden-line + no clip + all edges output)
;
; Reads vertex indices directly from model
; All edges are assumed visible 
;
; Register usage:
;   Y  = edge index (0..edge_count-1)  -- decremented via g_emit_edge_count
;   X  = vertex index for scr_* lookups
;   BSS: vgk_fifo_outer (keeps X/Y free for vertex indexing inside FIFO wait)
; ===========================================================================
gsf_fast_path:
        lda     g_emit_edge_count
        bne     gsf_fast_has_edges      ; count > 0 -> proceed
        jmp     gsf_fast_done_noop      ; no edges
gsf_fast_has_edges:

        lda     #$01
        sta     $D00A
        lda     g_emit_layer_ctrl
        sta     $D180

        ldy     #$00

gsf_fast_edge_loop:
        lda     (g_emit_edge_a), y
        tax                             ; X = v0
        lda     scr_x_lo, x
        sta     $D182                   ; x0 lo
        lda     scr_x_hi, x
        sta     $D183                   ; x0 hi
        lda     scr_y, x
        sta     $D186                   ; y0

        lda     (g_emit_edge_b), y
        tax                             ; X = v1
        lda     scr_x_lo, x
        sta     $D184                   ; x1 lo
        lda     scr_x_hi, x
        sta     $D185                   ; x1 hi
        lda     scr_y, x
        sta     $D187                   ; y1

        lda     g_emit_near_color
        sta     $D181                   ; color
        lda     g_emit_layer_ctrl
        ora     #$02
        sta     $D180                   ; clock in

        lda     #$40
        sta     vgk_fifo_outer
gsf_fast_fifo_outer:
        ldx     #$ff
gsf_fast_fifo_inner:
        lda     $D182
        ora     $D183
        beq     gsf_fast_fifo_ok
        dex
        bne     gsf_fast_fifo_inner
        dec     vgk_fifo_outer
        bne     gsf_fast_fifo_outer

        inc     g_line_fifo_timeouts
        bne     gsf_fast_fifo_bail
        inc     g_line_fifo_timeouts + 1
gsf_fast_fifo_bail:
        lda     #$00
        sta     $D180
        sta     $D00A
        lda     g_emit_visible_count
        rts
gsf_fast_fifo_ok:
        inc     g_emit_visible_count
        lda     g_emit_layer_ctrl
        sta     $D180                   

        iny
        dec     g_emit_edge_count
        beq     gsf_fast_loop_done
        jmp     gsf_fast_edge_loop
gsf_fast_loop_done:

gsf_fast_done:
        lda     #$00
        sta     $D180
        sta     $D00A
        lda     g_emit_visible_count
        rts

gsf_fast_done_noop:
        lda     #$00
        rts

