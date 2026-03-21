/*
 * OS next stage BIOS text installer - stage 2
 *
 * A standalone real-mode fallback installer shell. This variant does not boot
 * the kernel, but it can deploy a bundled regular OS system image onto another
 * BIOS disk so the system remains recoverable when the GUI installer path is
 * unavailable.
 */

.code16
.section .text
.global _start

#define BOOT_STAGE2_SEGMENT 0x07E0
#define DISK_BUFFER_SEGMENT 0x2000
#define COPY_CHUNK_SECTORS  4

_start:
    ljmp    $BOOT_STAGE2_SEGMENT, $real_mode_entry

real_mode_entry:
    cli
    mov     %cs, %ax
    mov     %ax, %ds
    mov     %ax, %es
    xor     %ax, %ax
    mov     %ax, %ss
    mov     $0x9000, %sp
    sti

    mov     %dl, boot_drive
    movb    $0, selected_drive

    mov     $0x0003, %ax
    int     $0x10

main_menu:
    call    clear_screen
    mov     $title_text, %si
    call    print_string
    mov     $subtitle_text, %si
    call    print_string
    call    print_drive_summary
    mov     $menu_text, %si
    call    print_string
    call    read_key

    cmp     $'1', %al
    je      deploy_installer_media
    cmp     $'2', %al
    je      select_target_disk
    cmp     $'3', %al
    je      apply_bootloader_menu
    cmp     $'4', %al
    je      show_disk_info
    cmp     $'5', %al
    je      show_shell
    cmp     $'6', %al
    je      reboot_system
    cmp     $27, %al
    je      halt_system
    jmp     main_menu

deploy_installer_media:
    call    clear_screen
    mov     $install_intro_text, %si
    call    print_string
    call    print_drive_summary

    movb    selected_drive, %al
    test    %al, %al
    jz      no_target_disk
    cmpb    boot_drive, %al
    je      no_target_disk

    mov     $confirm_text, %si
    call    print_string
    call    read_key
    cmp     $'y', %al
    je      do_clone
    cmp     $'Y', %al
    je      do_clone
    jmp     main_menu

do_clone:
    call    clear_screen
    mov     $copy_begin_text, %si
    call    print_string
    call    print_drive_summary
    call    clone_system_image
    jc      clone_failed

    mov     $copy_success_text, %si
    call    print_string
    call    wait_for_keypress
    jmp     main_menu

clone_failed:
    mov     $copy_failed_text, %si
    call    print_string
    call    wait_for_keypress
    jmp     main_menu

no_target_disk:
    mov     $no_target_text, %si
    call    print_string
    call    wait_for_keypress
    jmp     main_menu

apply_bootloader_menu:
    call    clear_screen
    mov     $bootloader_intro_text, %si
    call    print_string
    call    print_drive_summary

    movb    selected_drive, %al
    test    %al, %al
    jz      no_target_disk
    cmpb    boot_drive, %al
    je      no_target_disk

    mov     $bootloader_confirm_text, %si
    call    print_string
    call    read_key
    cmp     $'y', %al
    je      do_apply_bootloader
    cmp     $'Y', %al
    je      do_apply_bootloader
    jmp     main_menu

do_apply_bootloader:
    call    clear_screen
    mov     $bootloader_begin_text, %si
    call    print_string
    call    print_drive_summary
    call    apply_bootloader_to_target
    jc      bootloader_failed

    mov     $bootloader_success_text, %si
    call    print_string
    call    wait_for_keypress
    jmp     main_menu

bootloader_failed:
    mov     $bootloader_failed_text, %si
    call    print_string
    call    wait_for_keypress
    jmp     main_menu

select_target_disk:
    call    select_next_target
    call    clear_screen
    jc      select_target_none
    mov     $target_updated_text, %si
    call    print_string
    call    print_drive_summary
    call    wait_for_keypress
    jmp     main_menu

select_target_none:
    mov     $target_missing_text, %si
    call    print_string
    call    wait_for_keypress
    jmp     main_menu

show_disk_info:
    call    clear_screen
    mov     $disk_info_text, %si
    call    print_string
    call    print_drive_summary
    mov     $disk_info_hint_text, %si
    call    print_string
    call    wait_for_keypress
    jmp     main_menu

show_shell:
    call    clear_screen
    mov     $shell_text, %si
    call    print_string
shell_loop:
    mov     $prompt_text, %si
    call    print_string
    call    read_key
    cmp     $'h', %al
    je      shell_help
    cmp     $'i', %al
    je      show_disk_info
    cmp     $'n', %al
    je      shell_next_disk
    cmp     $'b', %al
    je      apply_bootloader_menu
    cmp     $'d', %al
    je      deploy_installer_media
    cmp     $'r', %al
    je      reboot_system
    cmp     $'q', %al
    je      main_menu
    mov     $unknown_text, %si
    call    print_string
    jmp     shell_loop

shell_help:
    mov     $shell_help_text, %si
    call    print_string
    jmp     shell_loop

