; ============================================================================
; pm.inc — 32-bit Protected Mode / Long Mode Library (corrected v2)
; ============================================================================
; Assumes the caller is in 32-bit flat protected mode (e.g. entered via
; rm.inc's `loadpmode`) with A20 enabled.
;
; Provides VGA output, hex/decimal formatting, ATA PIO disk reads, identity
; paging, a long-mode transition, and (optionally) a parallel set of 64-bit
; output routines.
;
; Usage:
;   %include "pm.inc"
;
; 64-bit support is opt-in:
;   By default this header emits only 32-bit code and leaves the assembler
;   in `bits 32` mode at the end of the include. The 64-bit VGA primitives
;   and macros (Section 8 and Section 9: lm_print, lm_clear, lm_halt,
                ;   lm_print_hex_*, lm_print_dec) are only emitted when the user defines
                ;   PM_ENABLE_64BIT before including:
                ;
                ;       %define PM_ENABLE_64BIT
                ;       %include "pm.inc"
                ;
                ;   With or without the flag, the assembler is left in `bits 32` at the end
                ;   of the include. The `longmode` macro switches to `bits 64` internally
                ;   when it is expanded; code placed after a `longmode` invocation is
                ;   therefore assembled as 64-bit, as expected.
                ;
                ; Preconditions (documented, not enforced):
                ;   * 32-bit flat protected mode with CS = 32-bit code selector, DS = ES =
                ;     FS = GS = SS = flat 32-bit data selector.
                ;   * A20 enabled. `rm.inc`'s `loadpmode` does this; if you skip rm.inc,
;     you must enable A20 yourself before using anything here.
;   * PML4_ADDR, PDPT_ADDR, PD_ADDR point at 4 KB-aligned, freely usable
;     memory. They need NOT be contiguous (each is zeroed separately).
;     The defaults sit in the conventional first-64 KB area; if your
;     bootloader keeps data there, override them before including.
;
; Rules:
;   Macros are lowercase, constants are UPPERCASE.
; ============================================================================

%ifndef PM_INC
%define PM_INC

; ============================================================================
; SECTION 1: CONSTANTS
; ============================================================================

; VGA text mode
%define VGA_BASE        0xB8000
%define VGA_COLS        80
%define VGA_ROWS        25
%define VGA_SIZE        (VGA_COLS * VGA_ROWS)
%define VGA_ATTR        0x07
%define CRTC_INDEX      0x3D4
%define CRTC_DATA       0x3D5

; Paging bits
%define PTE_P           0x0001
%define PTE_RW          0x0002
%define PTE_US          0x0004
%define PTE_PS          0x0080
%define PTE_G           0x0100

; Control registers
%define CR0_PE          0x00000001
%define CR0_PG          0x80000000
%define CR4_PAE         0x00000020

; MSR / EFER
%define MSR_EFER        0xC0000080
%define EFER_LME        0x00000100
%define EFER_LMA        0x00000400

; Long mode GDT selectors (must match lm__gdt)
%define LM_CODE_SEL     0x08
%define LM_DATA_SEL     0x10

; ATA PIO (primary bus, master drive)
%define ATA_DATA        0x1F0
%define ATA_ERR         0x1F1
%define ATA_SECCNT      0x1F2
%define ATA_LBA_LO      0x1F3
%define ATA_LBA_MID     0x1F4
%define ATA_LBA_HI      0x1F5
%define ATA_DRIVE       0x1F6
%define ATA_STATUS      0x1F7
%define ATA_CMD         0x1F7
%define ATA_ALT_STATUS  0x3F6
%define ATA_READ_CMD    0x20

; ATA status bits
%define ATA_SR_BSY      0x80
%define ATA_SR_DRDY     0x40
%define ATA_SR_DF       0x20
%define ATA_SR_DRQ      0x08
%define ATA_SR_ERR      0x01

; ATA polling timeout (iterations of the poll loop)
%ifndef ATA_TIMEOUT
%define ATA_TIMEOUT     0x100000
%endif

; ============================================================================
; SECTION 2: CONFIGURATION
; ============================================================================

; Page table physical addresses (each 4 KB, 4 KB-aligned, not necessarily
; contiguous — each is zeroed separately by pm__setup_paging).
%ifndef PML4_ADDR
%define PML4_ADDR       0x1000
%endif
%ifndef PDPT_ADDR
%define PDPT_ADDR       0x2000
%endif
%ifndef PD_ADDR
%define PD_ADDR         0x3000
%endif

; Number of 2 MB pages to identity-map at boot (1 GB by default).
; Must be <= 512: a single 4 KB PD holds 512 two-MiB entries.
%ifndef PM_IDENTITY_PAGES
%define PM_IDENTITY_PAGES 512
%endif

; Stacks
%ifndef PM_STACK_TOP
%define PM_STACK_TOP    0x90000
%endif
%ifndef LM_STACK_TOP
%define LM_STACK_TOP    0x90000
%endif

; Kernel load defaults
%ifndef KERNEL_LBA
%define KERNEL_LBA      1
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS  128
%endif
%ifndef KERNEL_LOAD_ADDR
%define KERNEL_LOAD_ADDR 0x100000
%endif

; ============================================================================
; SECTION 3: 32-BIT VGA PRIMITIVES
; ============================================================================

bits 32

pm__set_cursor:
push eax
push ebx
push edx
mov bx, ax
mov dx, CRTC_INDEX
mov al, 0x0E
out dx, al
inc dx
mov al, bh
out dx, al
dec dx
mov al, 0x0F
out dx, al
inc dx
mov al, bl
out dx, al
pop edx
pop ebx
pop eax
ret

pm__get_cursor:
push ebx
push edx
mov dx, CRTC_INDEX
mov al, 0x0E
out dx, al
inc dx
in al, dx
mov bh, al
dec dx
mov al, 0x0F
out dx, al
inc dx
in al, dx
mov bl, al
mov ax, bx
pop edx
pop ebx
ret

pm__scroll:
push eax
push ecx
push esi
push edi
mov esi, VGA_BASE + (VGA_COLS * 2)
mov edi, VGA_BASE
mov ecx, (VGA_ROWS - 1) * VGA_COLS
cld
rep movsw
mov ecx, VGA_COLS
mov ah, VGA_ATTR
mov al, ' '
rep stosw
pop edi
pop esi
pop ecx
pop eax
ret

pm__putc:
push eax
push ebx
push ecx
push edx
push esi
push edi
mov bl, al
call pm__get_cursor
movzx esi, ax
cmp bl, 13
je .done
cmp bl, 10
jne .print
xor edx, edx
mov eax, esi
mov ecx, VGA_COLS
div ecx
inc eax
imul eax, VGA_COLS
mov esi, eax
jmp .check
.print:
mov edi, VGA_BASE
add edi, esi
add edi, esi
mov ah, VGA_ATTR
mov al, bl
mov [edi], ax
inc esi
.check:
cmp esi, VGA_SIZE
jb .set
call pm__scroll
sub esi, VGA_COLS
.set:
mov ax, si
call pm__set_cursor
.done:
pop edi
pop esi
pop edx
pop ecx
pop ebx
pop eax
ret

pm__hex_nibble:
push eax
and al, 0x0F
cmp al, 10
jb .d
add al, 'A' - 10
jmp .e
.d:
add al, '0'
.e:
call pm__putc
pop eax
ret

; ============================================================================
; SECTION 4: 32-BIT MACROS
; ============================================================================

%macro pm_halt 0
%%l:
cli
hlt
jmp %%l
%endmacro

%macro pm_clear 1
push eax
push ecx
push edi
mov edi, VGA_BASE
mov ecx, VGA_SIZE
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

%macro pm_print 1
push esi
jmp %%s
%%m: db %1, 0
%%s:
mov esi, %%m
%%l:
lodsb
or al, al
jz %%d
call pm__putc
jmp %%l
%%d:
pop esi
%endmacro

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

%macro pm_print_hex_word 0
push eax
mov al, ah
pm_print_hex_byte
pop eax
pm_print_hex_byte
%endmacro

%macro pm_print_hex_dword 0
push eax
shr eax, 16
pm_print_hex_word
pop eax
pm_print_hex_word
%endmacro

%macro pm_print_dec 0
push eax
push ebx
push ecx
push edx
mov ebx, 10
xor ecx, ecx
%%d:
xor edx, edx
div ebx
push edx
inc ecx
test eax, eax
jnz %%d
%%p:
pop eax
add al, '0'
call pm__putc
loop %%p
pop edx
pop ecx
pop ebx
pop eax
%endmacro

; ============================================================================
; SECTION 5: ATA PIO
; ============================================================================

; ata_read: EAX = LBA (28-bit), ECX = sector count, EDI = destination.
;
; Preconditions:
;   * ES must be a flat 32-bit data selector covering the destination range.
;     This routine uses `rep insw`, which writes to ES:EDI.
;   * ECX > 0.
;
; Preserves all general-purpose registers (uses pushad/popad).
; Returns: CF=0 on success, CF=1 on error (ERR or DF set, or timeout).
; On error the destination buffer may contain partial garbage; the caller
; must not use it.
ata_read:
pushad
mov esi, ecx
mov ebx, eax
.loop:
; Wait for BSY to clear before issuing the next command.
mov dx, ATA_STATUS
mov ebp, ATA_TIMEOUT
.w1:
in al, dx
test al, ATA_SR_BSY
jz .w1_ok
dec ebp
jnz .w1
jmp .error
.w1_ok:
; Select drive/LBA[27:24]. Master + LBA mode + bits 24..27.
mov dx, ATA_DRIVE
mov eax, ebx
shr eax, 24
and al, 0x0F
or al, 0xE0
out dx, al

; ATA-mandated 400 ns delay after drive select: four reads of the
; alternate status port. Reads have no side effects on the drive.
mov dx, ATA_ALT_STATUS
in al, dx
in al, dx
in al, dx
in al, dx

; Sector count = 1
mov dx, ATA_SECCNT
mov al, 1
out dx, al
; LBA[7:0]
mov dx, ATA_LBA_LO
mov eax, ebx
out dx, al
; LBA[15:8]
shr eax, 8
mov dx, ATA_LBA_MID
out dx, al
; LBA[23:16]
shr eax, 8
mov dx, ATA_LBA_HI
out dx, al
; Issue READ SECTORS
mov dx, ATA_CMD
mov al, ATA_READ_CMD
out dx, al
; Wait for !BSY && (ERR || DF || DRQ). Bounded.
mov dx, ATA_STATUS
mov ebp, ATA_TIMEOUT
.w2:
in al, dx
test al, ATA_SR_BSY
jnz .w2_cont
test al, ATA_SR_ERR
jnz .error
test al, ATA_SR_DF
jnz .error
test al, ATA_SR_DRQ
jnz .ready
.w2_cont:
dec ebp
jnz .w2
jmp .error
.ready:
; Transfer one sector (256 words) into ES:EDI.
mov dx, ATA_DATA
mov ecx, 256
rep insw
inc ebx
dec esi
jnz .loop
popad
clc
ret
.error:
popad
stc
ret

; load_kernel: read KERNEL_SECTORS sectors starting at KERNEL_LBA into
; KERNEL_LOAD_ADDR. CF=1 on error.
%macro load_kernel 0
mov eax, KERNEL_LBA
mov ecx, KERNEL_SECTORS
mov edi, KERNEL_LOAD_ADDR
call ata_read
%endmacro

; ============================================================================
; SECTION 6: PAGING
; ============================================================================

%if PM_IDENTITY_PAGES > 512
%error "PM_IDENTITY_PAGES exceeds PD capacity (max 512 two-MiB pages)"
%endif

; pm__setup_paging: zero PML4/PDPT/PD and identity-map PM_IDENTITY_PAGES
; 2 MB pages starting at physical 0.
;
; Each table is zeroed independently, so the three addresses need not be
; contiguous. All three must be 4 KB-aligned.
;
; Preserves EAX, ECX, EDI.
pm__setup_paging:
push eax
push ecx
push edi
xor eax, eax
cld

; Zero PML4
mov edi, PML4_ADDR
mov ecx, 4096 / 4
rep stosd
; Zero PDPT
mov edi, PDPT_ADDR
mov ecx, 4096 / 4
rep stosd
; Zero PD
mov edi, PD_ADDR
mov ecx, 4096 / 4
rep stosd

; PML4[0] -> PDPT (P|RW)
mov edi, PML4_ADDR
mov eax, PDPT_ADDR | PTE_P | PTE_RW
mov [edi], eax

; PDPT[0] -> PD (P|RW)
mov edi, PDPT_ADDR
mov eax, PD_ADDR | PTE_P | PTE_RW
mov [edi], eax

; PD[i] = 2 MiB page at i*0x200000, P|RW|PS
mov edi, PD_ADDR
mov eax, PTE_P | PTE_RW | PTE_PS
mov ecx, PM_IDENTITY_PAGES
.fill:
mov [edi], eax
add edi, 8
add eax, 0x200000
loop .fill

pop edi
pop ecx
pop eax
ret

; ============================================================================
; SECTION 7: LONG MODE TRANSITION
; ============================================================================

; longmode entry:
;   Sets up PAE paging, enables LME + PG, far-jumps to a 64-bit code
;   segment, and jumps to `entry`. Never returns.
;
;   Preconditions (not enforced):
;     * A20 enabled.
;     * CPU supports long mode (CPUID.80000001H:EDX.LM[bit 29]).
;     * Paging tables at PML4_ADDR/PDPT_ADDR/PD_ADDR are free.
;
;   Effects on caller-visible state:
;     * Interrupts are disabled (cli) and never re-enabled.
;     * EFLAGS is undefined on entry to `entry`.
;     * All GPRs except RSP/RBP are undefined on entry to `entry`.
;     * The assembler is left in `bits 64` mode after the macro expands.
;       This is intentional: the stub and `entry` are 64-bit code. Do not
;       place 32-bit code after a `longmode` invocation.
;
;   To use the 64-bit output routines (lm_print, lm_clear, etc.) after the
;   transition, define PM_ENABLE_64BIT before including this file.
;
;   The built-in GDT is emitted by this macro and guarded so that multiple
;   `longmode` invocations within one translation unit do not cause
;   duplicate-symbol errors.
%macro longmode 1
cli
call pm__setup_paging

mov eax, cr4
or eax, CR4_PAE
mov cr4, eax

mov eax, PML4_ADDR
mov cr3, eax

mov ecx, MSR_EFER
rdmsr
or eax, EFER_LME
wrmsr

lgdt [lm__gdtr]

mov eax, cr0
or eax, CR0_PG
mov cr0, eax

jmp LM_CODE_SEL:%%stub

bits 64
%%stub:
mov ax, LM_DATA_SEL
mov ds, ax
mov es, ax
mov fs, ax
mov gs, ax
mov ss, ax
mov rsp, LM_STACK_TOP
mov rbp, rsp
jmp %1

%ifndef LM_GDT_EMITTED
%define LM_GDT_EMITTED
align 8, db 0
lm__gdt:
dq 0
; 0x08 — 64-bit code, ring 0, L=1, D=0
dw 0xFFFF, 0x0000
db 0x00, 0x9A, 0xAF, 0x00
; 0x10 — 32/64-bit data, ring 0
dw 0xFFFF, 0x0000
db 0x00, 0x92, 0xCF, 0x00
lm__gdt_end:
lm__gdtr:
dw lm__gdt_end - lm__gdt - 1
dq lm__gdt
%endif
%endmacro

; ============================================================================
; SECTION 8: 64-BIT VGA PRIMITIVES (opt-in: define PM_ENABLE_64BIT)
; ============================================================================

%ifdef PM_ENABLE_64BIT

bits 64

lm__set_cursor:
push rax
push rbx
push rdx
mov bx, ax
mov dx, CRTC_INDEX
mov al, 0x0E
out dx, al
inc dx
mov al, bh
out dx, al
dec dx
mov al, 0x0F
out dx, al
inc dx
mov al, bl
out dx, al
pop rdx
pop rbx
pop rax
ret

lm__get_cursor:
push rbx
push rdx
mov dx, CRTC_INDEX
mov al, 0x0E
out dx, al
inc dx
in al, dx
mov bh, al
dec dx
mov al, 0x0F
out dx, al
inc dx
in al, dx
mov bl, al
mov ax, bx
pop rdx
pop rbx
ret

lm__scroll:
push rax
push rcx
push rsi
push rdi
mov rsi, VGA_BASE + (VGA_COLS * 2)
mov rdi, VGA_BASE
mov rcx, (VGA_ROWS - 1) * VGA_COLS
cld
rep movsw
mov rcx, VGA_COLS
mov ah, VGA_ATTR
mov al, ' '
rep stosw
pop rdi
pop rsi
pop rcx
pop rax
ret

lm__putc:
push rax
push rbx
push rcx
push rdx
push rsi
push rdi
mov bl, al
call lm__get_cursor
movzx esi, ax
cmp bl, 13
je .done
cmp bl, 10
jne .print
xor edx, edx
mov eax, esi
mov ecx, VGA_COLS
div ecx
inc eax
imul eax, VGA_COLS
mov esi, eax
jmp .check
.print:
mov edi, VGA_BASE
add edi, esi
add edi, esi
mov ah, VGA_ATTR
mov al, bl
mov [edi], ax
inc esi
.check:
cmp esi, VGA_SIZE
jb .set
call lm__scroll
sub esi, VGA_COLS
.set:
mov ax, si
call lm__set_cursor
.done:
pop rdi
pop rsi
pop rdx
pop rcx
pop rbx
pop rax
ret

lm__hex_nibble:
push rax
and al, 0x0F
cmp al, 10
jb .d
add al, 'A' - 10
jmp .e
.d:
add al, '0'
.e:
call lm__putc
pop rax
ret

; ============================================================================
; SECTION 9: 64-BIT MACROS (opt-in: define PM_ENABLE_64BIT)
; ============================================================================

%macro lm_halt 0
%%l:
cli
hlt
jmp %%l
%endmacro

%macro lm_clear 1
push rax
push rcx
push rdi
mov rdi, VGA_BASE
mov rcx, VGA_SIZE
mov ah, %1
mov al, ' '
cld
rep stosw
xor eax, eax
call lm__set_cursor
pop rdi
pop rcx
pop rax
%endmacro

%macro lm_print 1
push rsi
jmp %%s
%%m: db %1, 0
%%s:
mov rsi, %%m
%%l:
lodsb
or al, al
jz %%d
call lm__putc
jmp %%l
%%d:
pop rsi
%endmacro

%macro lm_print_hex_byte 0
push rax
push rbx
mov bl, al
shr al, 4
call lm__hex_nibble
mov al, bl
call lm__hex_nibble
pop rbx
pop rax
%endmacro

%macro lm_print_hex_word 0
push rax
mov al, ah
lm_print_hex_byte
pop rax
lm_print_hex_byte
%endmacro

%macro lm_print_hex_dword 0
push rax
shr eax, 16
lm_print_hex_word
pop rax
lm_print_hex_word
%endmacro

%macro lm_print_hex_qword 0
push rax
shr rax, 32
lm_print_hex_dword
pop rax
lm_print_hex_dword
%endmacro

%macro lm_print_dec 0
push rax
push rbx
push rcx
push rdx
mov ebx, 10
xor ecx, ecx
%%d:
xor edx, edx
div ebx
push rdx
inc ecx
test eax, eax
jnz %%d
%%p:
pop rax
add al, '0'
call lm__putc
loop %%p
pop rdx
pop rcx
pop rbx
pop rax
%endmacro

%endif ; PM_ENABLE_64BIT

; ============================================================================
; Restore the ambient assembler mode so the caller's code is 32-bit by
; default, regardless of whether the 64-bit sections above were emitted.
; Callers who want to write 64-bit code should do so after a `longmode`
; invocation (which switches to bits 64 internally) or issue `bits 64`
; explicitly.
; ============================================================================

bits 32

%endif ; PM_INC
