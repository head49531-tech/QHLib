; ============================================================================
; lm.inc — Long Mode Library (corrected)
; ============================================================================
; Assumes the caller is in 64-bit long mode with a flat memory model,
; entered via pm.inc's `longmode` macro (or equivalent). The address space
; is expected to be identity-mapped for at least the first 1 GB.
;
; Usage:
;   %include "lm.inc"
;
; Public entry points (SysV AMD64: RDI, RSI, RDX, RCX, R8, R9 for args;
; RAX for return). Deviations are noted per routine.
;
;   lm_puts(rdi=str)                write null-terminated string to VGA
;   lm_putc(dil=char)               write one character to VGA
;   lm_clear(al=attr)               clear the screen
;   lm_serial_init()                initialize COM1
;   lm_serial_putc(dil=char)        write to serial
;   lm_serial_puts(rdi=str)         write string to serial
;   lm_serial_hex_byte(al=b)        write byte as two hex digits to serial
;   lm_memcpy(rdi=dst,rsi=src,rdx=n)
;   lm_memset(rdi=dst,esi=byte,rdx=n)
;   lm_memmove(rdi=dst,rsi=src,rdx=n)
;   lm_memcmp(rdi=a,rsi=b,rdx=n) -> rax = 0 / -1 / 1
;   lm_strlen(rdi=str) -> rax
;   lm_strcmp(rdi=a,rsi=b) -> rax = 0 / -1 / 1
;   lm_strcpy(rdi=dst,rsi=src) -> rax = dst
;   lm_strncpy(rdi=dst,rsi=src,rdx=n) -> rax = dst
;   lm_alloc(rdi=size) -> rax       bump allocator, 16-byte aligned, not zeroed
;   lm_alloc_page() -> rax          4 KB-aligned page, not zeroed
;   lm_map_4k(rdi=va,rsi=pa,rdx=flags) -> rax = 0 / -1
;   lm_unmap_4k(rdi=va)
;   lm_invlpg(rdi=va)
;   lm_paging_init(rdi=count)       rebuild identity map (count = 2 MB pages,
                                                          ;                                   0 means 512)
;   lm_idt_init()                   install exceptions + IRQs, load IDT
;   lm_gdt_init()                   install GDT + TSS, load them
;   lm_pic_remap(rdi=master, rsi=slave)
;   lm_pic_mask(rdi=master, rsi=slave)
;   lm_pic_eoi(rdi=irq)
;   lm_ata_read(rdi=lba,rsi=count,rdx=dst) -> rax = 0 / -1
;   lm_rdmsr(rdi=msr) -> rdx:rax
;   lm_wrmsr(rdi=msr,rsi=value)
;   lm_cpuid(rdi=leaf) -> rax,rbx,rcx,rdx   (clobbers RBX; see note)
;   lm_read_cr2/cr3 -> rax
;   lm_write_cr3(rdi=value)
;
;   Port I/O, SysV-ordered (port first, then value):
;   lm_outb(rdi=port,rsi=value)
;   lm_outw(rdi=port,rsi=value)
;   lm_outl(rdi=port,rsi=value)
;   lm_inb(rdi=port) -> al
;   lm_inw(rdi=port) -> ax
;   lm_inl(rdi=port) -> eax
;
; Macros:
;   lm_print "str"                  print literal
;   lm_print_nl                     print CRLF
;   lm_print_hex_byte               print AL
;   lm_print_hex_word               print AX
;   lm_print_hex_dword              print EAX
;   lm_print_hex_qword              print RAX
;   lm_print_dec                    print RAX as unsigned decimal
;   lm_halt                         disable interrupts, halt forever
;   lm_load_kernel                  load KERNEL_SECTORS sectors from
;                                   KERNEL_LBA to KERNEL_LOAD_ADDR
;
; Exceptions and IRQs default to printing the vector number, echoing it
; to serial, and halting. Override by installing your own gate with
; lm_idt_set_gate.
;
; Rules:
;   Macros are lowercase, constants are UPPERCASE, routine names are
;   lowercase with an `lm_` prefix.
; ============================================================================

%ifndef LM_INC
%define LM_INC

; ============================================================================
; SECTION 1: CONSTANTS
; ============================================================================

%define VGA_BASE        0xB8000
%define VGA_COLS        80
%define VGA_ROWS        25
%define VGA_SIZE        (VGA_COLS * VGA_ROWS)
%define VGA_ATTR        0x07
%define CRTC_INDEX      0x3D4

