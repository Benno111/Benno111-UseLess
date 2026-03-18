/*
 * OS next stage BIOS text installer - stage 2
 *
 * A standalone real-mode text installer shell intended as a DOS-style
 * fallback installer path. This variant does not boot the kernel.
 */

.code16
.section .text
.global _start

_start:
    cli
    xor     %ax, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss
    mov     $0x9000, %sp
    sti

    mov     $0x0003, %ax
    int     $0x10

main_menu:
    call    clear_screen
    mov     $title_text, %si
    call    print_string
    mov     $subtitle_text, %si
    call    print_string
    mov     $menu_text, %si
    call    print_string
    call    read_key

    cmp     $'1', %al
    je      show_install
    cmp     $'2', %al
    je      show_shell
    cmp     $'3', %al
    je      reboot_system
    cmp     $27, %al
    je      halt_system
    jmp     main_menu

show_install:
    call    clear_screen
    mov     $install_text, %si
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

reboot_system:
    movw    $0x1234, 0x472
    ljmp    $0xFFFF, $0x0000

halt_system:
    cli
.halt:
    hlt
    jmp     .halt

title_text:
    .asciz  "OS next stage DOS-style Installer\r\n"
subtitle_text:
    .asciz  "16-bit real mode fallback installer\r\n64-bit kernel handoff disabled\r\n\r\n"
menu_text:
    .asciz  "  1. Install system image (stub)\r\n  2. Enter installer shell\r\n  3. Reboot\r\n  ESC. Halt\r\n\r\nSelect an option: "
install_text:
    .asciz  "This installer variant runs without the kernel and is intended\r\nas a DOS-style fallback path.\r\n\r\nCurrent status:\r\n- BIOS text mode is working\r\n- real-mode control flow is working\r\n- 64-bit kernel handoff is disabled in this variant\r\n- integration hooks for full disk install can be added here next\r\n\r\n"
shell_text:
    .asciz  "Installer shell\r\nCommands: h=help, r=reboot, q=back\r\n\r\n"
shell_help_text:
    .asciz  "\r\nHelp:\r\n  h  Show this help\r\n  r  Reboot\r\n  q  Return to menu\r\n\r\n"
prompt_text:
    .asciz  "> "
unknown_text:
    .asciz  "\r\nUnknown command.\r\n"
press_key_text:
    .asciz  "Press any key to return...\r\n"
