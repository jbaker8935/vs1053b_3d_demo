; emit_edges_asm.s — Monolithic direct-emit of visible screen edges to F256 line-draw hardware.
;
; get_screen_edges_full_asm performs ALL work previously split between the C wrapper
; (get_screen_edges_with_depth) and the old get_screen_edges_asm /
; get_screen_edges_nodepth_asm entry points:
;
;   1. Inline SCI burst-read of screen vertex coords → scr_x_lo/hi/y
;   2. Read n_clip from DSP WRAM
;   3. Fast-path check: flat color + no hidden-line + no clip + all edges output
;      → direct model->edge_a/b lookup, skip edge buffer loading entirely
;   4. Clip vertex coords burst-read → clip_x_lo/hi/y  (slow path only)
;   5. Edge count / flags / packed-v0v1 burst-reads → g_edge_buf_flags/packed
;   6. Two-pass near/far emit (inline hardware writes, no line list) OR
;      single-pass emit when vgk_no_near_far_coloring is set
;
; SCI hardware ports (F256 VS1053b interface):
;   $D700 = VS_SCI_CTRL  (CTRL_Start=0x01, CTRL_RWn=0x02, CTRL_Busy=0x80)
;   $D701 = VS_SCI_ADDR
;   $D702 = VS_SCI_DATA lo byte  ($D703 = hi byte)
;
; SCI register addresses (written to $D701 before a SCI access):
;   SCI_WRAMADDR = 0x07
;   SCI_WRAM     = 0x06
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
; Vertex resolution for both the two-pass and single-pass loops:
;   if v >= g_emit_n_input → offset-encoded clip: ci = v - n_input
;   else if CLIP_Vx flag set → direct-encoded clip: ci = v
;   else → original screen vertex

        .include "build/struct_offsets.inc"

; ---------------------------------------------------------------------------
; Private staging variables — placed in regular BSS (absolute-addressed) to
; avoid exhausting the scarce ZP region.  Accessed with 3-byte absolute LDA/STA
; instead of 2-byte ZP — roughly 1 extra cycle each, fully acceptable.
; ---------------------------------------------------------------------------
        .section .bss
zp_v0:          .space 1        ; v0 raw vertex index from packed edge word
zp_v1:          .space 1        ; v1 raw vertex index from packed edge word
zp_sx0_lo:      .space 1        ; resolved x0 low byte
zp_sx0_hi:      .space 1        ; resolved x0 high byte
zp_sy0:         .space 1        ; resolved y0 (low byte only; screen Y ≤ 255)
zp_sx1_lo:      .space 1        ; resolved x1 low byte
zp_sx1_hi:      .space 1        ; resolved x1 high byte
zp_sy1:         .space 1        ; resolved y1
zp_edge_color:  .space 1        ; selected color (near or far) — kept for compat
zp_edge_flags:  .space 1        ; raw flags byte saved for CLIP_V0/V1 detection
zp_edge_idx:    .space 1        ; edge index into g_edge_buf_flags (uint8_t index)
zp_edge_byte2:  .space 1        ; edge index * 2 (byte offset into g_edge_buf_packed)
zp_fifo_outer:  .space 1        ; FIFO wait outer counter (keeps X/Y free for vertex indexing)

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
; Calling convention (llvm-mos f256): single uint8_t arg in A; return in A.
; ===========================================================================
        .globl vgk_scrn_edges_get_asm

vgk_scrn_edges_get_asm:

; ---------------------------------------------------------------------------
; Block 1: Compute layer control byte (layer<<2)|1 and store.
; ---------------------------------------------------------------------------
        asl
        asl
        ora     #$01
        sta     g_emit_layer_ctrl

