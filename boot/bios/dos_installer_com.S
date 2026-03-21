/*
 * OS next stage DOS runnable rescue installer utility.
 *
 * Build output is a flat .COM binary intended to be copied next to OSINST.IMG
 * on an existing DOS system. The program writes that image to a selected BIOS
 * hard disk using Int 13h extensions.
 */

.code16
.section .text
.global _start
.org 0x100

#define IMAGE_FILENAME "OSINST.IMG"

_start:
    cli
    mov     %cs, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss
    mov     $stack_top, %sp
    sti

    movb    $0, selected_drive

main_menu:
    call    clear_screen
    mov     $title_text, %si
    call    print_string
    mov     $subtitle_text, %si
    call    print_string
    call    print_target_summary
    mov     $menu_text, %si
    call    print_string
    call    read_key

    cmp     $'1', %al
    je      install_image
    cmp     $'2', %al
    je      select_target_disk
    cmp     $'3', %al
    je      show_help
    cmp     $'4', %al
    je      exit_to_dos
    cmp     $27, %al
    je      exit_to_dos
    jmp     main_menu

install_image:
    call    clear_screen
    mov     $install_intro_text, %si
    call    print_string
    call    print_target_summary

    movb    selected_drive, %al
    test    %al, %al
    jz      no_target_disk

    mov     $confirm_text, %si
    call    print_string
    call    read_key
    cmp     $'y', %al
    je      do_install
    cmp     $'Y', %al
    je      do_install
    jmp     main_menu

do_install:
    call    clear_screen
    mov     $copy_begin_text, %si
    call    print_string
    call    print_target_summary
    call    install_image_to_target
    jc      install_failed

    mov     $copy_success_text, %si
    call    print_string
    call    wait_for_keypress
    jmp     main_menu

install_failed:
    mov     $copy_failed_text, %si
    call    print_string
    call    wait_for_keypress
    jmp     main_menu

no_target_disk:
    mov     $no_target_text, %si
    call    print_string
    call    wait_for_keypress
    jmp     main_menu

select_target_disk:
    call    select_next_target
    call    clear_screen
    jc      select_target_none
    mov     $target_updated_text, %si
    call    print_string
    call    print_target_summary
    call    wait_for_keypress
    jmp     main_menu

select_target_none:
    mov     $target_missing_text, %si
    call    print_string
    call    wait_for_keypress
    jmp     main_menu

show_help:
    call    clear_screen
    mov     $help_text, %si
    call    print_string
    call    print_target_summary
    call    wait_for_keypress
    jmp     main_menu

clear_screen:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    mov     $0x0600, %ax
    mov     $0x0700, %bx
    mov     $0x0000, %cx
    mov     $0x184F, %dx
    int     $0x10
    mov     $0x0200, %ax
    xor     %bx, %bx
    xor     %dx, %dx
    int     $0x10
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

read_key:
    mov     $0x0000, %ax
    int     $0x16
    ret

wait_for_keypress:
    mov     $press_key_text, %si
    call    print_string
    call    read_key
    ret

print_string:
    push    %ax
    push    %bx
print_string_loop:
    lodsb
    test    %al, %al
    jz      print_string_done
    mov     $0x0E, %ah
    mov     $0x0007, %bx
    int     $0x10
    jmp     print_string_loop
print_string_done:
    pop     %bx
    pop     %ax
    ret

print_char:
    push    %ax
    push    %bx
    mov     $0x0E, %ah
    mov     $0x0007, %bx
    int     $0x10
    pop     %bx
    pop     %ax
    ret

print_hex_nibble:
    and     $0x0F, %al
    cmp     $10, %al
    jb      print_hex_nibble_digit
    add     $'A' - 10, %al
    jmp     print_char
print_hex_nibble_digit:
    add     $'0', %al
    jmp     print_char

print_hex_byte:
    push    %ax
    mov     %al, %ah
    shr     $4, %al
    call    print_hex_nibble
    mov     %ah, %al
    call    print_hex_nibble
    pop     %ax
    ret

print_drive_id:
    push    %ax
    mov     $'0', %al
    call    print_char
    mov     $'x', %al
    call    print_char
    pop     %ax
    call    print_hex_byte
    ret

