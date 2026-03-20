/*
 * OS next stage BIOS text installer - stage 1
 *
 * This is a BIOS volume boot sector for the DOS-style installer path.
 * It carries a minimal FAT-style BPB so chainloaders recognise it as a
 * bootable volume, then loads installer stage 2 from the following sectors.
 */

.code16
.section .text
.global _start

#define BOOT_STAGE1_SEGMENT 0x07C0
#define BOOT_STAGE2_SEGMENT 0x07E0

_start:
    jmp     real_mode_entry
    nop

    .ascii  "OSKERNEL"
    .word   512                 /* bytes per sector */
    .byte   1                   /* sectors per cluster */
    .word   1                   /* reserved sectors */
    .byte   1                   /* number of FATs */
    .word   16                  /* root directory entries */
    .word   0                   /* total sectors (16-bit) */
    .byte   0xF8                /* media descriptor */
    .word   1                   /* sectors per FAT */
    .word   63                  /* sectors per track */
    .word   255                 /* heads */
hidden_sectors:
    .long   0                   /* patched to partition LBA when embedded */
    .long   32768               /* total sectors (32-bit, 16 MiB image) */
    .byte   0x80                /* BIOS drive */
    .byte   0
    .byte   0x29                /* extended boot signature */
    .long   0x4F534E58          /* volume serial */
    .ascii  "OSKSETUP   "       /* volume label */
    .ascii  "FAT16   "

    ljmp    $BOOT_STAGE1_SEGMENT, $real_mode_entry

real_mode_entry:
    /* ================================================================= */
    /* Set up segments and stack */
    /* ================================================================= */
    cli
    mov     %cs, %ax
    mov     %ax, %ds
    xor     %ax, %ax
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
    
    ljmp    $BOOT_STAGE2_SEGMENT, $0x0000
    
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

    movl    hidden_sectors, %eax
    incl    %eax
    movl    %eax, dap_lba
    movl    $0, dap_lba + 4
    movb    boot_drive, %dl
    mov     $dap, %si
    mov     $0x4200, %ax
    int     $0x13
    jnc     .done

    cmpl    $0, hidden_sectors
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
stage2_sector_count:
    .word   16
    .word   0x7E00
    .word   0x0000
dap_lba:
    .quad   1

.org 510
.word 0xAA55