%define COM1            0x3F8

%define PTE_P           0x001
%define PTE_RW          0x002
%define PTE_US          0x004
%define PTE_PWT         0x008
%define PTE_PCD         0x010
%define PTE_A           0x020
%define PTE_D           0x040
%define PTE_PS          0x080
%define PTE_G           0x100
%define PTE_NX          (1 << 63)

%define CR0_MP          (1 << 1)
%define CR0_EM          (1 << 2)
%define CR0_TS          (1 << 3)
%define CR0_NE          (1 << 5)
%define CR0_WP          (1 << 16)
%define CR0_AM          (1 << 18)
%define CR0_PG          (1 << 31)

%define CR4_PAE         (1 << 5)
%define CR4_PGE         (1 << 7)
%define CR4_OSFXSR      (1 << 9)
%define CR4_OSXMMEXCPT  (1 << 10)

%define MSR_EFER        0xC0000080
%define MSR_STAR        0xC0000081
%define MSR_LSTAR       0xC0000082
%define MSR_FMASK       0xC0000084
%define MSR_FS_BASE     0xC0000100
%define MSR_GS_BASE     0xC0000101
%define MSR_KERNEL_GS   0xC0000102

%define EFER_SCE        (1 << 0)
%define EFER_LME        (1 << 8)
%define EFER_LMA        (1 << 10)
%define EFER_NXE        (1 << 11)

%define GDT_KCODE       0x08
%define GDT_KDATA       0x10
%define GDT_UDATA       0x18
%define GDT_UCODE       0x20
%define GDT_TSS         0x28

%define IDT_PRESENT     0x80
%define IDT_DPL0        0x00
%define IDT_DPL3        0x60
%define IDT_INT_GATE    0x0E
%define IDT_TRAP_GATE   0x0F

%define PIC1_CMD        0x20
%define PIC1_DATA       0x21
%define PIC2_CMD        0xA0
%define PIC2_DATA       0xA1
%define PIC_EOI         0x20
%define ICW1_INIT       0x10
%define ICW1_ICW4       0x01
%define ICW4_8086       0x01
%define PIC_DISABLE     0xFF

%define ATA_DATA        0x1F0
%define ATA_ERR         0x1F1
%define ATA_FEATURES    0x1F1
%define ATA_SECCNT      0x1F2
%define ATA_LBA_LO      0x1F3
%define ATA_LBA_MID     0x1F4
%define ATA_LBA_HI      0x1F5
%define ATA_DRIVE       0x1F6
%define ATA_STATUS      0x1F7
%define ATA_CMD         0x1F7
%define ATA_ALT_STATUS  0x3F6
%define ATA_READ_EXT    0x24

%define ATA_SR_BSY      0x80
%define ATA_SR_DRDY     0x40
%define ATA_SR_DF       0x20
%define ATA_SR_DRQ      0x08
%define ATA_SR_ERR      0x01

%ifndef ATA_TIMEOUT
%define ATA_TIMEOUT     0x1000000
%endif

; ============================================================================
; SECTION 2: CONFIGURATION
; ============================================================================

%ifndef LM_HEAP_START
%define LM_HEAP_START   0x100000
%endif
%ifndef LM_HEAP_END
%define LM_HEAP_END     0x400000
%endif

%ifndef LM_IST_TOP
%define LM_IST_TOP      0x7F00
%endif
%ifndef LM_IST_SIZE
%define LM_IST_SIZE     0x1000
%endif

%ifndef KERNEL_LBA
%define KERNEL_LBA      1
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS  128
%endif
%ifndef KERNEL_LOAD_ADDR
%define KERNEL_LOAD_ADDR 0x100000
%endif

%ifndef PML4_ADDR
%define PML4_ADDR       0x1000
%endif
%ifndef PDPT_ADDR
%define PDPT_ADDR       0x2000
%endif
%ifndef PD_ADDR
%define PD_ADDR         0x3000
%endif

; ============================================================================
; SECTION 3: PORT I/O  (SysV: RDI = port, RSI = value)
; ============================================================================

bits 64

lm_outb:
; rdi = port, rsi = value
mov dx, di
mov al, sil
out dx, al
ret

lm_outw:
; rdi = port, rsi = value
mov dx, di
mov ax, si
out dx, ax
ret

