;******************************************************************************;
;* S E R I A L   T E R M I N A L   2 0 1 5                               v1.0 *;
;*----------------------------------------------------------------------------*;
;* function      : Terminal program for serial communication based upon       *;
;*                 the RS232 port on an Atom2015                              *;
;*----------------------------------------------------------------------------*;
;* filenaam       : terminal.inc                                              *;
;* auteur         : Kees van Oss                                              *;
;* date           : july 2012                                                 *;
;* latest update  : 23-07-2012 Initial startup                                *;
;*                  28-11-2016 4K circular buffer, works at 115,200 baud      *;
;*----------------------------------------------------------------------------*;
;* assemble       : use CC65 for compiling the program                        *;
;*----------------------------------------------------------------------------*;
;* usage          : TERM <baudrate>                                           *;
;*                  Default settings: none parity, 8 bits, 1 start/stop bit   *;
;*----------------------------------------------------------------------------*;
;* Divisor Latch Byte Values (common baud rates)
;*
;*          Divisor
;* BaudRate  Latch
;*-----------------
;*    300    #682A
;*    600    #3415
;*   1200    #1A0A
;*   2400    #0D05
;*   4800    #0682
;*   9600    #0341
;*  19200    #01A0
;*  38400    #00D0
;*  57600    #008A
;* 115200    #0045
;*----------------------------------------------------------------------------*;
;* Control/status register:
;*
;* Bit      Description         0             1
;*---------------------------------------------------
;* Bit 7    Emulate Break   Disabled       Enabled
;* Bit 6    Emulate Escape  Disabled       Enabled
;* Bit 5-2  Unused
;* Bit 1    Rx Available    Not Available  Available
;* Bit 0    Tx Ready        Not Ready      Ready
;******************************************************************************;

; ==============================================================================
; VARIABELEN DECLARATION
; ==============================================================================

; === GENERAL ===

stackpointer    = $4               ; Standaard Atom adressen
stack1          = $16
stack2          = $25
stack3          = $34
stack4          = $43
esc_key         = $3B
workspace       = $80
snd_x           = $92
snd_y           = $93
rec_x           = $94
rec_y           = $95
cur_y_lb        = $de
cur_y_hb        = $df
cur_x           = $e0
cur_shape       = $e1

inputbuffer     = $100
intvec          = $204
tab             = $321
via_t1_latch    = $B804
via_acr         = $B80B
via_ier         = $B80E
next_param      = $C231
printdec        = $C589
read_param      = $C8BC
printnum        = $F802
printtext       = $F7D1
keyscan         = $FE71
OSRDCHR         = $FE94
newline         = $FFED
printchr        = $FFF4

; === Circular Receive Buffer ===
rcv_buffer      = $4000         ; 0x4000 - 0x4FFF
wrptr           = $96           ; write pointer
rdptr           = $98           ; read pointer

; === SER ===
int_interval    = 50            ; every 50 usec interrupt, sufficient for 115,200 baud
UART_data       = $BDB0
UART_control    = $BDB1
UART_status     = $BDB1
UART_baudrate   = $BDB2         ; lb/hb

;***************************************************************************;
;* T E R M I N A L                                                          ;
;*-------------------------------------------------------------------------*;
;* Program to:                                                             *;
;*   - read and write bytes to/from a serial port                          *;
;***************************************************************************;

; ==============================================================================
; TERM_START
;
; Main loop
; ==============================================================================

term_start:
                lda #0                  ; Init cursor send/receive screen
                sta snd_x
                sta rec_x
                lda #1
                sta snd_y
                lda #8
                sta rec_y

                jsr read_params         ; Read commandline parameters
                jsr param_check         ; Check for legal parameters
                jsr screenlayout        ; Print screenlayout
                jsr ser_init            ; Init serial interface

term_loop:
                jsr keyscan             ; Scan keyboard
                bcs process_rec         ; Skip forward if no key pressed

                jsr translate_key       ; Translate key code to ASCII

                cmp #$7F                ; Backspace?
                beq bs

                cmp #$1B                ; Escape?
                beq term_end

                cmp #$0d                ; Return?
                beq crlf
                bcc control_chr         ; Control code?

                jsr print_snd           ; Print char
                jsr ser_snd             ; Send char to Serial
control_chr:
                jsr keyscan             ; Wait for the key to be released
                bcc control_chr

process_rec:
                lda wrptr               ; test if any characters have been received
                cmp rdptr
                bne process_rec1
                lda wrptr + 1
                cmp rdptr + 1
                beq term_loop