shell_next_disk:
    call    select_next_target
    jc      shell_no_disk
    mov     $target_updated_text, %si
    call    print_string
    jmp     shell_loop

shell_no_disk:
    mov     $target_missing_text, %si
    call    print_string
    jmp     shell_loop

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
.print_loop:
    lodsb
    test    %al, %al
    jz      .print_done
    mov     $0x0E, %ah
    mov     $0x0007, %bx
    int     $0x10
    jmp     .print_loop
.print_done:
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
    jb      .digit
    add     $'A' - 10, %al
    jmp     print_char
.digit:
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

print_drive_line:
    call    print_string
    movb    (%bx), %al
    test    %al, %al
    jz      .none
    call    print_drive_id
    mov     $crlf_text, %si
    call    print_string
    ret
.none:
    mov     $none_text, %si
    call    print_string
    ret

print_drive_summary:
    push    %bx
    mov     $boot_drive_text, %si
    mov     $boot_drive, %bx
    call    print_drive_line
    mov     $target_drive_text, %si
    mov     $selected_drive, %bx
    call    print_drive_line
    pop     %bx
    ret

probe_drive:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    mov     $0x55AA, %bx
    mov     $0x4100, %ax
    int     $0x13
    jc      probe_drive_fail
    cmp     $0xAA55, %bx
    jne     probe_drive_fail
    test    $0x0001, %cx
    jz      probe_drive_fail
    clc
    jmp     probe_drive_done
probe_drive_fail:
    stc
probe_drive_done:
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

find_default_target:
    movb    $0, selected_drive
    movb    $0x80, %bl
find_default_target_loop:
    cmpb    $0x88, %bl
    jae     find_default_target_done
    cmpb    boot_drive, %bl
    je      find_default_target_next
    movb    %bl, %dl
    call    probe_drive
    jc      find_default_target_next
    movb    %bl, selected_drive
    ret
find_default_target_next:
    inc     %bl
    jmp     find_default_target_loop
find_default_target_done:
    ret

select_next_target:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    movb    selected_drive, %bl
    cmpb    $0x80, %bl
    jae     select_next_target_cycle
    movb    $0x7F, %bl
select_next_target_cycle:
    movb    $8, %cl
select_next_target_try:
    inc     %bl
    cmpb    $0x88, %bl
    jb      select_next_target_check
    movb    $0x80, %bl
select_next_target_check:
    cmpb    boot_drive, %bl
    je      select_next_target_continue
    movb    %bl, %dl
    call    probe_drive
    jc      select_next_target_continue
    movb    %bl, selected_drive
    clc
    jmp     select_next_target_done
select_next_target_continue:
    dec     %cl
    jnz     select_next_target_try
    movb    $0, selected_drive
    stc
select_next_target_done:
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

clone_system_image:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    push    %si

    movb    boot_drive, %dl
    call    probe_drive
    jc      clone_system_image_fail
    movb    selected_drive, %dl
    call    probe_drive
    jc      clone_system_image_fail

    movl    system_image_start_lba, %eax
    testl   %eax, %eax
    jz      clone_system_image_fail
    movl    %eax, current_lba
    movl    $0, current_lba + 4

    movl    system_image_total_sectors, %eax
    testl   %eax, %eax
    jz      clone_system_image_fail
    movl    %eax, remaining_sectors

clone_system_image_copy_loop:
    movl    remaining_sectors, %eax
    testl   %eax, %eax
    jz      clone_system_image_success
    cmpw    $COPY_CHUNK_SECTORS, %ax
    jbe     clone_system_image_use_remaining
    movw    $COPY_CHUNK_SECTORS, dap_sector_count
    jmp     clone_system_image_read_chunk
clone_system_image_use_remaining:
    movw    %ax, dap_sector_count

clone_system_image_read_chunk:
    movl    current_lba, %eax
    movl    %eax, dap_lba
    movl    current_lba + 4, %eax
    movl    %eax, dap_lba + 4

    movb    boot_drive, %dl
    mov     $dap, %si
    mov     $0x4200, %ax
    int     $0x13
    jc      clone_system_image_fail

    movb    selected_drive, %dl
    mov     $dap, %si
    mov     $0x4300, %ax
    int     $0x13
    jc      clone_system_image_fail

    movw    dap_sector_count, %ax
    addl    %eax, current_lba
    adcl    $0, current_lba + 4
    subl    %eax, remaining_sectors

    mov     $'.', %al
    call    print_char
    jmp     clone_system_image_copy_loop

clone_system_image_success:
    mov     $crlf_text, %si
    call    print_string
    clc
    jmp     clone_system_image_done

clone_system_image_fail:
    stc

clone_system_image_done:
    pop     %si
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