lm_outl:
; rdi = port, rsi = value
mov dx, di
mov eax, esi
out dx, eax
ret

lm_inb:
; rdi = port -> al
mov dx, di
in al, dx
ret

lm_inw:
; rdi = port -> ax
mov dx, di
in ax, dx
ret

lm_inl:
; rdi = port -> eax
mov dx, di
in eax, dx
ret

; ============================================================================
; SECTION 4: SERIAL (COM1)
; ============================================================================

lm_serial_init:
push rax
push rdx
mov dx, COM1 + 1
mov al, 0x00
out dx, al
mov dx, COM1 + 3
mov al, 0x80
out dx, al
mov dx, COM1 + 0
mov al, 0x03
out dx, al
mov dx, COM1 + 1
mov al, 0x00
out dx, al
mov dx, COM1 + 3
mov al, 0x03
out dx, al
mov dx, COM1 + 2
mov al, 0xC7
out dx, al
mov dx, COM1 + 4
mov al, 0x0B
out dx, al
pop rdx
pop rax
ret

lm_serial_putc:
; dil = char
push rax
push rdx
mov al, dil
mov dx, COM1
out dx, al
pop rdx
pop rax
ret

lm_serial_puts:
; rdi = string
; FIX: copy the pointer into RSI (lodsb reads from RSI) and save RSI.
push rdi
push rsi
mov rsi, rdi
.loop:
lodsb
test al, al
jz .done
mov dil, al
call lm_serial_putc
jmp .loop
.done:
pop rsi
pop rdi
ret

; FIX: added so the exception handler can echo a byte to serial.
lm_serial_hex_byte:
; al = byte
push rax
push rbx
mov bl, al
shr al, 4
call lm__serial_hex_nibble
mov al, bl
call lm__serial_hex_nibble
pop rbx
pop rax
ret

lm__serial_hex_nibble:
; al = nibble
and al, 0x0F
cmp al, 10
jb .digit
add al, 'A' - 10
jmp .emit
.digit:
add al, '0'
.emit:
mov dil, al
call lm_serial_putc
ret

; ============================================================================
; SECTION 5: VGA TEXT OUTPUT
; ============================================================================

lm_set_cursor:
; di = text offset (0..1999)
push rax
push rbx
push rdx
mov bx, di
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

lm_get_cursor:
; -> ax = text offset
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

lm_scroll:
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

lm_putc:
; dil = char
push rax
push rbx
push rcx
push rdx
push rsi
push rdi
mov bl, dil
call lm_get_cursor
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
add rdi, rsi
add rdi, rsi
mov ah, VGA_ATTR
mov al, bl
mov [rdi], ax
inc esi
.check:
cmp esi, VGA_SIZE
jb .set
call lm_scroll
sub esi, VGA_COLS
.set:
mov di, si
call lm_set_cursor
.done:
pop rdi
pop rsi
pop rdx
pop rcx
pop rbx
pop rax
ret

lm_puts:
; rdi = string
; FIX: copy the pointer into RSI (lodsb reads from RSI) and save RSI.
push rdi
push rsi
mov rsi, rdi
.loop:
lodsb
test al, al
jz .done
mov dil, al
call lm_putc
jmp .loop
.done:
pop rsi
pop rdi
ret

lm_clear:
; al = attribute
push rax
push rcx
push rdi
mov ah, al
mov al, ' '
mov rdi, VGA_BASE
mov rcx, VGA_SIZE
cld
rep stosw
xor edi, edi
call lm_set_cursor
pop rdi
pop rcx
pop rax
ret

lm__hex_nibble:
push rax
and al, 0x0F
cmp al, 10
jb .digit
add al, 'A' - 10
jmp .emit
.digit:
add al, '0'
.emit:
mov dil, al
call lm_putc
pop rax
ret

; ============================================================================
; SECTION 6: MACROS
; ============================================================================

%macro lm_halt 0
%%l:
cli
hlt
jmp %%l
%endmacro

%macro lm_print 1
push rdi
jmp %%skip
%%msg: db %1, 0
%%skip:
lea rdi, [rel %%msg]
call lm_puts
pop rdi
%endmacro

%macro lm_print_nl 0
push rdi
mov dil, 13
call lm_putc
mov dil, 10
call lm_putc
pop rdi
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
push rdi
mov ebx, 10
xor ecx, ecx
%%div:
xor edx, edx
div rbx
push rdx
inc ecx
test rax, rax
jnz %%div
%%print:
pop rax
add al, '0'
mov dil, al
push rcx
call lm_putc
pop rcx
loop %%print
pop rdi
pop rdx
pop rcx
pop rbx
pop rax
%endmacro