process_rec1:
                ldy #0                  ; print one character
                lda (rdptr), y
                jsr print_rec
                inc rdptr               ; increment the lsb of the rdptr
                bne process_rec
                lda rdptr + 1           ; increment the msb of the rdptr, handling wrap
                clc
                adc #1
                and #$EF
                sta rdptr + 1
                bne process_rec

term_end:
                sei

                lda old_intvec          ; Restore IRQ vector
                sta intvec
                lda old_intvec+1
                sta intvec+1

                lda #12                 ; CLS
                jmp printchr            ; End of program

;---------------------------------------------------
; Handling Control codes
;---------------------------------------------------

crlf:                                   ; Handle $0D CRLF
                jsr print_ret
                lda #'>'
                jsr print_snd
                lda #$0d
                jsr ser_snd             ; Send char to Serial
                lda #$0a
                jsr ser_snd             ; Send char to Serial
                jmp control_chr
bs:                                     ; Handle $7F Backspace
                lda snd_x
                beq bs_1
                dec snd_x
                jmp bs_end
bs_1:
                lda snd_y
                cmp #1
                beq bs_end
                lda #31
                sta snd_x
                dec snd_y
bs_end:
                lda #' '
                ldx snd_x
                ldy snd_y
                jsr gotoxy
                jsr printchr

                jmp control_chr

; ==============================================================================
; READ_PARAMS
;
; Read commandline parameters if given
; Format: <baudrate>
;
; Default baudrateparameter 1200
; ==============================================================================

read_params:
                lda inputbuffer         ; Check if parameters given
                cmp #$0d
                beq set_default

                ldy #0                  ; Yes, read parameters
                sty 3
                lda #<inputbuffer
                sta 5
                lda #>inputbuffer
                sta 6

                jsr read_num            ; Read baudrate
                lda data_stack
                sta baudrate
                lda data_stack+1
                sta baudrate+1
                lda data_stack+2
                sta baudrate+2

                jmp end_read

set_default:                            ; No, set default parameters

                lda #<1200              ; Set baudrate=1200
                sta baudrate
                lda #>1200
                sta baudrate+1

end_read:
                rts

translate_key:
                php
                jmp $FEB1

; ==============================================================================
; PARAM_CHECK
;
; Check if parameters are legal
; ==============================================================================

goto_param_error:
              jmp param_error

param_check:
              ldx #$ff
chk_baud_loop:
              inx
              lda baudrate_tbl_hb,x
              cmp #$ff
              beq param_error
              cmp baudrate+2
              bne chk_baud_loop

              lda baudrate_tbl_mb,x
              cmp baudrate+1
              bne chk_baud_loop

              lda baudrate_tbl_lb,x
              cmp baudrate
              bne chk_baud_loop

              lda baudrate_val_lb,x
              sta divisor
              lda baudrate_val_hb,x
              sta divisor+1

              rts
param_error:
              jsr printtext             ; Print error at parameter
              .byte "BAUDRATE? ",$ea
              brk

; ==============================================================================
; SCREENLAYOUT
;
; Print screen layout with serial port settings at the bottom.
; ==============================================================================

screenlayout:
                lda #12                 ; CLS
                jsr printchr

                lda #0                  ; Hide cursor
                sta cur_shape

                sta tab                 ; Tab = 0

                sta stack2              ; Stack = 0
                sta stack3
                sta stack4

                ldx #<frame             ; Print frame
                ldy #>frame
                jsr buildscreen

                ldx #0                  ; Cursor at 0,15
                ldy #15
                jsr gotoxy

                lda tab                 ; Print baudrate
                pha

                lda #0                  ; @=0
                sta tab

                lda baudrate
                sta stack1
                lda baudrate+1
                sta stack2
                lda baudrate+2
                sta stack3
                lda #0
                sta stack4
                jsr printdec

                pla
                sta tab

                jsr printtext
                .byte ",N,8,1"
                nop

                lda #'>'                ; Print prompt
                jsr print_snd

                ldy #0                  ; Invert lines on screen
                jsr invert_line
                ldy #7
                jsr invert_line
                ldy #15
                jsr invert_line

                rts

