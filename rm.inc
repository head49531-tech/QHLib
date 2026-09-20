; ============================================================================
; rm.inc — Real Mode Library (corrected v4, with protected mode support)
; ============================================================================
; A 16-bit real mode library of macros, EQUs, and emit-once routines.
;
; Usage:
;   %include "rm.inc"
;
; Multi-file safety:
;   The emit-once routines are guarded by %ifndef. NASM labels are local to
;   the object file, so linking multiple .o files that each include this
;   header does NOT produce duplicate-symbol errors.
;
; Register/flag conventions used in this file:
;   * Macros list exactly what they preserve. Where the comment says a
;     register is preserved, it is preserved (including across BIOS calls).
;   * Flags: unless stated otherwise, assume flags are undefined on exit.
;   * Direction Flag (DF) is explicitly cleared with `cld` before any string
;     instruction. Callers do not need to pre-clear DF.
;   * Embedded strings in `print` are read with a `cs:` override, so DS may
;     point anywhere, provided CS correctly addresses the code.
;   * All int 0x10 teletype calls set BH = 0 (video page 0) explicitly.
;     A caller with a non-zero BH will NOT have it preserved across print
;     macros.
;
; Protected mode:
;   The `loadpmode` macro performs a complete 16-bit real -> 32-bit protected
;   transition: enable A20, load a flat GDT, set CR0.PE, far-jump, set up
;   segments and stack, and jump to the user's 32-bit entry point. See
;   SECTION 13.
;
;   SECTION 14 provides 32-bit output utilities that write directly to the
;   VGA text buffer (BIOS int 0x10 is unavailable in protected mode).
;
;   All PM utilities assume a flat 4 GB model with DS=ES=FS=GS=SS=0x10
;   and CS=PM_CODE_SEL, i.e. exactly what `loadpmode` installs.
;   FIX (v4): the header previously said DS=ES=FS=GS=SS=0, which did not
;   match the code; corrected to 0x10 (PM_DATA_SEL).
;
; Rules:
;   Macros are lowercase, constants are UPPERCASE.
;
; QH 2026
; ============================================================================

%ifndef RM_INC
%define RM_INC

; ============================================================================
; SECTION 1: CONFIGURATION CONSTANTS
; ============================================================================

; --- Boot Configuration ---
%ifndef LOAD_ADDR
%define LOAD_ADDR       0x7C00
%endif
%ifndef BOOTSIG
%define BOOTSIG         0xAA55
%endif
%ifndef PT
%define PT              510
%endif
%ifndef SECTOR_SIZE
%define SECTOR_SIZE     512
%endif
%ifndef STACK_ADDR
%define STACK_ADDR      LOAD_ADDR
%endif

; --- Kernel Load Configuration ---
%ifndef KERNEL_SEG
%define KERNEL_SEG      0x1000
%endif
%ifndef KERNEL_OFF
%define KERNEL_OFF      0x0000
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS  64
%endif

; --- Video / VGA Text Mode ---
%define VIDMEM          0xB8000
%define VIDMEM_SEG      0xB800
%define VGA_COLS        80
%define VGA_ROWS        25
%define VGA_ATTR        0x07
%define VGA_SIZE        (VGA_COLS * VGA_ROWS)
%define VGA_CRTC_INDEX  0x3D4
%define VGA_CRTC_DATA   0x3D5
%define VGA_CURSOR_HI   0x0E
%define VGA_CURSOR_LO   0x0F

; --- Colors ---
%define BLACK           0x00
%define BLUE            0x01
%define GREEN           0x02
%define CYAN            0x03
%define RED             0x04
%define MAGENTA         0x05
%define BROWN           0x06
%define LIGHT_GRAY      0x07
%define DARK_GRAY       0x08
%define LIGHT_BLUE      0x09
%define LIGHT_GREEN     0x0A
%define LIGHT_CYAN      0x0B
%define LIGHT_RED       0x0C
%define LIGHT_MAGENTA   0x0D
%define YELLOW          0x0E
%define WHITE           0x0F