; ============================================================================
; SECTION 7: MEMORY AND STRING
; ============================================================================

lm_memcpy:
; rdi = dst, rsi = src, rdx = n
push rdi
mov rcx, rdx
cld
rep movsb
pop rax
ret

lm_memset:
; rdi = dst, esi = byte, rdx = n
push rdi
mov rcx, rdx
mov al, sil
cld
rep stosb
pop rax
ret

lm_memmove:
; rdi = dst, rsi = src, rdx = n
cmp rdi, rsi
je .done
jb .forward
mov rcx, rdx
lea rdi, [rdi + rdx - 1]
lea rsi, [rsi + rdx - 1]
std
rep movsb
cld
ret
.forward:
mov rcx, rdx
cld
rep movsb
.done:
ret

lm_memcmp:
; rdi = a, rsi = b, rdx = n -> rax = 0 if equal, -1 if a<b, 1 if a>b
; FIX: handle n = 0 (repe cmpsb with rcx=0 leaves flags stale).
mov rcx, rdx
test rcx, rcx
jz .equal
cld
repe cmpsb
je .equal
mov rax, 1
jnc .ret
mov rax, -1
.ret:
ret
.equal:
xor eax, eax
ret

lm_strlen:
; rdi = str -> rax = length
xor eax, eax
test rdi, rdi
jz .done
.loop:
cmp byte [rdi + rax], 0
je .done
inc rax
jmp .loop
.done:
ret

lm_strcmp:
; rdi = a, rsi = b -> rax = 0 if equal, -1 if a<b, 1 if a>b
.loop:
movzx eax, byte [rdi]
movzx ecx, byte [rsi]
sub eax, ecx
jnz .ret
test cl, cl
jz .ret
inc rdi
inc rsi
jmp .loop
.ret:
movsx rax, ax
ret

lm_strcpy:
; rdi = dst, rsi = src -> rax = dst
mov rax, rdi
.loop:
lodsb
stosb
test al, al
jnz .loop
ret

lm_strncpy:
; rdi = dst, rsi = src, rdx = n -> rax = dst
; FIX: the previous version wrote one extra NUL when src terminated
;      early. This version writes exactly n bytes when src is shorter,
;      or exactly n bytes when src is longer (no terminator).
mov rax, rdi
mov rcx, rdx
test rcx, rcx
jz .done
.loop:
lodsb
stosb
test al, al
jz .pad
dec rcx
jnz .loop
jmp .done
.pad:
; We just copied a NUL. Fill the rest of the field.
dec rcx
jz .done
xor al, al
.pad_loop:
stosb
dec rcx
jnz .pad_loop
.done:
ret

; ============================================================================
; SECTION 8: BUMP ALLOCATOR
; ============================================================================

; lm_alloc(rdi = size) -> rax = pointer or 0. 16-byte aligned, not zeroed.
lm_alloc:
push rbx
mov rbx, [rel lm__heap_ptr]
test rbx, rbx
jnz .ready
mov rbx, LM_HEAP_START
.ready:
add rbx, 15
and rbx, ~15
mov rax, rbx
add rbx, rdi
cmp rbx, LM_HEAP_END
ja .fail
mov [rel lm__heap_ptr], rbx
pop rbx
ret
.fail:
xor eax, eax
pop rbx
ret

; lm_alloc_page() -> rax = 4 KB-aligned page or 0.
; FIX: round the heap pointer up to a 4 KB boundary before allocating,
;      rather than retrying lm_alloc (which only guarantees 16-byte
;      alignment).
lm_alloc_page:
push rbx
mov rbx, [rel lm__heap_ptr]
test rbx, rbx
jnz .have_base
mov rbx, LM_HEAP_START
.have_base:
add rbx, 0xFFF
and rbx, ~0xFFF
mov rax, rbx
add rbx, 0x1000
cmp rbx, LM_HEAP_END
ja .fail
mov [rel lm__heap_ptr], rbx
pop rbx
ret
.fail:
xor eax, eax
pop rbx
ret

; ============================================================================
; SECTION 9: CPU HELPERS
; ============================================================================

lm_rdmsr:
; rdi = MSR -> rdx:rax
push rcx
mov ecx, edi
rdmsr
pop rcx
ret

