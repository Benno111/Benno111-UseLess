BITS 16
ORG 0x8000

%define BOOTINFO_ADDR 0x7000
%define MMAP_ADDR     0x7200
%define CMDLINE_ADDR  0x7800
%define CMDLINE_MAX   120
%define MMAP_MAX      128

%ifndef KERNEL_LBA
%define KERNEL_LBA 129
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 256
%endif
%ifndef KERNEL_SIZE
%define KERNEL_SIZE (KERNEL_SECTORS*512)
%endif
%ifndef KERNEL_ENTRY
%define KERNEL_ENTRY 0x00100000
%endif

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov si, s2_msg
    call print

    call enable_a20
    call load_kernel
    mov si, s2_load_msg
    call print
    call build_memory_map
    mov si, s2_mmap_msg
    call print
    call read_cmdline
    call build_bootinfo
    mov si, s2_jump_msg
    call print
    call enter_long_mode

hang:
    hlt
    jmp hang

enable_a20:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    ret

disk_reset:
    mov ah, 0x00
    mov dl, [boot_drive]
    int 0x13
    ret

enter_unreal_mode:
    cli
    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    sti
    ret

set_real_mode:
    mov ax, 0
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

load_kernel:
    mov word [lba_cur], KERNEL_LBA
    mov cx, KERNEL_SECTORS
    mov edi, 0x00100000

.read_loop:
    cmp cx, 0
    je .done
    push cx
    mov byte [retry_count], 3
.retry:
    mov ah, 0x42
    mov dl, [boot_drive]
    mov word [dap+2], 1
    mov word [dap+4], 0x0000
    mov word [dap+6], 0x2000
    mov ax, [lba_cur]
    mov word [dap+8], ax
    mov word [dap+10], 0
    mov dword [dap+12], 0x00000000
    mov si, dap
    int 0x13
    jnc .read_ok
    mov [disk_status], ah
    call disk_reset
    dec byte [retry_count]
    jnz .retry
    jmp .disk_error
.read_ok:
    call enter_unreal_mode
    cld
    mov esi, 0x00020000
    mov ecx, 128
    a32 rep movsd
    add edi, 512
    call set_real_mode
    pop cx
    inc word [lba_cur]
    dec cx
    jmp .read_loop

.disk_error:
    mov [disk_status], ah
    mov al, 1
    jmp boot_error

.done:
    ret

build_memory_map:
    xor ebx, ebx
    mov di, MMAP_ADDR
    mov word [mmap_count], 0

.e820_next:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    mov edi, e820_buf
    int 0x15
    jc .fail
    cmp eax, 0x534D4150
    jne .fail
    mov eax, [e820_buf]
    mov edx, [e820_buf+4]
    mov ecx, [e820_buf+8]
    mov esi, [e820_buf+12]
    or ecx, esi
    jz .skip
    mov ax, [mmap_count]
    cmp ax, MMAP_MAX
    jae .done
    mov si, di
    mov eax, [e820_buf]
    mov [si+0], eax
    mov eax, [e820_buf+4]
    mov [si+4], eax
    mov eax, [e820_buf]
    mov edx, [e820_buf+4]
    add eax, [e820_buf+8]
    adc edx, [e820_buf+12]
    mov [si+8], eax
    mov [si+12], edx
    mov eax, [e820_buf+16]
    mov dword [si+16], eax
    mov dword [si+20], 0
    add di, 24
    inc word [mmap_count]
.skip:
    test ebx, ebx
    jne .e820_next

.done:
    ret
.fail:
    mov al, 2
    jmp boot_error

build_bootinfo:
    mov si, BOOTINFO_ADDR
    mov dword [si+0], 0x4F534B42
    mov dword [si+4], 0x4F4F5449
    mov dword [si+8], 1
    mov dword [si+12], 1
    mov dword [si+16], MMAP_ADDR
    mov dword [si+20], 0
    movzx eax, word [mmap_count]
    mov dword [si+24], eax
    mov dword [si+28], 0
    mov dword [si+32], 0
    mov dword [si+36], 0
    mov dword [si+40], 0
    mov dword [si+44], 0
    mov dword [si+48], 0x00100000
    mov dword [si+52], 0
    mov dword [si+56], KERNEL_SIZE
    mov dword [si+60], 0
    mov dword [si+64], 0
    mov dword [si+68], 0
    mov dword [si+72], CMDLINE_ADDR
    mov dword [si+76], 0
    movzx eax, word [cmdline_len]
    mov dword [si+80], eax
    mov dword [si+84], 0
    ret