apply_bootloader_to_target:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    push    %si

    movb    boot_drive, %dl
    call    probe_drive
    jc      apply_bootloader_fail
    movb    selected_drive, %dl
    call    probe_drive
    jc      apply_bootloader_fail

    movl    system_image_start_lba, %eax
    testl   %eax, %eax
    jz      apply_bootloader_fail
    movl    %eax, dap_lba
    movl    $0, dap_lba + 4
    movw    $1, dap_sector_count

    movb    boot_drive, %dl
    mov     $dap, %si
    mov     $0x4200, %ax
    int     $0x13
    jc      apply_bootloader_fail

    xorl    %eax, %eax
    movl    %eax, dap_lba
    movl    %eax, dap_lba + 4

    movb    selected_drive, %dl
    mov     $dap, %si
    mov     $0x4300, %ax
    int     $0x13
    jc      apply_bootloader_fail

    clc
    jmp     apply_bootloader_done

apply_bootloader_fail:
    stc

apply_bootloader_done:
    pop     %si
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

reboot_system:
    movw    $0x1234, 0x472
    ljmp    $0xFFFF, $0x0000

halt_system:
    cli
.halt:
    hlt
    jmp     .halt

title_text:
    .asciz  "OS next stage DOS System Installer\r\n"
subtitle_text:
    .asciz  "16-bit real mode fallback path\r\nDeploys the bundled regular OS system image to another BIOS disk\r\n\r\n"
menu_text:
    .asciz  "  1. Deploy regular system image\r\n  2. Change target disk\r\n  3. Apply bootloader to disk\r\n  4. Show disk info\r\n  5. Enter installer shell\r\n  6. Reboot\r\n  ESC. Halt\r\n\r\nSelect an option: "
install_intro_text:
    .asciz  "This fallback path writes the bundled regular OS system image\r\nonto another BIOS disk. That target can then boot OS next stage\r\neven when the GUI or EFI install flow is unavailable.\r\n\r\n"
bootloader_intro_text:
    .asciz  "This writes only the bundled regular system boot sector to the\r\nselected target disk. Use it to repair bootability without rewriting\r\nthe full system image.\r\n\r\n"
confirm_text:
    .asciz  "\r\nWARNING: target contents will be overwritten.\r\nPress Y to continue or any other key to cancel.\r\n"
bootloader_confirm_text:
    .asciz  "\r\nWARNING: this overwrites the target disk boot sector only.\r\nPress Y to continue or any other key to cancel.\r\n"
copy_begin_text:
    .asciz  "Deploying regular system image to target disk...\r\nProgress: "
bootloader_begin_text:
    .asciz  "Applying bootloader to target disk...\r\n"
copy_success_text:
    .asciz  "Deployment complete.\r\nBoot the target disk to start OS next stage.\r\n\r\n"
bootloader_success_text:
    .asciz  "Bootloader applied successfully.\r\n\r\n"
copy_failed_text:
    .asciz  "\r\nDeployment failed.\r\nCheck BIOS disk access, bundled image payload, and choose a different target.\r\n\r\n"
bootloader_failed_text:
    .asciz  "\r\nBootloader apply failed.\r\nCheck BIOS disk access and bundled image payload.\r\n\r\n"
no_target_text:
    .asciz  "No alternate BIOS target disk is available.\r\nAttach another disk and try again.\r\n\r\n"
target_updated_text:
    .asciz  "Target disk updated.\r\n\r\n"
target_missing_text:
    .asciz  "No alternate BIOS disk could be selected.\r\n\r\n"
disk_info_text:
    .asciz  "Detected installer drives\r\n\r\n"
disk_info_hint_text:
    .asciz  "Only BIOS disks with Int13h extensions are offered as targets.\r\nThe source boot drive is never reused as the deployment target.\r\n\r\n"
shell_text:
    .asciz  "Installer shell\r\nCommands: h=help, i=info, n=next disk, d=deploy, r=reboot, q=back\r\n\r\n"
shell_help_text:
    .asciz  "\r\nHelp:\r\n  h  Show this help\r\n  i  Show current disk info\r\n  n  Select next target disk\r\n  b  Apply bundled bootloader to target disk\r\n  d  Deploy bundled regular system image\r\n  r  Reboot\r\n  q  Return to menu\r\n\r\n"
prompt_text:
    .asciz  "> "
unknown_text:
    .asciz  "\r\nUnknown command.\r\n"
press_key_text:
    .asciz  "Press any key to continue...\r\n"
boot_drive_text:
    .asciz  "Boot drive:   "
target_drive_text:
    .asciz  "Target drive: "
none_text:
    .asciz  "none\r\n"
crlf_text:
    .asciz  "\r\n"

boot_drive:
    .byte   0
selected_drive:
    .byte   0
remaining_sectors:
    .long   0
current_lba:
    .quad   0
system_image_start_lba:
    .long   0x13572468
    .long   0
system_image_total_sectors:
    .long   0x24681357

dap:
    .byte   0x10
    .byte   0
dap_sector_count:
    .word   COPY_CHUNK_SECTORS
    .word   0x0000
    .word   DISK_BUFFER_SEGMENT
dap_lba:
    .quad   0