lm_wrmsr:
; rdi = MSR, rsi = value
push rcx
push rax
mov ecx, edi
mov rax, rsi
mov rdx, rsi
shr rdx, 32
wrmsr
pop rax
pop rcx
ret

lm_cpuid:
; rdi = leaf -> rax, rbx, rcx, rdx
; FIX: the previous version pushed/poped RBX, destroying the CPUID
;      result. RBX is now clobbered; callers must save it themselves.
;      This is a documented deviation from SysV AMD64.
mov eax, edi
xor ecx, ecx
cpuid
ret

lm_read_cr2:
mov rax, cr2
ret

lm_read_cr3:
mov rax, cr3
ret

lm_write_cr3:
mov cr3, rdi
ret

lm_invlpg:
invlpg [rdi]
ret

; ============================================================================
; SECTION 10: PAGING
; ============================================================================

; lm_paging_init(rdi = count of 2 MB pages to identity-map; 0 means 512):
; Build a fresh PML4 at PML4_ADDR with an identity map of the first
; `count` 2 MB pages, and install it. Requires that PML4_ADDR..PD_ADDR+4096
; is free, writable, and 4 KB-aligned.
;
; FIX: the previous version read the count from RSI instead of RDI, and
;      could overflow the PD if count > 512.
lm_paging_init:
push rax
push rcx
push rdi
push rsi
mov rsi, rdi            ; stash the count (RDI will be reused below)

; Zero PML4, PDPT, PD
mov rdi, PML4_ADDR
mov rcx, 512
xor eax, eax
cld
rep stosq
mov rdi, PDPT_ADDR
mov rcx, 512
rep stosq
mov rdi, PD_ADDR
mov rcx, 512
rep stosq

; PML4[0] -> PDPT
mov rdi, PML4_ADDR
mov rax, PDPT_ADDR | PTE_P | PTE_RW
mov [rdi], rax
; PDPT[0] -> PD
mov rdi, PDPT_ADDR
mov rax, PD_ADDR | PTE_P | PTE_RW
mov [rdi], rax

; PD[i] = 2 MB page; count defaults to 512, clamped to 512.
mov rdi, PD_ADDR
mov rax, PTE_P | PTE_RW | PTE_PS
mov rcx, rsi
test rcx, rcx
jnz .have_count
mov rcx, 512
.have_count:
cmp rcx, 512
jbe .fill
mov rcx, 512
.fill:
mov [rdi], rax
add rdi, 8
add rax, 0x200000
loop .fill

mov rax, PML4_ADDR
mov cr3, rax
pop rsi
pop rdi
pop rcx
pop rax
ret

; lm_map_4k(rdi = va, rsi = pa, rdx = flags) -> rax = 0 ok, -1 error.
lm_map_4k:
push rbx
push r12
push r13
push r14
push r15
mov r12, rdi
mov r13, rsi
mov r14, rdx

mov r15, cr3
and r15, ~0xFFF

mov rbx, r12
shr rbx, 39
and rbx, 0x1FF
lea rbx, [r15 + rbx*8]
mov rax, [rbx]
test al, PTE_P
jnz .have_pdpt
call lm_alloc_page
test rax, rax
jz .fail
or rax, PTE_P | PTE_RW
mov [rbx], rax
.have_pdpt:
mov r15, [rbx]
and r15, ~0xFFF

mov rbx, r12
shr rbx, 30
and rbx, 0x1FF
lea rbx, [r15 + rbx*8]
mov rax, [rbx]
test al, PTE_P
jnz .have_pd
call lm_alloc_page
test rax, rax
jz .fail
or rax, PTE_P | PTE_RW
mov [rbx], rax
.have_pd:
mov r15, [rbx]
and r15, ~0xFFF
test qword [rbx], PTE_PS
jz .not_2mb
jmp .fail
.not_2mb:

mov rbx, r12
shr rbx, 21
and rbx, 0x1FF
lea rbx, [r15 + rbx*8]
mov rax, [rbx]
test al, PTE_P
jnz .have_pt
call lm_alloc_page
test rax, rax
jz .fail
or rax, PTE_P | PTE_RW
mov [rbx], rax
.have_pt:
mov r15, [rbx]
and r15, ~0xFFF

mov rbx, r12
shr rbx, 12
and rbx, 0x1FF
and r13, ~0xFFF
mov rax, r14
or rax, PTE_P
or rax, r13
mov [r15 + rbx*8], rax