; ---------------------------------------------------------------------------
; Block 2: Inline SCI burst-read of screen vertex coords.
;   Set WRAMADDR = VGK_SCREEN_COORDS (0x36C0)
;   Then loop g_emit_n_input times reading [sx_lo, sx_hi, sy_lo] per vertex.
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
; Block 3: Read n_clip from DSP WRAM.
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
; Block 4: Fast path check.
;   Conditions: no_near_far_coloring != 0, vgk_hidden_line_active == 0,
;               g_emit_n_clip == 0, n_out == g_emit_edge_count.
;   If all pass -> JMP gsf_fast_path.
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
; Block 5: Clip vertex coords (if n_clip > 0).
;   VGK_CLIP_SCREEN = 0x3700
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
; Block 6: Edge data burst-reads.
;
;   6a: Read VGK_N_OUTPUT_EDGES -> update g_emit_edge_count.
;       VGK_N_OUTPUT_EDGES = 0x3720
;   6b: Burst-read VGK_OUTPUT_EDGE_FLAGS (0x3721, packed 2 flags/word)
;       -> unpack to g_edge_buf_flags[] (one byte per edge).
;   6c: Burst-read VGK_OUTPUT_EDGE_PACKED (0x3733)
;       -> g_edge_buf_packed[] as raw uint16_t LE pairs.
; ---------------------------------------------------------------------------

        ; 6a: read N_OUTPUT_EDGES
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

        ; 6b: read output edge flags -> g_edge_buf_flags[]
        ;     Flags are packed 2 per WRAM word: lo byte = even-index edge, hi byte = odd.
        ;     VGK_OUTPUT_EDGE_FLAGS = 0x3721
        lda     #$07
        sta     $D701
        lda     #$21                    ; lo of 0x3721
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
        lsr                             ; A = n_flag_words
        tay                             ; Y = word-loop count (countdown)
        beq     gsf_flags_done          ; 0 edges -> skip
        ldx     #$00                    ; X = byte index into g_edge_buf_flags

gsf_flags_loop:
        lda     #$03
        sta     $D700
        stz     $D700
gsf_sci_flags_wait:
        lda     $D700
        bmi     gsf_sci_flags_wait
        lda     $D702                   ; even-index edge flags (lo byte)
        sta     g_edge_buf_flags, x
        inx
        cpx     g_emit_edge_count
        bcs     gsf_flags_done          ; odd slot would be beyond edge count
        lda     $D703                   ; odd-index edge flags (hi byte)
        sta     g_edge_buf_flags, x
        inx
        dey
        bne     gsf_flags_loop

gsf_flags_done:

        ; 6c: burst-read packed v0v1 -> g_edge_buf_packed[]
        ;     VGK_OUTPUT_EDGE_PACKED = 0x3733
        ;     Each WRAM word: lo byte = v0, hi byte = v1.
        lda     #$07
        sta     $D701
        lda     #$33                    ; lo of 0x3733
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
        ldx     #$00                    ; X = byte offset (2 bytes per edge)

gsf_packed_loop:
        lda     #$03
        sta     $D700
        stz     $D700
gsf_sci_packed_wait:
        lda     $D700
        bmi     gsf_sci_packed_wait
        lda     $D702                   ; v0 (lo byte of packed WRAM word)
        sta     g_edge_buf_packed, x
        lda     $D703                   ; v1 (hi byte of packed WRAM word)
        sta     g_edge_buf_packed + 1, x
        inx
        inx
        dey
        bne     gsf_packed_loop

gsf_packed_done:

; ---------------------------------------------------------------------------
; Block 7: Branch to near/far two-pass or single-pass.
; ---------------------------------------------------------------------------
        lda     vgk_no_near_far_coloring
        beq     gsf_do_twopass          ; zero -> two-pass near/far
        jmp     gsf_single_pass         ; non-zero -> single-pass flat color
gsf_do_twopass:

; ===========================================================================
; Block 8: Two-pass near/far direct-emit.
;
; Far pass first (NEAR flag clear), then near pass (NEAR flag set).
; Each pass iterates g_edge_buf_flags/packed from index 0 to edge_count-1.
; Visible edges are written directly to F256 line-draw hardware ($D181..$D187).
;
; Register usage:
;   Y  = edge index (0..edge_count-1)
;   X  = vertex index for scr_*/clip_* lookups
;   BSS: zp_edge_flags, zp_v0, zp_v1, zp_sx0_lo/hi, zp_sy0,
;        zp_sx1_lo/hi, zp_sy1, zp_fifo_outer, zp_edge_byte2
; ===========================================================================

        lda     g_emit_edge_count
        bne     gsf_twopass_has_edges
        jmp     gsf_twopass_done        ; edge_count==0, nothing to draw
