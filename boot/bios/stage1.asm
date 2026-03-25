org 0x7C00
bits 16

start:
    jmp short boot_main
    nop

    ; FAT16 BPB / EBPB area. The image builder patches these fields in-place,
    ; so executable code must stay out of offsets 0x0B..0x3D.
oem_id              db 'OSKSETUP'
bytes_per_sector    dw 512
sectors_per_cluster db 0
reserved_sectors    dw 0
fat_count           db 0
root_entries        dw 0
total_sectors_16    dw 0
media_descriptor    db 0
fat_sectors         dw 0
sectors_per_track   dw 0
head_count          dw 0
hidden_sectors      dd 0
total_sectors_32    dd 0
drive_number        db 0
reserved1           db 0
boot_signature      db 0
volume_id           dd 0
volume_label        db 'OSKSETUP   '
fs_type             db 'FAT16   '

boot_main:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    cld
    sti

    mov [boot_drive], dl
    mov [drive_number], dl

    call load_stage2_lba
    jnc boot_stage2

    call load_stage2_chs
    jnc boot_stage2

    mov si, disk_error_msg
    call show_error
    jmp hang

boot_stage2:
    jmp 0x0000:0x7E00

load_stage2_lba:
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .unsupported
    cmp bx, 0xAA55
    jne .unsupported
    test cx, 0x0001
    jz .unsupported

    mov byte [retry_count], 3
.retry:
    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jnc .done

    call reset_disk_with_delay
    dec byte [retry_count]
    jnz .retry

.unsupported:
    stc
    ret

.done:
    clc
    ret

load_stage2_chs:
    mov word [current_lba], 1
    mov di, 0x7E00
    mov si, [stage2_sector_count]

.next_sector:
    test si, si
    jz .done

    mov byte [retry_count], 3
.retry:
    call lba_to_chs
    jc .fail

    mov bx, di
    mov ax, 0x0201
    mov dl, [boot_drive]
    int 0x13
    jnc .sector_ok

    call reset_disk_with_delay
    dec byte [retry_count]
    jnz .retry
    jmp .fail

.sector_ok:
    add di, 512
    inc word [current_lba]
    dec si
    jmp .next_sector

.done:
    clc
    ret

.fail:
    stc
    ret

lba_to_chs:
    mov ax, [sectors_per_track]
    or ax, ax
    jz .fail
    mov bx, ax
    mov ax, [current_lba]
    xor dx, dx
    div bx

    mov cl, dl
    inc cl

    mov ax, [head_count]
    or ax, ax
    jz .fail
    mov bx, ax
    xor dx, dx
    div bx

    cmp ax, 1024
    jae .fail

    mov dh, dl
    mov ch, al
    shl ah, 6
    and ah, 0xC0
    or cl, ah
    clc
    ret

.fail:
    stc
    ret

reset_disk_with_delay:
    push ax
    push bx
    mov ah, 0x00
    mov dl, [boot_drive]
    int 0x13
    mov bx, 0xFFFF
.delay:
    dec bx
    jnz .delay
    pop bx
    pop ax
    ret

show_error:
    mov ax, 0x0003
    int 0x10
    call print_string
    ret

hang:
    cli
    hlt
    jmp hang

print_string:
    mov ah, 0x0E
    xor bx, bx
    mov bl, 0x07
.next:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .next
.done:
    ret

boot_drive db 0
current_lba dw 0
retry_count db 0

disk_error_msg db "Stage2 load failed", 0

dap:
    db 0x10
    db 0
stage2_sector_count dw 16
    dw 0x7E00
    dw 0x0000
    dq 1

times 510-($-$$) db 0
dw 0xAA55