invlpg [r12]
xor eax, eax
jmp .done
.fail:
mov rax, -1
.done:
pop r15
pop r14
pop r13
pop r12
pop rbx
ret

lm_unmap_4k:
; rdi = va
push rbx
push r15
mov r15, cr3
and r15, ~0xFFF
mov rbx, rdi
shr rbx, 39
and rbx, 0x1FF
mov r15, [r15 + rbx*8]
test r15b, PTE_P
jz .done
and r15, ~0xFFF
mov rbx, rdi
shr rbx, 30
and rbx, 0x1FF
mov r15, [r15 + rbx*8]
test r15b, PTE_P
jz .done
test r15b, PTE_PS
jnz .done
and r15, ~0xFFF
mov rbx, rdi
shr rbx, 21
and rbx, 0x1FF
mov r15, [r15 + rbx*8]
test r15b, PTE_P
jz .done
test r15b, PTE_PS
jnz .done
and r15, ~0xFFF
mov rbx, rdi
shr rbx, 12
and rbx, 0x1FF
mov qword [r15 + rbx*8], 0
invlpg [rdi]
.done:
pop r15
pop rbx
ret

; ============================================================================
; SECTION 11: GDT + TSS
; ============================================================================

align 16, db 0
lm_gdt:
dq 0x0000000000000000
dq 0x00AF9A000000FFFF
dq 0x00CF92000000FFFF
dq 0x00CFF2000000FFFF
dq 0x00AFFA000000FFFF
lm_gdt_tss:
dq 0
dq 0
lm_gdt_end:

lm_gdtr:
dw lm_gdt_end - lm_gdt - 1
dq lm_gdt

align 16, db 0
lm_tss:
dd 0
dq 0
dq 0
dq 0
dq 0
dq 0
dq 0
dq 0
dq 0
dq 0
dq 0
dq 0
dw 0
dw 0
lm_tss_end:

lm_gdt_init:
push rax
push rbx
push rcx
push rdi
push rsi

; Fill the TSS descriptor.
lea rax, [rel lm_tss]
lea rbx, [rel lm_gdt_tss]
mov cx, lm_tss_end - lm_tss - 1
mov word [rbx + 0], cx
mov rcx, rax
mov word [rbx + 2], cx
shr rcx, 16
mov byte [rbx + 4], cl
mov byte [rbx + 5], 0x89
mov byte [rbx + 6], 0x00
shr rcx, 8
mov byte [rbx + 7], cl
shr rcx, 8
mov dword [rbx + 8], ecx
mov dword [rbx + 12], 0

; Set IST1 for double fault.
lea rax, [rel lm_tss]
mov rcx, LM_IST_TOP
mov [rax + 36], rcx

lgdt [rel lm_gdtr]
push qword GDT_KCODE
lea rax, [rel .reload_cs]
push rax
retfq
.reload_cs:
mov ax, GDT_KDATA
mov ds, ax
mov es, ax
mov ss, ax
xor eax, eax
mov fs, ax
mov gs, ax
mov ax, GDT_TSS
ltr ax

pop rsi
pop rdi
pop rcx
pop rbx
pop rax
ret

; ============================================================================
; SECTION 12: IDT + EXCEPTION STUBS
; ============================================================================

align 16, db 0
lm_idt:
times 256 * 16 db 0

lm_idtr:
dw 256 * 16 - 1
dq lm_idt

lm_idt_set_gate:
; rdi = vector, rsi = handler, rdx = IST index (0..7)
push rax
push rbx
push rcx
push rdi
push rsi
mov rax, rdi
shl rax, 4
lea rbx, [rel lm_idt]
add rbx, rax
mov rcx, rsi
mov word [rbx + 0], cx
mov word [rbx + 2], GDT_KCODE
and rdx, 7
mov byte [rbx + 4], dl
mov byte [rbx + 5], IDT_PRESENT | IDT_DPL0 | IDT_INT_GATE
mov rcx, rsi
shr rcx, 16
mov word [rbx + 6], cx
mov rcx, rsi
shr rcx, 32
mov dword [rbx + 8], ecx
pop rsi
pop rdi
pop rcx
pop rbx
pop rax
ret