gsf_twopass_has_edges:

        lda     #$01
        sta     $D00A                   ; enter line-draw mode
        lda     g_emit_layer_ctrl
        sta     $D180                   ; pre-arm for first line

        ; ----- FAR PASS -----
        ldy     #$00                    ; Y = edge index
        stz     zp_edge_byte2           ; byte offset into g_edge_buf_packed (index*2)

gsf_far_edge_loop:
        lda     g_edge_buf_flags, y
        sta     zp_edge_flags

        ; Advance packed byte offset BEFORE any skip so both counters stay in sync
        lda     zp_edge_byte2
        clc
        adc     #2
        sta     zp_edge_byte2

        ; Test VISIBLE (bit 0) — reload flags; A holds byte2 after the adc above
        lda     zp_edge_flags
        lsr                             ; C = VISIBLE
        bcs     gsf_far_visible
        jmp     gsf_far_next_edge       ; not visible -> skip
gsf_far_visible:

        ; Skip NEAR edges in far pass (bit 3 of raw flags)
        lda     zp_edge_flags
        and     #$08
        beq     gsf_far_is_far          ; NEAR clear -> it's a far edge, process
        jmp     gsf_far_next_edge       ; NEAR set -> reserve for near pass
gsf_far_is_far:

        ; Load v0/v1 from g_edge_buf_packed[zp_edge_byte2 - 2]
        lda     zp_edge_byte2
        sec
        sbc     #2
        tax
        lda     g_edge_buf_packed, x    ; v0
        sta     zp_v0
        lda     g_edge_buf_packed + 1, x ; v1
        sta     zp_v1

        ; Resolve V0
        ldx     zp_v0
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
        sta     zp_sx0_lo
        lda     clip_x_hi, x
        sta     zp_sx0_hi
        lda     clip_y, x
        sta     zp_sy0
        bra     gsf_far_v0_done
gsf_far_v0_zero:
        stz     zp_sx0_lo
        stz     zp_sx0_hi
        stz     zp_sy0
        bra     gsf_far_v0_done
gsf_far_v0_lt_ninput:
        lda     zp_edge_flags
        and     #$02                    ; CLIP_V0
        beq     gsf_far_v0_original
        cpx     g_emit_n_clip
        bcs     gsf_far_v0_zero
        lda     clip_x_lo, x
        sta     zp_sx0_lo
        lda     clip_x_hi, x
        sta     zp_sx0_hi
        lda     clip_y, x
        sta     zp_sy0
        bra     gsf_far_v0_done
gsf_far_v0_original:
        lda     scr_x_lo, x
        sta     zp_sx0_lo
        lda     scr_x_hi, x
        sta     zp_sx0_hi
        lda     scr_y, x
        sta     zp_sy0
gsf_far_v0_done:

        ; Resolve V1
        ldx     zp_v1
        cpx     g_emit_n_input
        bcc     gsf_far_v1_lt_ninput
        txa
        sec
        sbc     g_emit_n_input
        tax
        cpx     g_emit_n_clip
        bcs     gsf_far_v1_zero
        lda     clip_x_lo, x
        sta     zp_sx1_lo
        lda     clip_x_hi, x
        sta     zp_sx1_hi
        lda     clip_y, x
        sta     zp_sy1
        bra     gsf_far_v1_done
gsf_far_v1_zero:
        stz     zp_sx1_lo
        stz     zp_sx1_hi
        stz     zp_sy1
        bra     gsf_far_v1_done
gsf_far_v1_lt_ninput:
        lda     zp_edge_flags
        and     #$04                    ; CLIP_V1
        beq     gsf_far_v1_original
        cpx     g_emit_n_clip
        bcs     gsf_far_v1_zero
        lda     clip_x_lo, x
        sta     zp_sx1_lo
        lda     clip_x_hi, x
        sta     zp_sx1_hi
        lda     clip_y, x
        sta     zp_sy1
        bra     gsf_far_v1_done
gsf_far_v1_original:
        lda     scr_x_lo, x
        sta     zp_sx1_lo
        lda     scr_x_hi, x
        sta     zp_sx1_hi
        lda     scr_y, x
        sta     zp_sy1