; --- BIOS Video Functions ---
%define TELETYPE        0x0E
%define VIDEO_SET_MODE  0x00
%define VIDEO_SET_CUR   0x02
%define VIDEO_GET_CUR   0x03
%define VIDEO_SCROLL_UP 0x06
%define VIDEO_SCROLL_DN 0x07

; --- Video Modes ---
%define MODE_TEXT_80x25 0x03
%define MODE_13H        0x13
%define MODE_12H        0x12
%define MODE_TEXT_MONO  0x07

; --- Keyboard Scancodes ---
%define KEY_ESC         0x01
%define KEY_BACKSPACE   0x0E
%define KEY_TAB         0x0F
%define KEY_ENTER       0x1C
%define KEY_LCTRL       0x1D
%define KEY_LSHIFT      0x2A
%define KEY_RSHIFT      0x36
%define KEY_LALT        0x38
%define KEY_SPACE       0x39
%define KEY_CAPSLOCK    0x3A
%define KEY_F1          0x3B
%define KEY_F2          0x3C
%define KEY_F3          0x3D
%define KEY_F4          0x3E
%define KEY_F5          0x3F
%define KEY_F6          0x40
%define KEY_F7          0x41
%define KEY_F8          0x42
%define KEY_F9          0x43
%define KEY_F10         0x44
%define KEY_NUMLOCK     0x45
%define KEY_SCROLLLOCK  0x46
%define KEY_HOME        0x47
%define KEY_UP          0x48
%define KEY_PGUP        0x49
%define KEY_LEFT        0x4B
%define KEY_RIGHT       0x4D
%define KEY_END         0x4F
%define KEY_DOWN        0x50
%define KEY_PGDN        0x51
%define KEY_INSERT      0x52
%define KEY_DELETE      0x53

; --- BIOS Keyboard ---
%define KBD_GET_KEY     0x00
%define KBD_CHECK_KEY   0x01

; --- Disk ---
%define DISK_RESET      0x00
%define DISK_READ       0x02
%define DISK_WRITE      0x03
%define DISK_READ_LBA   0x42
%define DISK_WRITE_LBA  0x43
%define DISK_GET_PARAMS 0x08

%define DRIVE_A         0x00
%define DRIVE_B         0x01
%define DRIVE_C         0x80
%define DRIVE_D         0x81

; --- A20 Gate ---
%define A20_PORT        0x92
%define A20_BIT         0x02
%define KBC_DATA        0x60
%define KBC_STATUS      0x64
%define KBC_CMD_WRITE   0xD1
%define KBC_CMD_READ    0xD0
%define KBC_CMD_DISABLE 0xAD
%define KBC_CMD_ENABLE  0xAE
%define KBC_A20_BIT     0x02

; --- PIT ---
%define PIT_CH0         0x40
%define PIT_CH1         0x41
%define PIT_CH2         0x42
%define PIT_CMD         0x43
%define PIT_FREQ        1193182

; --- CMOS ---
%define CMOS_ADDR       0x70
%define CMOS_DATA       0x71
%define CMOS_SHUTDOWN   0x0F

; --- E820 ---
%define E820_SIGNATURE  0x534D4150
%define E820_INT        0x15

; --- Protected Mode Bits ---
%define CR0_PE          0x00000001
%define CR0_PG          0x80000000
%define CR4_PAE         0x00000020
%define EFER_MSR        0xC0000080
%define EFER_LME        0x00000100
%define EFER_LMA        0x00000400

; ============================================================================
; SECTION 2: OPTIONAL REGISTER ALIASES
; ============================================================================
; Uncomment to use. Only sane with `mov`. Don't push/pop with these.

; %define r_ss ss
; %define r_sp sp
; %define r_ax ax
; %define r_bx bx
; %define r_cx cx
; %define r_dx dx
; %define r_si si
; %define r_di di
; %define stackseg ss
; %define stackptr sp

; ============================================================================
; SECTION 3: BOOT MACROS
; ============================================================================

%macro sof 0
bits 16
org LOAD_ADDR
%endmacro

%macro bootloader 0
times PT-($-$$) db 0
dw BOOTSIG
%endmacro

%macro setupdata 0
xor ax, ax
mov ds, ax
mov es, ax
%endmacro

%macro setupstack 0
xor ax, ax
mov ss, ax
mov sp, STACK_ADDR
%endmacro

%macro halt 0
cli
hlt
jmp $
%endmacro

; ============================================================================
; SECTION 4: TEXT OUTPUT — MACROS
; ============================================================================

; --- print ---
; Prints a string literal. Inline, no routine call.
; Preserves AX, SI. BH is clobbered to 0. Flags undefined on exit.
%macro print 1
jmp %%skip
%%msg: db %1, 0
%%skip:
push si
push ax
mov si, %%msg
cld
%%loop:
cs lodsb
or al, al
jz %%done
cmp al, 0x0A
jne %%emit
mov al, 0x0D
mov ah, TELETYPE
xor bh, bh
int 0x10
mov al, 0x0A
%%emit:
mov ah, TELETYPE
xor bh, bh
int 0x10
jmp %%loop
%%done:
pop ax
pop si
%endmacro

; --- print_at_si ---
; Prints a null-terminated string at DS:SI. Clobbers AX, SI, BH.
%macro print_at_si 0
cld
%%loop:
lodsb
or al, al
jz %%done
cmp al, 0x0A
jne %%emit
mov al, 0x0D
mov ah, TELETYPE
xor bh, bh
int 0x10
mov al, 0x0A
%%emit:
mov ah, TELETYPE
xor bh, bh
int 0x10
jmp %%loop
%%done:
%endmacro

; --- print_char ---
; Prints char in AL. Clobbers AH, BH, flags.
%macro print_char 0
mov ah, TELETYPE
xor bh, bh
int 0x10
%endmacro

; --- print_nl ---
; Prints CR+LF. Preserves AX. BH is clobbered to 0.
%macro print_nl 0
push ax
mov al, 0x0D
mov ah, TELETYPE
xor bh, bh
int 0x10
mov al, 0x0A
xor bh, bh
int 0x10
pop ax
%endmacro

; --- print_hex_byte ---
; Prints byte in AL as 2 hex digits. Preserves AX, BX, CX, DX.
%macro print_hex_byte 0
push ax
push bx
push cx
push dx
mov bl, al
shr al, 4
call rm__hex_nibble
mov al, bl
call rm__hex_nibble
pop dx
pop cx
pop bx
pop ax
%endmacro

; --- print_hex_word ---
; Prints word in AX as 4 hex digits. Preserves AX, BX, CX, DX.
%macro print_hex_word 0
push ax
mov al, ah
print_hex_byte
pop ax
print_hex_byte
%endmacro

; --- print_dec ---
; Prints unsigned word in AX as decimal. Preserves AX, BX, CX, DX.
%macro print_dec 0
push ax
push bx
push cx
push dx
mov cx, 0
mov bx, 10
%%divide:
xor dx, dx
div bx
push dx
inc cx
test ax, ax
jnz %%divide
%%print:
pop dx
mov al, dl
add al, '0'
mov ah, TELETYPE
xor bh, bh
push cx
int 0x10
pop cx
loop %%print
pop dx
pop cx
pop bx
pop ax
%endmacro

; ============================================================================
; SECTION 5: SCREEN CONTROL — MACROS
; ============================================================================

; --- clear_screen ---
; Clears screen with attribute in AL. Preserves AX, CX, DI, ES.
%macro clear_screen 0
push es
push di
push ax
push cx
mov ah, al
mov al, ' '
mov cx, VIDMEM_SEG
mov es, cx
xor di, di
cld
mov cx, VGA_SIZE
rep stosw
pop cx
pop ax
pop di
pop es
%endmacro

; --- set_cursor ---
; Sets hardware cursor. DH=row, DL=col. Preserves AX, BX.
%macro set_cursor 0
push ax
push bx
mov ah, VIDEO_SET_CUR
mov bh, 0
int 0x10
pop bx
pop ax
%endmacro

; --- get_cursor ---
; Gets cursor. Returns DH=row, DL=col. Preserves AX, BX, CX.
%macro get_cursor 0
push ax
push bx
push cx
mov ah, VIDEO_GET_CUR
mov bh, 0
int 0x10
pop cx
pop bx
pop ax
%endmacro

; --- scroll_up ---
; Scrolls screen up 1 line with attribute AL. Preserves AX, BX, CX, DX.
%macro scroll_up 0
push ax
push bx
push cx
push dx
mov bh, al
mov ah, VIDEO_SCROLL_UP
mov al, 1
mov cx, 0
mov dx, 0x184F
int 0x10
pop dx
pop cx
pop bx
pop ax
%endmacro

; ============================================================================
; SECTION 6: KEYBOARD — MACROS
; ============================================================================

%macro get_key 0
mov ah, KBD_GET_KEY
int 0x16
%endmacro

%macro check_key 0
mov ah, KBD_CHECK_KEY
int 0x16
%endmacro

; ============================================================================
; SECTION 7: DISK I/O — MACROS
; ============================================================================

; --- disk_reset ---
; Input: DL = drive. Output: CF=1 on error, AH=status.
%macro disk_reset 0
mov ah, DISK_RESET
int 0x13
%endmacro

; --- disk_read_chs ---
; Setup AL, CH, CL, DH, DL, ES:BX first. CF=1 on error, AH=status.
%macro disk_read_chs 0
mov ah, DISK_READ
int 0x13
%endmacro

; --- disk_read_lba ---
; Setup DL=drive, DS:SI=DAP. CF=1 on error, AH=status.
%macro disk_read_lba 0
mov ah, DISK_READ_LBA
int 0x13
%endmacro

; --- dap_define ---
; Usage: dap_define my_dap, count, buf_off, buf_seg, lba
%macro dap_define 5
%1:
db 0x10
db 0
dw %2
dw %3
dw %4
dq %5
%endmacro

; ============================================================================
; SECTION 8: A20 GATE — MACROS
; ============================================================================

; --- enable_a20 ---
; Fast A20 via port 0x92. Preserves AX.
%macro enable_a20 0
push ax
in al, A20_PORT
or al, A20_BIT
out A20_PORT, al
pop ax
%endmacro

; --- enable_a20_kbc ---
; Keyboard controller method. Preserves AX, BX and the interrupt-enable state.
%macro enable_a20_kbc 0
push ax
push bx
pushf
cli

rm__kbc_wait_in
mov al, KBC_CMD_DISABLE
out KBC_STATUS, al

rm__kbc_wait_in
mov al, KBC_CMD_READ
out KBC_STATUS, al

rm__kbc_wait_out
in al, KBC_DATA
mov bl, al

rm__kbc_wait_in
mov al, KBC_CMD_WRITE
out KBC_STATUS, al

rm__kbc_wait_in
mov al, bl
or al, KBC_A20_BIT
out KBC_DATA, al

rm__kbc_wait_in
mov al, KBC_CMD_ENABLE
out KBC_STATUS, al

popf
pop bx
pop ax
%endmacro

%macro rm__kbc_wait_in 0
%%wait:
in al, KBC_STATUS
test al, 2
jnz %%wait
%endmacro

%macro rm__kbc_wait_out 0
%%wait:
in al, KBC_STATUS
test al, 1
jz %%wait
%endmacro

; ============================================================================
; SECTION 9: VIDEO MODE — MACROS
; ============================================================================

%macro set_video_mode 0
mov ah, VIDEO_SET_MODE
int 0x10
%endmacro

; ============================================================================
; SECTION 10: TIMER & DELAY — MACROS
; ============================================================================

; --- delay ---
; Crude busy-wait. CX = approximate outer-loop count. Preserves AX, CX, DX.
%macro delay 0
push ax
push cx
push dx
%%outer:
mov dx, 0xFFFF
%%inner:
dec dx
jnz %%inner
loop %%outer
pop dx
pop cx
pop ax
%endmacro

; ============================================================================
; SECTION 11: SYSTEM CONTROL — MACROS
; ============================================================================

%macro reboot 0
cli
%%wait:
in al, KBC_STATUS
test al, 2
jnz %%wait
mov al, 0xFE
out KBC_STATUS, al
%endmacro

; ============================================================================
; SECTION 12: PROTECTED MODE — CONFIGURATION
; ============================================================================

; --- GDT selectors installed by `loadpmode` ---
%define PM_CODE_SEL     0x08        ; flat 32-bit code, ring 0
%define PM_DATA_SEL     0x10        ; flat 32-bit data, ring 0

; --- Protected mode stack ---
; The 32-bit stack grows down from here. Default 0x90000 sits in the gap
; between the bootloader image and the EBDA.
;
; WARNING (v4): 0x90000 is a heuristic, not a guarantee. On some systems the
; Extended BIOS Data Area (EBDA) begins as low as 0x80000, in which case the
; first 32-bit push silently overwrites it. If you target hardware where
; this matters, override PM_STACK_TOP before including rm.inc, or read the
; EBDA segment from the BIOS data area at physical 0040:000E at runtime and
; choose a stack top below it.
%ifndef PM_STACK_TOP
%define PM_STACK_TOP    0x90000
%endif

; --- A20 enable method used by `loadpmode` ---
;   1 = fast A20 (port 0x92)        — fast, works on nearly all systems
;   2 = keyboard controller method  — slower, more compatible
;   3 = both (fast then KBC)        — belt and suspenders, slowest
%ifndef PM_A20_METHOD
%define PM_A20_METHOD   1
%endif

; --- VGA text buffer in 32-bit flat mode ---
%define PM_VGA_BASE     0xB8000
%define PM_VGA_COLS     80
%define PM_VGA_ROWS     25
%define PM_VGA_SIZE     (PM_VGA_COLS * PM_VGA_ROWS)
%define PM_VGA_ATTR     0x07

; ============================================================================
; SECTION 13: PROTECTED MODE — TRANSITION
; ============================================================================

; --- pm__enable_a20 ---
; Internal. Selects the A20 method configured by PM_A20_METHOD.
%macro pm__enable_a20 0
%if PM_A20_METHOD == 1
enable_a20
%elif PM_A20_METHOD == 2
enable_a20_kbc
%elif PM_A20_METHOD == 3
enable_a20
enable_a20_kbc
%else
%error "PM_A20_METHOD must be 1, 2, or 3"
%endif
%endmacro

; --- loadpmode ---
; The Big One. Transitions from 16-bit real mode to 32-bit protected mode.
;
;   %1 = 32-bit entry point (linear address; a label or a constant).
;
; Performs, in order:
;   1. cli                 — disable interrupts (no IDT is installed)
;   2. A20 enable          — according to PM_A20_METHOD
;   3. lgdt [rm__gdtr]     — load the built-in flat GDT
;   4. set CR0.PE          — enter protected mode
;   5. far jump            — flush the pipeline; load CS with 0x08
;   6. 32-bit stub         — load DS/ES/FS/GS/SS with 0x10, set ESP and EBP
;                            to PM_STACK_TOP, and jump to %1
;
; The built-in GDT is a flat 4 GB model:
;   0x00  null
;   0x08  32-bit code, base 0, limit 4 GB, DPL 0
;   0x10  32-bit data, base 0, limit 4 GB, DPL 0
;
; Register state on entry to %1:
;   CS = 0x08, DS/ES/FS/GS/SS = 0x10
;   ESP = EBP = PM_STACK_TOP
;   EAX = undefined
;   EBX/ECX/EDX/ESI/EDI = preserved from real mode (upper halves zeroed
;                          by the far jump's operand-size change)
;   EFLAGS = undefined (IF cleared)
;
; ASSEMBLER STATE WARNING (v4):
;   `loadpmode` toggles `bits 32` for its inline stub and then restores
;   `bits 16`. NASM's `bits` directive is a global, source-order toggle,
;   not scoped to the macro body. As a consequence, after `loadpmode` is
;   expanded the assembler is in `bits 16` mode regardless of what it was
;   before. This is correct for the intended usage (caller is already in
;   `bits 16` real-mode code). If you invoke `loadpmode` from a `bits 32`
;   context — which is unusual and generally wrong — you must re-issue
;   `bits 32` afterwards. Keep `loadpmode` invocations confined to 16-bit
;   code.
;
; IMPORTANT: execution never returns to the 16-bit caller. Place your
;            32-bit code in a `bits 32` section and pass its label.
;
; Space cost: the transition stub is ~30 bytes, the GDT + GDTR ~30 bytes.
; Budget ~64 bytes in your bootloader for `loadpmode` itself.
%macro loadpmode 1
cli
pm__enable_a20

lgdt [rm__gdtr]

mov eax, cr0
or eax, CR0_PE
mov cr0, eax

; Far jump: flush pipeline, load CS with PM_CODE_SEL.
jmp PM_CODE_SEL:%%pm_stub

bits 32
%%pm_stub:
mov ax, PM_DATA_SEL
mov ds, ax
mov es, ax
mov fs, ax
mov gs, ax
mov ss, ax
mov esp, PM_STACK_TOP
mov ebp, esp
jmp %1
bits 16

; --- Built-in GDT (emitted once per translation unit) ---
%ifndef RM_GDT_EMITTED
%define RM_GDT_EMITTED
align 8, db 0
rm__gdt:
; 0x00 — null descriptor
dq 0x0000000000000000
; 0x08 — 32-bit code, flat, ring 0
;   access 0x9A = P=1 DPL=00 S=1 Type=1010 (code, exec/read)
;   flags  0xCF = G=1 D/B=1 L=0 AVL=0 limit_hi=F
rm__gdt_code32:
dw 0xFFFF
dw 0x0000
db 0x00
db 0x9A
db 0xCF
db 0x00
; 0x10 — 32-bit data, flat, ring 0
;   access 0x92 = P=1 DPL=00 S=1 Type=0010 (data, read/write)
;   flags  0xCF
rm__gdt_data32:
dw 0xFFFF
dw 0x0000
db 0x00
db 0x92
db 0xCF
db 0x00
rm__gdt_end:
rm__gdtr:
dw rm__gdt_end - rm__gdt - 1
dd rm__gdt
%endif
%endmacro

; ============================================================================
; SECTION 14: PROTECTED MODE — 32-BIT OUTPUT UTILITIES
; ============================================================================
; All macros in this section are intended to be expanded inside `bits 32`
; code, with the flat CS/DS layout that `loadpmode` installs. BIOS int 0x10
; is unavailable; output goes directly to the VGA text buffer.
;
; PERFORMANCE NOTE (v4):
;   `pm_print` / `pm__putc` round-trip the VGA CRTC on every character to
;   keep the hardware cursor in sync. Port I/O to the CRTC is slow — each
;   `out` to 0x3D4/0x3D5 has microsecond-scale latency on real hardware,
;   and `pm__putc` performs several per character. This is appropriate for
;   boot-stage console output (a few dozen lines), not for high-throughput
;   text. A faster variant would cache the cursor offset in a register or
;   memory variable between calls; that is left as an exercise to the
;   integrator.

; --- pm_halt ---
; Halt the CPU in 32-bit protected mode. Does not return.
%macro pm_halt 0
%%loop:
cli
hlt
jmp %%loop
%endmacro

; --- pm_clear ---
; Clears the VGA text buffer and resets the hardware cursor to 0,0.
;   %1 = attribute byte (e.g. 0x07 for light gray on black)
; Clobbers EAX, ECX, EDI. Preserves EBX, EDX, ESI, EBP.
%macro pm_clear 1
push eax
push ecx
push edi
mov edi, PM_VGA_BASE
mov ecx, PM_VGA_SIZE
mov ah, %1
mov al, ' '
cld
rep stosw
xor eax, eax
call pm__set_cursor
pop edi
pop ecx
pop eax
%endmacro

; --- pm_print ---
; Prints a null-terminated string literal at the hardware cursor, handling
; LF (0x0A) as a line break, CR (0x0D) as a no-op, wrapping at column 80,
; and scrolling when the last row is reached.
; Clobbers EAX, EBX, ECX, EDX, ESI, EDI. Preserves EBP.
;
; See the PERFORMANCE NOTE at the top of this section.
%macro pm_print 1
jmp %%skip
%%msg: db %1, 0
%%skip:
push esi
mov esi, %%msg
%%loop:
lodsb
or al, al
jz %%done
call pm__putc
jmp %%loop
%%done:
pop esi
%endmacro

; --- pm_print_at ---
; Prints a string literal at a fixed row/column. Does not touch the
; hardware cursor.
;   %1 = row (0..24)
;   %2 = column (0..79)
;   %3 = string literal (printed verbatim, no newline handling)
;
; The string is clipped at the end of the target row: at most
; (PM_VGA_COLS - %2) characters are written, so a long string cannot bleed
; into the next row.
;
; Clobbers EAX, ECX, EDI, ESI. Preserves EBX, EDX, EBP.
%macro pm_print_at 3
; FIX (v4): compile-time bounds check on row and column.
%if %1 >= PM_VGA_ROWS
%error "pm_print_at: row %1 out of range (0..24)"
%endif
%if %2 >= PM_VGA_COLS
%error "pm_print_at: column %2 out of range (0..79)"
%endif
push eax
push ecx
push esi
push edi
mov eax, %1
imul eax, PM_VGA_COLS
add eax, %2
add eax, eax
mov edi, PM_VGA_BASE
add edi, eax
jmp %%skip
%%msg: db %3, 0
%%skip:
mov esi, %%msg
; FIX (v4): ECX is the remaining-cell budget for this row; `loop` both
; decrements it and terminates the loop, so the write cannot spill past
; the row boundary even if the string is longer than the space left.
mov ecx, PM_VGA_COLS - %2
cld
%%loop:
lodsb
or al, al
jz %%done
mov ah, PM_VGA_ATTR
stosw
loop %%loop
%%done:
pop edi
pop esi
pop ecx
pop eax
%endmacro

; --- pm_print_hex_byte ---
; Prints AL as 2 hex digits at the hardware cursor. Preserves EAX.
%macro pm_print_hex_byte 0
push eax
push ebx
mov bl, al
shr al, 4
call pm__hex_nibble
mov al, bl
call pm__hex_nibble
pop ebx
pop eax
%endmacro

; --- pm_print_hex_word ---
; Prints AX as 4 hex digits. Preserves EAX.
%macro pm_print_hex_word 0
push eax
mov al, ah
pm_print_hex_byte
pop eax
pm_print_hex_byte
%endmacro

; --- pm_print_hex_dword ---
; Prints EAX as 8 hex digits. Preserves EAX.
%macro pm_print_hex_dword 0
push eax
shr eax, 16
pm_print_hex_word
pop eax
pm_print_hex_word
%endmacro

; ============================================================================
; SECTION 15: EMIT-ONCE ROUTINES (16-BIT REAL MODE)
; ============================================================================

%ifndef RM_ROUTINES_EMITTED
%define RM_ROUTINES_EMITTED

; --- rm__hex_nibble ---
; 16-bit real-mode helper. Prints the low nibble of AL via BIOS teletype.
; Callers save AX around the call, so the exact clobber set is internal.
rm__hex_nibble:
and al, 0x0F
cmp al, 10
jb .digit
add al, 'A' - 10
jmp .emit
.digit:
add al, '0'
.emit:
mov ah, TELETYPE
xor bh, bh
int 0x10
ret

%endif ; RM_ROUTINES_EMITTED

; ============================================================================
; SECTION 16: PROTECTED MODE — EMIT-ONCE ROUTINES
; ============================================================================
; These are 32-bit flat routines. They are only reachable after `loadpmode`
; has installed the flat GDT and set the segment registers accordingly.

%ifndef RM_PM_ROUTINES_EMITTED
%define RM_PM_ROUTINES_EMITTED

bits 32

; --- pm__set_cursor ---
; Sets the VGA hardware cursor. Input: AX = linear text offset (0..1999).
;
; Preserves EAX, EBX, EDX.
; FIX (v4): doc comment corrected — the previous text claimed these were
; clobbered, but the body pushes/pops all three.
pm__set_cursor:
push eax
push ebx
push edx
mov bx, ax
mov dx, VGA_CRTC_INDEX
mov al, VGA_CURSOR_HI
out dx, al
inc dx
mov al, bh
out dx, al
dec dx
mov al, VGA_CURSOR_LO
out dx, al
inc dx
mov al, bl
out dx, al
pop edx
pop ebx
pop eax
ret

; --- pm__get_cursor ---
; Reads the VGA hardware cursor. Returns: AX = linear text offset (0..1999).
;
; Preserves EBX, EDX. EAX holds the return value.
; FIX (v4): doc comment corrected — EBX and EDX are preserved by this
; routine; only EAX is used as the return channel.
pm__get_cursor:
push ebx
push edx
mov dx, VGA_CRTC_INDEX
mov al, VGA_CURSOR_HI
out dx, al
inc dx
in al, dx
mov bh, al
dec dx
mov al, VGA_CURSOR_LO
out dx, al
inc dx
in al, dx
mov bl, al
mov ax, bx
pop edx
pop ebx
ret

; --- pm__putc ---
; Writes AL to the VGA text buffer at the hardware cursor, advances the
; cursor, wraps at column 80, and scrolls when the last row is reached.
; LF (0x0A) moves to the start of the next row; CR (0x0D) is ignored.
; Preserves EAX, EBX, ECX, EDX, ESI, EDI.
pm__putc:
push eax
push ebx
push ecx
push edx
push esi
push edi

mov bl, al                  ; BL = char
call pm__get_cursor         ; AX = cursor offset
movzx esi, ax               ; ESI = cursor offset

cmp bl, 13                  ; CR — ignore
je .advance_cursor

cmp bl, 10                  ; LF — next row, column 0
jne .print_char

xor edx, edx
mov eax, esi
mov ecx, PM_VGA_COLS
div ecx                     ; EAX = row, EDX = column
inc eax
imul eax, PM_VGA_COLS
mov esi, eax
jmp .check_scroll

.print_char:
mov edi, PM_VGA_BASE
add edi, esi
add edi, esi                ; *2 bytes per cell
mov ah, PM_VGA_ATTR
mov al, bl
mov [edi], ax
inc esi

.check_scroll:
cmp esi, PM_VGA_SIZE
jb .set_cursor
call pm__scroll_up
sub esi, PM_VGA_COLS

.set_cursor:
mov ax, si
call pm__set_cursor

.advance_cursor:
pop edi
pop esi
pop edx
pop ecx
pop ebx
pop eax
ret

; --- pm__scroll_up ---
; Scrolls the VGA text buffer up by one row: rows 1..24 are copied to rows
; 0..23, and the bottom row is filled with blank cells using PM_VGA_ATTR.
; Does not modify the hardware cursor (the caller is responsible for
; repositioning it after a scroll).
; Preserves EAX, ECX, ESI, EDI.
;
; FIX (v4): this routine was referenced by pm__putc but missing from the
; truncated v3 file. Added.
pm__scroll_up:
push eax
push ecx
push esi
push edi

; Copy rows 1..(PM_VGA_ROWS-1) down to rows 0..(PM_VGA_ROWS-2).
; Word count = (PM_VGA_ROWS - 1) * PM_VGA_COLS.
mov esi, PM_VGA_BASE + (PM_VGA_COLS * 2)
mov edi, PM_VGA_BASE
mov ecx, (PM_VGA_ROWS - 1) * PM_VGA_COLS
cld
rep movsw

; Blank the last row: PM_VGA_COLS cells, each a space with PM_VGA_ATTR.
; After the copy, EDI points at the start of the last row.
mov ecx, PM_VGA_COLS
mov ah, PM_VGA_ATTR
mov al, ' '
rep stosw

pop edi
pop esi
pop ecx
pop eax
ret

; --- pm__hex_nibble ---
; 32-bit helper. Prints the low nibble of AL at the hardware cursor via
; pm__putc. Preserves all general-purpose registers (pm__putc does).
;
; FIX (v4): this routine was referenced by pm_print_hex_byte but missing
; from the truncated v3 file. Added.
pm__hex_nibble:
push eax
and al, 0x0F
cmp al, 10
jb .digit
add al, 'A' - 10
jmp .emit
.digit:
add al, '0'
.emit:
call pm__putc
pop eax
ret

%endif ; RM_PM_ROUTINES_EMITTED

%endif ; RM_INC