read_cmdline:
    mov si, prompt_msg
    call print
    mov di, CMDLINE_ADDR
    xor cx, cx
.loop:
    xor ax, ax
    int 0x16
    cmp al, 0x0D
    je .done
    cmp al, 0x08
    je .backspace
    cmp cx, CMDLINE_MAX
    jae .loop
    mov [di], al
    inc di
    inc cx
    mov ah, 0x0E
    int 0x10
    jmp .loop
.backspace:
    cmp cx, 0
    je .loop
    dec di
    dec cx
    mov ah, 0x0E
    mov al, 0x08
    int 0x10
    mov al, ' '
    int 0x10
    mov al, 0x08
    int 0x10
    jmp .loop
.done:
    mov byte [di], 0
    mov [cmdline_len], cx
    mov ah, 0x0E
    mov al, 0x0D
    int 0x10
    mov al, 0x0A
    int 0x10
    mov si, cmdline_ok_msg
    call print
    ret

enter_long_mode:
    mov al, 'G'
    call putc_direct
    cli
    lgdt [gdt_ptr]

    mov al, 'P'
    call putc_direct
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    mov al, 'C'
    call putc_direct
    mov eax, pml4
    mov cr3, eax

    mov al, 'E'
    call putc_direct
    call check_long_mode
    mov ecx, 0xC0000080
    rdmsr
    mov al, 'R'
    call putc_direct
    or eax, (1 << 8)
    wrmsr
    mov al, 'W'
    call putc_direct

    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    jmp 0x08:long_mode_entry

boot_error:
    push ax
    mov si, err_msg
    call print
    pop ax
    mov ah, 0x0E
    add al, '0'
    int 0x10
    mov ah, 0x0E
    mov al, ' '
    int 0x10
    mov al, [disk_status]
    call print_hex8
    mov ah, 0x0E
    mov al, 0x0D
    int 0x10
    mov al, 0x0A
    int 0x10
    jmp hang

print_hex8:
    push ax
    mov ah, al
    shr al, 4
    call print_hex_nibble
    mov al, ah
    and al, 0x0F
    call print_hex_nibble
    pop ax
    ret

print_hex_nibble:
    cmp al, 9
    jbe .num
    add al, 'A' - 10
    jmp .out
.num:
    add al, '0'
.out:
    mov ah, 0x0E
    int 0x10
    ret

check_long_mode:
    push eax
    push ebx
    push ecx
    push edx
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .fail
    mov eax, 0x80000001
    cpuid
    test edx, (1 << 29)
    jz .fail
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret
.fail:
    pop edx
    pop ecx
    pop ebx
    pop eax
    mov al, 3
    jmp boot_error

putc_direct:
    push ax
    push bx
    push di
    mov bx, 0xB800
    mov es, bx
    mov di, [vga_cursor]
    mov ah, 0x07
    mov [es:di], ax
    add di, 2
    mov [vga_cursor], di
    pop di
    pop bx
    pop ax
    ret

BITS 64
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, 0x00090000
    mov rdi, BOOTINFO_ADDR
    mov rax, KERNEL_ENTRY
    jmp rax

BITS 16
print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print
.done:
    ret

boot_drive: db 0
err_msg: db "Boot error code: ", 0
mmap_count: dw 0
cmdline_len: dw 0
lba_cur: dw 0
disk_status: db 0
retry_count: db 0
vga_cursor: dw 0
e820_buf: times 24 db 0
prompt_msg: db "Boot params: ", 0
s2_msg: db "S2", 0
s2_load_msg: db " LK", 0
s2_mmap_msg: db " MM", 0
s2_jump_msg: db " LM", 0
cmdline_ok_msg: db 0x0D, 0x0A, "Boot params accepted.", 0x0D, 0x0A, 0

dap:
    db 0x10
    db 0
    dw 1
    dw 0
    dw 0
    dq 0

align 16
gdt:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt
gdt_end:

align 4096
pml4:
    dq (pdpt + 0x03)
    times 511 dq 0
align 4096
pdpt:
    dq (pd0 + 0x03)
    times 511 dq 0
align 4096
pd0:
    %assign i 0
    %rep 512
        dq (i * 0x200000) | 0x83
        %assign i i+1
    %endrep