print_target_summary:
    push    %ax
    mov     $image_file_text, %si
    call    print_string
    mov     $image_filename_text, %si
    call    print_string
    mov     $crlf_text, %si
    call    print_string
    mov     $target_drive_text, %si
    call    print_string
    movb    selected_drive, %al
    test    %al, %al
    jz      print_target_summary_none
    call    print_drive_id
    mov     $crlf_text, %si
    call    print_string
    pop     %ax
    ret
print_target_summary_none:
    mov     $none_text, %si
    call    print_string
    pop     %ax
    ret

probe_drive:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    mov     $0x55AA, %bx
    mov     $0x4100, %ax
    int     $0x13
    jc      dos_probe_drive_fail
    cmp     $0xAA55, %bx
    jne     dos_probe_drive_fail
    test    $0x0001, %cx
    jz      dos_probe_drive_fail
    clc
    jmp     dos_probe_drive_done
dos_probe_drive_fail:
    stc
dos_probe_drive_done:
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

find_default_target:
    movb    $0, selected_drive
    movb    $0x81, %bl
dos_find_target_scan:
    cmpb    $0x88, %bl
    jae     dos_find_target_done
    movb    %bl, %dl
    call    probe_drive
    jc      dos_find_target_next
    movb    %bl, selected_drive
    ret
dos_find_target_next:
    inc     %bl
    jmp     dos_find_target_scan
dos_find_target_done:
    ret

select_next_target:
    push    %bx
    push    %cx
    push    %dx
    movb    selected_drive, %bl
    cmpb    $0x81, %bl
    jae     dos_select_target_cycle
    movb    $0x80, %bl
dos_select_target_cycle:
    movb    $8, %cl
dos_select_target_try:
    inc     %bl
    cmpb    $0x88, %bl
    jb      dos_select_target_check
    movb    $0x81, %bl
dos_select_target_check:
    movb    %bl, %dl
    call    probe_drive
    jc      dos_select_target_continue
    movb    %bl, selected_drive
    clc
    jmp     dos_select_target_done
dos_select_target_continue:
    dec     %cl
    jnz     dos_select_target_try
    movb    $0, selected_drive
    stc
dos_select_target_done:
    pop     %dx
    pop     %cx
    pop     %bx
    ret

install_image_to_target:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    push    %si
    push    %di

    movb    selected_drive, %dl
    call    probe_drive
    jc      .fail

    mov     $image_filename_dos, %dx
    mov     $0x3D00, %ax
    int     $0x21
    jc      .file_open_failed
    mov     %ax, file_handle

    xor     %ax, %ax
    mov     %ax, progress_divider
    mov     %ax, current_lba
    mov     %ax, current_lba + 2
    mov     %ax, current_lba + 4
    mov     %ax, current_lba + 6

dos_install_read_loop:
    mov     file_handle, %bx
    mov     $sector_buffer, %dx
    mov     $512, %cx
    mov     $0x3F, %ah
    int     $0x21
    jc      dos_install_read_failed
    test    %ax, %ax
    jz      dos_install_success_close

    cmp     $512, %ax
    je      dos_install_write_sector

    mov     %ax, %cx
    mov     $sector_buffer, %di
    add     %ax, %di
    mov     $512, %bx
    sub     %ax, %bx
    xor     %ax, %ax
    mov     %bx, %cx
    rep stosb

dos_install_write_sector:
    movw    $1, dap_sector_count
    movw    $sector_buffer, dap_buffer_offset
    mov     %ds, %ax
    movw    %ax, dap_buffer_segment

    movw    current_lba, %ax
    movw    %ax, dap_lba
    movw    current_lba + 2, %ax
    movw    %ax, dap_lba + 2
    movw    current_lba + 4, %ax
    movw    %ax, dap_lba + 4
    movw    current_lba + 6, %ax
    movw    %ax, dap_lba + 6

    movb    selected_drive, %dl
    mov     $dap, %si
    mov     $0x4300, %ax
    int     $0x13
    jc      dos_install_write_failed

    incw    current_lba
    jnz     .progress
    incw    current_lba + 2
    jnz     .progress
    incw    current_lba + 4
    jnz     .progress
    incw    current_lba + 6