gsf_far_v1_done:

        ; Emit far edge
        lda     zp_sx0_lo
        sta     $D182
        lda     zp_sx0_hi
        sta     $D183
        lda     zp_sx1_lo
        sta     $D184
        lda     zp_sx1_hi
        sta     $D185
        lda     zp_sy0
        sta     $D186
        lda     zp_sy1
        sta     $D187
        lda     g_emit_far_color
        sta     $D181
        lda     g_emit_layer_ctrl
        ora     #$02
        sta     $D180

        ; FIFO wait — X free (last vertex index no longer needed); Y preserved as edge index
        lda     #$40
        sta     zp_fifo_outer
gsf_far_fifo_outer:
        ldx     #$ff
gsf_far_fifo_inner:
        lda     $D182
        ora     $D183
        beq     gsf_far_fifo_ok
        dex
        bne     gsf_far_fifo_inner
        dec     zp_fifo_outer
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
        beq     gsf_far_pass_done       ; all far edges done -> fall to near pass
        jmp     gsf_far_edge_loop
gsf_far_pass_done:

        ; ----- NEAR PASS -----
        lda     g_emit_layer_ctrl
        sta     $D180                   ; re-arm for first near line
        ldy     #$00
        stz     zp_edge_byte2

gsf_near_edge_loop:
        lda     g_edge_buf_flags, y
        sta     zp_edge_flags

        lda     zp_edge_byte2
        clc
        adc     #2
        sta     zp_edge_byte2

        ; Test VISIBLE — reload flags; A holds byte2 after the adc above
        lda     zp_edge_flags
        lsr                             ; C = VISIBLE
        bcs     gsf_near_visible
        jmp     gsf_near_next_edge      ; not visible -> skip
gsf_near_visible:

        ; Only process NEAR edges in near pass
        lda     zp_edge_flags
        and     #$08
        bne     gsf_near_is_near        ; NEAR set -> process
        jmp     gsf_near_next_edge      ; NEAR not set -> skip in near pass
gsf_near_is_near:

        ; Load v0/v1
        lda     zp_edge_byte2
        sec
        sbc     #2
        tax
        lda     g_edge_buf_packed, x
        sta     zp_v0
        lda     g_edge_buf_packed + 1, x
        sta     zp_v1

        ; Resolve V0
        ldx     zp_v0
        cpx     g_emit_n_input
        bcc     gsf_near_v0_lt_ninput
        txa
        sec
        sbc     g_emit_n_input
        tax
        cpx     g_emit_n_clip
        bcs     gsf_near_v0_zero
        lda     clip_x_lo, x
        sta     zp_sx0_lo
        lda     clip_x_hi, x
        sta     zp_sx0_hi
        lda     clip_y, x
        sta     zp_sy0
        bra     gsf_near_v0_done
gsf_near_v0_zero:
        stz     zp_sx0_lo
        stz     zp_sx0_hi
        stz     zp_sy0
        bra     gsf_near_v0_done
gsf_near_v0_lt_ninput:
        lda     zp_edge_flags
        and     #$02
        beq     gsf_near_v0_original
        cpx     g_emit_n_clip
        bcs     gsf_near_v0_zero
        lda     clip_x_lo, x
        sta     zp_sx0_lo
        lda     clip_x_hi, x
        sta     zp_sx0_hi
        lda     clip_y, x
        sta     zp_sy0
        bra     gsf_near_v0_done
gsf_near_v0_original:
        lda     scr_x_lo, x
        sta     zp_sx0_lo
        lda     scr_x_hi, x
        sta     zp_sx0_hi
        lda     scr_y, x
        sta     zp_sy0
gsf_near_v0_done:

        ; Resolve V1
        ldx     zp_v1
        cpx     g_emit_n_input
        bcc     gsf_near_v1_lt_ninput
        txa
        sec
        sbc     g_emit_n_input
        tax
        cpx     g_emit_n_clip
        bcs     gsf_near_v1_zero
        lda     clip_x_lo, x
        sta     zp_sx1_lo
        lda     clip_x_hi, x
        sta     zp_sx1_hi
        lda     clip_y, x
        sta     zp_sy1
        bra     gsf_near_v1_done
gsf_near_v1_zero:
        stz     zp_sx1_lo
        stz     zp_sx1_hi
        stz     zp_sy1
        bra     gsf_near_v1_done
gsf_near_v1_lt_ninput:
        lda     zp_edge_flags
        and     #$04
        beq     gsf_near_v1_original
        cpx     g_emit_n_clip
        bcs     gsf_near_v1_zero
        lda     clip_x_lo, x
        sta     zp_sx1_lo
        lda     clip_x_hi, x
        sta     zp_sx1_hi
        lda     clip_y, x
        sta     zp_sy1
        bra     gsf_near_v1_done
gsf_near_v1_original:
        lda     scr_x_lo, x
        sta     zp_sx1_lo
        lda     scr_x_hi, x
        sta     zp_sx1_hi
        lda     scr_y, x
        sta     zp_sy1
gsf_near_v1_done:

        ; Emit near edge
        lda     zp_sx0_lo
        sta     $D182
        lda     zp_sx0_hi
        sta     $D183
        lda     zp_sx1_lo
        sta     $D184
        lda     zp_sx1_hi
        sta     $D185
        lda     zp_sy0
        sta     $D186
        lda     zp_sy1
        sta     $D187
        lda     g_emit_near_color
        sta     $D181
        lda     g_emit_layer_ctrl
        ora     #$02
        sta     $D180

        ; FIFO wait — X free (last vertex index no longer needed); Y preserved as edge index
        lda     #$40
        sta     zp_fifo_outer
gsf_near_fifo_outer:
        ldx     #$ff
gsf_near_fifo_inner:
        lda     $D182
        ora     $D183
        beq     gsf_near_fifo_ok
        dex
        bne     gsf_near_fifo_inner
        dec     zp_fifo_outer
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
; Block 9: Single-pass emit (vgk_no_near_far_coloring set).
;
; Identical vertex-resolve + emit logic to the two-pass loops above, but:
;   color is always g_emit_near_color (no NEAR flag test)
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

        stz     zp_edge_idx
        stz     zp_edge_byte2

gsf_single_edge_loop:
        ldx     zp_edge_byte2
        lda     g_edge_buf_packed, x
        sta     zp_v0
        lda     g_edge_buf_packed + 1, x
        sta     zp_v1

        ldx     zp_edge_idx
        lda     g_edge_buf_flags, x
        sta     zp_edge_flags

        inc     zp_edge_idx
        clc
        lda     zp_edge_byte2
        adc     #2
        sta     zp_edge_byte2

        ; Test VISIBLE — reload flags; A holds byte2 after the adc above
        lda     zp_edge_flags
        lsr                             ; C = VISIBLE
        bcs     gsf_single_visible
        jmp     gsf_single_next_edge
gsf_single_visible:

        ; Resolve V0
        ldx     zp_v0
        cpx     g_emit_n_input
        bcc     gsf_s_v0_lt
        txa
        sec
        sbc     g_emit_n_input
        tax
        cpx     g_emit_n_clip
        bcs     gsf_s_v0_zero
        lda     clip_x_lo, x
        sta     zp_sx0_lo
        lda     clip_x_hi, x
        sta     zp_sx0_hi
        lda     clip_y, x
        sta     zp_sy0
        bra     gsf_s_v0_done
gsf_s_v0_zero:
        stz     zp_sx0_lo
        stz     zp_sx0_hi
        stz     zp_sy0
        bra     gsf_s_v0_done
gsf_s_v0_lt:
        lda     zp_edge_flags
        and     #$02
        beq     gsf_s_v0_orig
        cpx     g_emit_n_clip
        bcs     gsf_s_v0_zero
        lda     clip_x_lo, x
        sta     zp_sx0_lo
        lda     clip_x_hi, x
        sta     zp_sx0_hi
        lda     clip_y, x
        sta     zp_sy0
        bra     gsf_s_v0_done
gsf_s_v0_orig:
        lda     scr_x_lo, x
        sta     zp_sx0_lo
        lda     scr_x_hi, x
        sta     zp_sx0_hi
        lda     scr_y, x
        sta     zp_sy0
gsf_s_v0_done:

        ; Resolve V1
        ldx     zp_v1
        cpx     g_emit_n_input
        bcc     gsf_s_v1_lt
        txa
        sec
        sbc     g_emit_n_input
        tax
        cpx     g_emit_n_clip
        bcs     gsf_s_v1_zero
        lda     clip_x_lo, x
        sta     zp_sx1_lo
        lda     clip_x_hi, x
        sta     zp_sx1_hi
        lda     clip_y, x
        sta     zp_sy1
        bra     gsf_s_v1_done
gsf_s_v1_zero:
        stz     zp_sx1_lo
        stz     zp_sx1_hi
        stz     zp_sy1
        bra     gsf_s_v1_done
gsf_s_v1_lt:
        lda     zp_edge_flags
        and     #$04
        beq     gsf_s_v1_orig
        cpx     g_emit_n_clip
        bcs     gsf_s_v1_zero
        lda     clip_x_lo, x
        sta     zp_sx1_lo
        lda     clip_x_hi, x
        sta     zp_sx1_hi
        lda     clip_y, x
        sta     zp_sy1
        bra     gsf_s_v1_done
gsf_s_v1_orig:
        lda     scr_x_lo, x
        sta     zp_sx1_lo
        lda     scr_x_hi, x
        sta     zp_sx1_hi
        lda     scr_y, x
        sta     zp_sy1
gsf_s_v1_done:

        ; Emit
        lda     zp_sx0_lo
        sta     $D182
        lda     zp_sx0_hi
        sta     $D183
        lda     zp_sx1_lo
        sta     $D184
        lda     zp_sx1_hi
        sta     $D185
        lda     zp_sy0
        sta     $D186
        lda     zp_sy1
        sta     $D187
        lda     g_emit_near_color
        sta     $D181
        lda     g_emit_layer_ctrl
        ora     #$02
        sta     $D180

        ; FIFO wait — X free (last vertex index no longer needed)
        lda     #$40
        sta     zp_fifo_outer
gsf_s_fifo_outer:
        ldx     #$ff
gsf_s_fifo_inner:
        lda     $D182
        ora     $D183
        beq     gsf_s_fifo_ok
        dex
        bne     gsf_s_fifo_inner
        dec     zp_fifo_outer
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
        beq     gsf_single_done         ; count hit 0 -> exit
        jmp     gsf_single_edge_loop

gsf_single_done:
        lda     #$00
        sta     $D180
        sta     $D00A
        lda     g_emit_visible_count
        rts

; ===========================================================================
; Block 10: Fast path
;   (vgk_no_near_far_coloring + no hidden-line + no clip + all edges output)
;
; Reads vertex indices directly from model->edge_a/b via ZP indirect addressing.
; g_emit_edge_a and g_emit_edge_b are ZP pointers (set by C wrapper).
; All edges are assumed visible -- fast-path eligibility check (Block 4) confirmed
; that the DSP output count equals g_emit_edge_count.
;
; Register usage:
;   Y  = edge index (0..edge_count-1)  -- decremented via g_emit_edge_count
;   X  = vertex index for scr_* lookups
;   BSS: zp_fifo_outer (keeps X/Y free for vertex indexing inside FIFO wait)
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
        ; v0 = edge_a[Y]
        lda     (g_emit_edge_a), y
        tax                             ; X = v0
        lda     scr_x_lo, x
        sta     $D182                   ; x0 lo
        lda     scr_x_hi, x
        sta     $D183                   ; x0 hi
        lda     scr_y, x
        sta     $D186                   ; y0

        ; v1 = edge_b[Y]
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

        ; FIFO wait — X free (last vertex index no longer needed); Y preserved as edge index
        lda     #$40
        sta     zp_fifo_outer
gsf_fast_fifo_outer:
        ldx     #$ff
gsf_fast_fifo_inner:
        lda     $D182
        ora     $D183
        beq     gsf_fast_fifo_ok
        dex
        bne     gsf_fast_fifo_inner
        dec     zp_fifo_outer
        bne     gsf_fast_fifo_outer
        ; Timeout
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
        sta     $D180                   ; re-arm for next line

        iny
        dec     g_emit_edge_count
        beq     gsf_fast_loop_done      ; count hit 0 -> all done
        jmp     gsf_fast_edge_loop      ; more edges
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