; Frame layout after the stub and the isr_common pushes, indexed from
; RSP (low address) upward:
;   [rsp + 0]   r15
;   [rsp + 8]   r14
;   [rsp + 16]  r13
;   [rsp + 24]  r12
;   [rsp + 32]  r11
;   [rsp + 40]  r10
;   [rsp + 48]  r9
;   [rsp + 56]  r8
;   [rsp + 64]  rbp
;   [rsp + 72]  rdi
;   [rsp + 80]  rsi
;   [rsp + 88]  rdx
;   [rsp + 96]  rcx
;   [rsp + 104] rbx
;   [rsp + 112] rax
;   [rsp + 120] int_no        (stub)
;   [rsp + 128] err_code      (stub or CPU)
;   [rsp + 136] rip           (CPU)
;   [rsp + 144] cs            (CPU)
;   [rsp + 152] rflags        (CPU)
;   [rsp + 160] rsp           (CPU, CPL change only)
;   [rsp + 168] ss            (CPU, CPL change only)

isr_common:
push rax
push rbx
push rcx
push rdx
push rsi
push rdi
push rbp
push r8
push r9
push r10
push r11
push r12
push r13
push r14
push r15

mov rdi, rsp
call lm_exception_dispatch

pop r15
pop r14
pop r13
pop r12
pop r11
pop r10
pop r9
pop r8
pop rbp
pop rdi
pop rsi
pop rdx
pop rcx
pop rbx
pop rax
add rsp, 16         ; drop int_no and err_code
iretq

%macro ISR_NOERR 1
isr%1:
push qword 0
push qword %1
jmp isr_common
%endmacro

%macro ISR_ERR 1
isr%1:
push qword %1
jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

%macro IRQ_STUB 1
irq%1:
push qword 0
push qword (32 + %1)
jmp isr_common
%endmacro

IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15

; Default exception dispatcher. Prints the vector number to VGA and serial,
; then halts. Override by installing a custom handler with
; lm_idt_set_gate, or replace this routine's body if you want shared
; pre/post handling.
;
; NOTE: With the default handler installed, this routine never returns.
;       The unwind path in isr_common (the 15 pops + iretq) is only live
;       if you replace this dispatcher with one that returns.
lm_exception_dispatch:
; rdi = pointer to regs frame
push rbx
push rdi
mov rbx, [rdi + 15*8]     ; int_no

lea rdi, [rel .msg]
call lm_puts
mov rax, rbx
lm_print_hex_byte
lm_print_nl

; Echo to serial.
lea rdi, [rel .msg]
call lm_serial_puts
mov al, bl
call lm_serial_hex_byte
mov dil, 13
call lm_serial_putc
mov dil, 10
call lm_serial_putc

pop rdi
pop rbx
lm_halt
.msg: db "exception ", 0

lm_idt_init:
push rax
push rbx
push rcx
push rdi
push rsi
push rdx

xor rbx, rbx
.ext_loop:
cmp rbx, 32
jae .irq_start
lea rcx, [rel lm__isr_table]
mov rax, [rcx + rbx*8]
mov rdi, rbx
mov rsi, rax
xor edx, edx
cmp rbx, 8
je .use_ist
cmp rbx, 13
je .use_ist
cmp rbx, 14
je .use_ist
jmp .install
.use_ist:
mov edx, 1
.install:
call lm_idt_set_gate
inc rbx
jmp .ext_loop

.irq_start:
xor rbx, rbx
.irq_loop:
cmp rbx, 16
jae .load
lea rcx, [rel lm__irq_table]
mov rax, [rcx + rbx*8]
mov rdi, 32
add rdi, rbx
mov rsi, rax
xor edx, edx
call lm_idt_set_gate
inc rbx
jmp .irq_loop

.load:
lidt [rel lm_idtr]

pop rdx
pop rsi
pop rdi
pop rcx
pop rbx
pop rax
ret

align 8, db 0
lm__isr_table:
dq isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7
dq isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15
dq isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
dq isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31

lm__irq_table:
dq irq0,  irq1,  irq2,  irq3,  irq4,  irq5,  irq6,  irq7
dq irq8,  irq9,  irq10, irq11, irq12, irq13, irq14, irq15

; ============================================================================
; SECTION 13: PIC
; ============================================================================

lm_pic_remap:
; rdi = master vector offset, rsi = slave vector offset
push rax
push rdx
push rdi
push rsi

mov dx, PIC1_CMD
mov al, ICW1_INIT | ICW1_ICW4
out dx, al
mov dx, PIC2_CMD
out dx, al