; ==============================================================================
; SER_INIT
;
; Initialize serial interface.
; ==============================================================================
ser_init:
                lda #<rcv_buffer        ; reset bufferpointers
                sta wrptr
                sta rdptr
                lda #>rcv_buffer
                sta wrptr + 1
                sta rdptr + 1

                lda divisor             ; set baudrate
                sta UART_baudrate
                lda divisor+1
                sta UART_baudrate+1

                sei

                lda intvec              ; Save interrupt vector
                sta old_intvec
                lda intvec+1
                sta old_intvec+1

                lda #<new_intvec        ; verzet interrupt vector
                sta intvec
                lda #>new_intvec
                sta intvec+1

                lda #$c0                ; timer1 geeft int
                sta via_ier

                lda via_acr
                ora #$c0                ; free running mode
                sta via_acr

                lda #<int_interval      ; zet interval tijd int
                sta via_t1_latch
                lda #>int_interval
                sta via_t1_latch + 1

                cli
                rts

new_intvec:
                lda via_t1_latch        ; 4     4
                lda UART_control        ; 4     4
                and #2                  ; 2     2
                beq return              ; 2     3
                tya                     ; 2
                pha                     ; 3
                ldy #0                  ; 2
                lda UART_data           ; 4
                sta (wrptr), Y          ; 6
                inc wrptr               ; 5
                bne nocarry             ; 2
                lda wrptr + 1           ; 3
                clc                     ; 2
                adc #1                  ; 2
                and #$EF                ; 2
                sta wrptr + 1           ; 3
nocarry:
                pla                     ; 4
                tay                     ; 2
return:
                pla                     ; 4     4
                rti                     ; 6     6
                                        ; --    --
                                        ; 64    23
                                        ; --    --

; ==============================================================================
; SER_SND
;
; Send byte in A over serial interface
; ==============================================================================

ser_snd:
                pha                     ; Save byte to send
                ldx #0
snd_loop:
                lda UART_control
                and #1
                bne snd_ok
                inx
                bne snd_loop
                pla
                jmp snd_end
snd_ok:
                pla
                sta UART_data
snd_end:
                rts

; ==============================================================================
; READ_VAR
;
; Read parameter as letter (A-Z)
; ==============================================================================

read_var:
                 LDY $3
                 JSR skip_spc           ; Negeer spaties

                 CMP #'A'               ; Test of karakter, letter is
                 BMI no_var
                 CMP #'['
                 BPL no_var
                 STA data_stack

                 INY                    ; Spring naar einde parameter
                 STY $3
                 JMP next_param
no_var:
                 jsr printtext
                 .byte "PARAMETER 03",$ea
                 BRK

; ==============================================================================
; READ_NUM
;
; Read parameter as number
; ==============================================================================

read_num:
                 JSR read_param         ; Lees expressie
                 JSR next_param         ; Spring naar einde expressie

                 LDA stack1             ; Zet lo-byte in accu
                 sta data_stack
                 lda stack2
                 sta data_stack+1
                 lda stack3
                 sta data_stack+2
                 LDY #0                 ; Reset stackpointer
                 STY stackpointer
                 RTS

; === SKIP SPATIES
skip:
                 INY                    ; Negeer spaties in interpreter regel
skip_spc:
                 LDA ($05),Y
                 CMP #' '
                 BEQ skip
                 RTS

; ==============================================================================
; BUILDSCREEN
;
; Build screen from table.
; Format table: <x>,<y>,<tekst>,0
;               $ff
; Input: X = lobyte table address
;        Y = hibyte table address
; ==============================================================================

frame_adr       = workspace
screen_adr      = frame_adr+2

buildscreen:
                 stx frame_adr          ; Set frame address
                 sty frame_adr+1

                 ldy #$ff
next_line:
                 iny                    ; Check frame end
                 lda (frame_adr),y
                 cmp #$ff
                 beq build_end
                 sta cur_x              ; Set cur_x

                 iny                    ; Set cur_y
                 lda (frame_adr),y
                 tax
                 lda scr_tab_lb,x
                 sta cur_y_lb
                 lda scr_tab_hb,x
                 sta cur_y_hb
                 iny
build_loop:                             ; Build screen
                 lda (frame_adr),y
                 beq next_line
                 jsr printchr
                 iny
                 jmp build_loop
build_end:
                 rts

; ==============================================================================
; INVERT_LINE
;
; Invert line on screen.
; Input: Y = linenr.
; ==============================================================================

invert_line:
                 lda scr_tab_lb,y
                 sta screen_adr
                 lda scr_tab_hb,y
                 sta screen_adr+1
                 ldy #31
invert_loop:
                 lda (screen_adr),y
                 ora #$80
                 sta (screen_adr),y
                 dey
                 bpl invert_loop
                 rts

; ==============================================================================
; GOTOXY
;
; Locate cursor at x,y
; ==============================================================================

