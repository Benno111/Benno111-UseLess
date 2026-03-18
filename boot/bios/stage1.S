/*
 * Vib-OS BIOS Bootloader - Stage 1
 * 
 * This is the Master Boot Record (MBR) that loads stage 2
 * Loaded by BIOS at 0x7C00
 */

.code16
.section .text
.global _start

_start:
    ljmp    $0x0000, $real_mode_entry

real_mode_entry:
    /* ================================================================= */
    /* Set up segments and stack */
    /* ================================================================= */
    cli
    xor     %ax, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss
    mov     $0x7C00, %sp
    sti

    mov     %dl, boot_drive
    
    /* ================================================================= */
    /* Print boot message */
    /* ================================================================= */
    mov     $msg_boot, %si
    call    print_string
    
    /* ================================================================= */
    /* Load stage 2 from disk */
    /* ================================================================= */
    call    load_stage2
    jc      disk_error
    
    /* ================================================================= */
    /* Jump to stage 2 */
    /* ================================================================= */
    mov     $msg_stage2, %si
    call    print_string
    
    ljmp    $0x0000, $0x7E00
    
/* ===================================================================== */
/* Error Handling */
/* ===================================================================== */

disk_error:
    mov     $msg_error, %si
    call    print_string
    jmp     halt

halt:
    cli
    hlt
    jmp     halt

/* ===================================================================== */
/* Print String Function */
/* Input: SI = pointer to null-terminated string */
/* ===================================================================== */

print_string:
    push    %ax
    push    %bx
    
.print_loop:
    lodsb                       # Load byte from SI into AL
    test    %al, %al            # Check if null terminator
    jz      .print_done
    
    mov     $0x0E, %ah          # BIOS teletype output
    mov     $0x07, %bx          # Page 0, light gray
    int     $0x10               # BIOS video interrupt
    
    jmp     .print_loop
    
.print_done:
    pop     %bx
    pop     %ax
    ret

/* ===================================================================== */
/* Stage 2 loader */
/* ===================================================================== */

load_stage2:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    push    %si

    movl    partition_base_lba, %eax
    incl    %eax
    movl    %eax, dap_lba
    movl    $0, dap_lba + 4
    movb    boot_drive, %dl
    mov     $dap, %si
    mov     $0x4200, %ax
    int     $0x13
    jnc     .done

    cmpb    $0, partition_base_lba
    jne     .fail

    movb    boot_drive, %dl
    mov     $0x0200, %ax        # BIOS read sectors function
    mov     $16, %al            # Number of sectors to read
    mov     $0x0000, %cx        # Cylinder 0, sector 0
    mov     $0x0000, %dx        # Head 0, preserve drive below
    movb    boot_drive, %dl
    movb    $0x02, %cl          # Start from sector 2
    mov     $0x7E00, %bx        # Load to 0x7E00
    int     $0x13
    jnc     .done

.fail:
    stc

.done:
    pop     %si
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* ===================================================================== */
/* Data */
/* ===================================================================== */

msg_boot:
    .asciz  "OS next stage Stage 1\r\n"

msg_stage2:
    .asciz  "Loading Stage 2...\r\n"

msg_error:
    .asciz  "Disk read error!\r\n"

boot_drive:
    .byte   0

dap:
    .byte   0x10
    .byte   0
    .word   16
    .word   0x7E00
    .word   0x0000
dap_lba:
    .quad   1

/* ===================================================================== */
/* Boot Signature */
/* ===================================================================== */

.org 502
partition_base_lba:
    .long   0

.org 510
.word 0xAA55