mov dx, PIC1_DATA
mov al, dil
out dx, al
mov dx, PIC2_DATA
mov al, sil
out dx, al

mov dx, PIC1_DATA
mov al, 0x04
out dx, al
mov dx, PIC2_DATA
mov al, 0x02
out dx, al

mov dx, PIC1_DATA
mov al, ICW4_8086
out dx, al
mov dx, PIC2_DATA
out dx, al

mov dx, PIC1_DATA
mov al, PIC_DISABLE
out dx, al
mov dx, PIC2_DATA
out dx, al

pop rsi
pop rdi
pop rdx
pop rax
ret

lm_pic_mask:
; rdi = master mask, rsi = slave mask
push rax
push rdx
mov dx, PIC1_DATA
mov al, dil
out dx, al
mov dx, PIC2_DATA
mov al, sil
out dx, al
pop rdx
pop rax
ret

lm_pic_eoi:
; rdi = IRQ number
push rax
push rdx
cmp rdi, 8
jb .master
mov dx, PIC2_CMD
mov al, PIC_EOI
out dx, al
.master:
mov dx, PIC1_CMD
mov al, PIC_EOI
out dx, al
pop rdx
pop rax
ret

; ============================================================================
; SECTION 14: ATA PIO (LBA48)
; ============================================================================

; lm_ata_read(rdi = LBA, rsi = count, rdx = dest) -> rax = 0 ok, -1 error.
; Preserves all callee-saved registers.
lm_ata_read:
push rbx
push rbp
push r12
push r13
push r14
push r15
mov r12, rdi
mov r13, rsi
mov r14, rdx
.loop:
mov r15, ATA_TIMEOUT
.wait_bsy:
mov dx, ATA_STATUS
in al, dx
test al, ATA_SR_BSY
jz .drive_select
dec r15
jnz .wait_bsy
jmp .error

.drive_select:
mov dx, ATA_DRIVE
; LBA48: drive/head holds only the drive select bits.
;   0x40 = LBA mode, master. (LBA[27:24] are sent via the LBA
;   registers, not the drive/head register, in LBA48 mode.)
mov al, 0x40
out dx, al

mov dx, ATA_ALT_STATUS
in al, dx
in al, dx
in al, dx
in al, dx

; High bytes first: sector count [15:8] = 0
mov dx, ATA_SECCNT
xor al, al
out dx, al
; LBA[24:31]
mov dx, ATA_LBA_LO
mov rax, r12
shr rax, 24
out dx, al
; LBA[32:39]
shr rax, 8
mov dx, ATA_LBA_MID
out dx, al
; LBA[40:47]
shr rax, 8
mov dx, ATA_LBA_HI
out dx, al

; Low bytes: sector count [7:0] = 1
mov dx, ATA_SECCNT
mov al, 1
out dx, al
; LBA[0:7]
mov dx, ATA_LBA_LO
mov rax, r12
out dx, al
; LBA[8:15]
shr rax, 8
mov dx, ATA_LBA_MID
out dx, al
; LBA[16:23]
shr rax, 8
mov dx, ATA_LBA_HI
out dx, al

mov dx, ATA_CMD
mov al, ATA_READ_EXT
out dx, al

mov r15, ATA_TIMEOUT
.wait_drq:
mov dx, ATA_STATUS
in al, dx
test al, ATA_SR_BSY
jnz .cont
test al, ATA_SR_ERR
jnz .error
test al, ATA_SR_DF
jnz .error
test al, ATA_SR_DRQ
jnz .read
.cont:
dec r15
jnz .wait_drq
jmp .error

.read:
mov dx, ATA_DATA
mov rcx, 256
mov rdi, r14
cld
rep insw
add r14, 512
inc r12
dec r13
jnz .loop

xor eax, eax
jmp .done
.error:
mov rax, -1
.done:
pop r15
pop r14
pop r13
pop r12
pop rbp
pop rbx
ret

; ============================================================================
; SECTION 15: KERNEL LOADER
; ============================================================================

%macro lm_load_kernel 0
mov rdi, KERNEL_LBA
mov rsi, KERNEL_SECTORS
mov rdx, KERNEL_LOAD_ADDR
call lm_ata_read
%endmacro

; ============================================================================
; SECTION 16: STATE
; ============================================================================

align 8, db 0
lm__heap_ptr:
dq 0

%endif ; LM_INC