gotoxy:
                 pha

                 stx cur_x              ; Set cur_x

                 lda scr_tab_lb,y       ; Set cur_y
                 sta cur_y_lb
                 lda scr_tab_hb,y
                 sta cur_y_hb

                 pla
                 rts

; ==============================================================================
; PRINT_SND
;
; Print character in A in send window
; ==============================================================================

print_snd:
                 sta a_tmp
                 sei
                 ldx snd_x              ; Set cursor in send window
                 ldy snd_y
                 jsr gotoxy

                 jsr printchr           ; Print character
                 cli

                 inc snd_x              ; Cursor to next position
                 lda snd_x
                 cmp #32                ; If end of line CRLF
                 bne end_print_snd
print_ret:
                 lda #0                 ; CR
                 sta snd_x

                 lda snd_y              ; LF
                 cmp #6
                 beq scroll_snd
                 inc snd_y
end_print_snd:
                 lda a_tmp
                 rts
scroll_snd:
                 ldx #0
scroll_snd_loop:
                 lda $8040,x
                 sta $8020,x
                 inx
                 cpx #160
                 bne scroll_snd_loop

                 ldx #31
                 lda #32
clear_snd:
                 sta $80c0,x
                 dex
                 bpl clear_snd

                 rts

; ==============================================================================
; PRINT_REC
;
; Check if character in buffer, if so then print in recieve window
; ==============================================================================

print_rec:
                 ldx rec_x              ; Position cursor
                 ldy rec_y
                 jsr gotoxy

                 cmp #$0d
                 beq rec_ret
                 cmp #32                ; Check if ctrl-char
                 bcc end_print_rec      ; If ctrl-char, do not print

                 jsr printchr           ; Print char

                 inc rec_x              ; Increment x-cursor
                 lda rec_x              ; If end of line, cr+lf
                 cmp #32
                 bne end_print_rec
rec_ret:
                 lda #0
                 sta rec_x

                 lda rec_y              ; Check scrolling window
                 cmp #14
                 beq scroll_rec
                 inc rec_y
end_print_rec:
                 rts
scroll_rec:
                 ldx #0                 ; Copy lines 8-14 to 7-13
scroll_rec_loop:
                 lda $8120,x
                 sta $8100,x
                 inx
                 cpx #192
                 bne scroll_rec_loop

                 ldx #31
                 lda #32
clear_rec:
                 sta $81c0,x            ; Clear line 14
                 dex
                 bpl clear_rec

                 rts

; ==============================================================================
; SCREENLAYOUT TABLES
; ==============================================================================

frame:
                 .byte 0, 0,"ATOMIC SERIAL TERMINAL      V1.0",0
                 .byte 0, 7,"--------------------------------",0
                 .byte 0,15,"                        ESC=END",30,0
                 .byte $ff

; ==============================================================================
; TABLES
; ==============================================================================

scr_tab_lb:
                 .byte $00,$20,$40,$60,$80,$a0,$c0,$e0
                 .byte $00,$20,$40,$60,$80,$a0,$c0,$e0
scr_tab_hb:
                 .byte $80,$80,$80,$80,$80,$80,$80,$80
                 .byte $81,$81,$81,$81,$81,$81,$81,$81
;-----------------------------------
baudrate_tbl_lb:
                 .byte <300,<600,<1200,<2400,<4800,<9600,<19200,<38400,<57600,<115200,$ff
baudrate_tbl_mb:
                 .byte <(300/256),<(600/256),<(1200/256),<(2400/256),<(4800/256)
                 .byte <(9600/256),<(19200/256),<(38400/256),<(57600/256),<(115200/256),$ff
baudrate_tbl_hb:
                 .byte <(300/65536),<(600/65536),<(1200/65536),<(2400/65536),<(4800/65536)
                 .byte <(9600/65536),<(19200/65536),<(38400/65536),<(57600/65536),<(115200/65536),$ff
baudrate_val_lb:
                 .byte $2a,$15,$0a,$05,$82,$41,$a0,$d0,$8a,$45
baudrate_val_hb:
                 .byte $68,$34,$1a,$0d,$06,$03,$01,$00,$00,$00

; ==============================================================================
; VARIABLE DECLARATION
; ==============================================================================

ser_rxptr:     .byte 0
ser_rdptr:     .byte 0
data_stack:    .byte 0,0
y_tmp:         .byte 0
old_intvec:    .byte 0,0
a_tmp:         .byte 0

comport:       .byte 0
baudrate:      .byte 0,0,0
parity:        .byte 0
length:        .byte 0
stopbits:      .byte 0

divisor:       .byte 0,0
LCR:           .byte 0
par_count:     .byte 0