.dos_install_progress:
    incw    progress_divider
    testw   $0x003F, progress_divider
    jnz     dos_install_read_loop
    mov     $'.', %al
    call    print_char
    jmp     dos_install_read_loop

dos_install_success_close:
    mov     file_handle, %bx
    mov     $0x3E, %ah
    int     $0x21
    movw    $0xFFFF, file_handle
    mov     $crlf_text, %si
    call    print_string
    clc
    jmp     dos_install_done

dos_install_file_open_failed:
    mov     $file_missing_text, %si
    call    print_string
    stc
    jmp     dos_install_done

dos_install_read_failed:
    mov     file_handle, %bx
    mov     $0x3E, %ah
    int     $0x21
    movw    $0xFFFF, file_handle
    mov     $file_read_error_text, %si
    call    print_string
    stc
    jmp     dos_install_done

dos_install_write_failed:
    mov     file_handle, %bx
    mov     $0x3E, %ah
    int     $0x21
    movw    $0xFFFF, file_handle
    mov     $disk_write_error_text, %si
    call    print_string
    stc
    jmp     dos_install_done

dos_install_fail:
    stc

dos_install_done:
    pop     %di
    pop     %si
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

exit_to_dos:
    mov     $0x4C00, %ax
    int     $0x21

title_text:
    .asciz  "OS next stage DOS Rescue Installer Utility\r\n"
subtitle_text:
    .asciz  "Run this from an existing DOS system with OSINST.IMG nearby.\r\nIt writes the fallback installer image to another BIOS disk.\r\n\r\n"
menu_text:
    .asciz  "  1. Write OSINST.IMG to target disk\r\n  2. Change target disk\r\n  3. Help\r\n  4. Exit to DOS\r\n  ESC. Exit to DOS\r\n\r\nSelect an option: "
help_text:
    .asciz  "Usage:\r\n  Place OSINST.COM and OSINST.IMG in the same DOS folder.\r\n  Boot DOS, change to that directory, and run OSINST.COM.\r\n  The utility writes the raw installer image to the selected BIOS disk.\r\n\r\nFor safety, target selection starts at BIOS disk 0x81 instead of 0x80.\r\n\r\n"
install_intro_text:
    .asciz  "This writes the fallback installer image file to the selected BIOS disk.\r\nThe target disk contents will be overwritten sector-by-sector.\r\n\r\n"
confirm_text:
    .asciz  "WARNING: continue only if you selected the correct target.\r\nPress Y to start or any other key to cancel.\r\n"
copy_begin_text:
    .asciz  "Writing installer image to target disk...\r\nProgress: "
copy_success_text:
    .asciz  "Install image write complete.\r\nYou can now boot the target disk into the fallback installer.\r\n\r\n"
copy_failed_text:
    .asciz  "\r\nInstall image write failed.\r\n\r\n"
no_target_text:
    .asciz  "No alternate BIOS target disk is available.\r\n\r\n"
target_updated_text:
    .asciz  "Target disk updated.\r\n\r\n"
target_missing_text:
    .asciz  "No alternate BIOS disk could be selected.\r\n\r\n"
file_missing_text:
    .asciz  "\r\nOSINST.IMG was not found in the current DOS directory.\r\n\r\n"
file_read_error_text:
    .asciz  "\r\nDOS file read failed while reading OSINST.IMG.\r\n\r\n"
disk_write_error_text:
    .asciz  "\r\nBIOS disk write failed on the selected target.\r\n\r\n"
press_key_text:
    .asciz  "Press any key to continue...\r\n"
image_file_text:
    .asciz  "Image file:    "
image_filename_text:
    .asciz  IMAGE_FILENAME
target_drive_text:
    .asciz  "Target drive:  "
none_text:
    .asciz  "none\r\n"
crlf_text:
    .asciz  "\r\n"
image_filename_dos:
    .asciz  IMAGE_FILENAME

selected_drive:
    .byte   0
    .byte   0
file_handle:
    .word   0xFFFF
progress_divider:
    .word   0
current_lba:
    .quad   0

dap:
    .byte   0x10
    .byte   0
dap_sector_count:
    .word   1
dap_buffer_offset:
    .word   sector_buffer
dap_buffer_segment:
    .word   0
dap_lba:
    .quad   0

sector_buffer:
    .space  512

stack_space:
    .space  1024
stack_top:
