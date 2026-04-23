/*
 * OS8 DOS Setup – Stage 2 BIOS Installer with Text-Mode GUI
 *
 * Provides a graphical text-mode installer interface using BIOS INT 10h
 * with coloured panels, CP437 box-drawing borders, and arrow-key navigation.
 *
 * Screen layout (80×25 VGA text mode 3):
 *
 *   Row  0  ████████████████  OS8 DOS SETUP  ████████████████  title bar
 *   Row  1..4  (background)
 *   Row  5  ╔══════════════════════════════════════════════════╗
 *   Row  6  ║             OS8 DOS SETUP PROGRAM               ║
 *   Row  7  ╠══════════════════════════════════════════════════╣
 *   Row  8  ║  Install OS8                                     ║
 *   Row  9  ║  Change target drive                             ║
 *   Row 10  ║  Shell                                           ║
 *   Row 11  ║  Reboot                                          ║
 *   Row 12  ╠══════════════════════════════════════════════════╣
 *   Row 13  ║  Target drive: 0xXX                              ║
 *   Row 14  ╚══════════════════════════════════════════════════╝
 *   Row 15..23  (background)
 *   Row 24  ████  Up/Dn=Select   Enter=OK   Esc=Reboot  ████  status bar
 *
 * Navigation: Up/Down arrows move selection, Enter activates, Esc reboots.
 */

.code16
.global real_mode_entry

/* ── VGA text-mode colour attributes: (background<<4)|foreground ────────
 *   background/foreground nibbles: 0=blk 1=blu 2=grn 3=cyn 4=red 5=mag
 *                                  6=brn 7=lgry  (+8 for bright fg)        */
#define C_BG       0x30   /* cyan bg,   black fg   – main background      */
#define C_TITLE    0x1F   /* blue bg,   bright wht – title / status bars  */
#define C_WIN      0x70   /* lgray bg,  black fg   – window interior      */
#define C_SELECTED 0x1F   /* blue bg,   bright wht – highlighted item     */
#define C_ITEM     0x70   /* lgray bg,  black fg   – normal menu item     */

/* ── CP437 box-drawing characters ───────────────────────────────────── */
#define CH_TL  0xC9   /* ╔ */
#define CH_TR  0xBB   /* ╗ */
#define CH_BL  0xC8   /* ╚ */
#define CH_BR  0xBC   /* ╝ */
#define CH_H   0xCD   /* ═ */
#define CH_V   0xBA   /* ║ */
#define CH_ML  0xCC   /* ╠ */
#define CH_MR  0xB9   /* ╣ */

/* ── Window geometry (all values are absolute screen coordinates) ─────
 *   Window is 52 chars wide and occupies rows 5-14, cols 14-65.          */
#define WIN_ROW_TOP  5
#define WIN_ROW_BOT  14
#define WIN_COL_L    14
#define WIN_COL_R    65
#define WIN_W        52    /* WIN_COL_R - WIN_COL_L + 1 */
#define WIN_INNER_W  50    /* WIN_W - 2 (between the vertical borders)   */

/* Absolute row assignments within the window */
#define ROW_TOP    5   /* ╔═══╗  top border                              */
#define ROW_WTITLE 6   /* ║   ║  window title row                        */
#define ROW_SEP1   7   /* ╠═══╣  separator after title                   */
#define ROW_ITEM0  8   /* ║   ║  menu item 0: Install OS8                */
#define ROW_ITEM1  9   /* ║   ║  menu item 1: Change target drive        */
#define ROW_ITEM2 10   /* ║   ║  menu item 2: Shell                      */
#define ROW_ITEM3 11   /* ║   ║  menu item 3: Reboot                     */
#define ROW_SEP2  12   /* ╠═══╣  separator after menu                    */
#define ROW_INFO  13   /* ║   ║  target drive info                       */
#define ROW_BOT   14   /* ╚═══╝  bottom border                           */

#define MENU_COUNT 4

.section .text

real_mode_entry:
    cli
    mov     %cs, %ax
    mov     %ax, %ds
    mov     %ax, %es
    xor     %ax, %ax
    mov     %ax, %ss
    mov     $0x9000, %sp
    cld
    sti

    mov     %dl, boot_drive
    movb    $0, selected_drive
    movb    $0, menu_sel
    call    find_default_target

    /* Set 80×25 colour text mode (clears screen as a side-effect) */
    mov     $0x0003, %ax
    int     $0x10

    /* Hide hardware cursor – scan-lines 32–0 = invisible shape */
    mov     $0x0100, %ax
    mov     $0x2000, %cx
    int     $0x10

    call    draw_screen

/* ── Main keyboard input loop ────────────────────────────────────────── */
input_loop:
    xor     %ax, %ax
    int     $0x16               /* wait for keystroke: AH=scan AL=ASCII */

    test    %al, %al
    jz      ext_key             /* AL==0 → extended / special key */

    cmp     $0x0D, %al          /* Enter */
    je      do_enter
    jmp     input_loop

ext_key:
    cmp     $0x48, %ah          /* ↑  scan code 0x48 */
    je      key_up
    cmp     $0x50, %ah          /* ↓  scan code 0x50 */
    je      key_down
    jmp     input_loop

key_up:
    movb    menu_sel, %al
    test    %al, %al
    jz      input_loop          /* already at top – nothing to do */
    dec     %al
    movb    %al, menu_sel
    call    draw_menu_items
    jmp     input_loop

key_down:
    movb    menu_sel, %al
    cmp     $(MENU_COUNT - 1), %al
    jge     input_loop          /* already at bottom */
    inc     %al
    movb    %al, menu_sel
    call    draw_menu_items
    jmp     input_loop

do_enter:
    movb    menu_sel, %al
    cmp     $0, %al
    je      act_install
    cmp     $1, %al
    je      act_drive
    cmp     $2, %al
    je      act_shell
    /* item 3 = Reboot */
do_reboot:
    int     $0x19
    hlt
    jmp     do_reboot

act_install:
    call    action_install
    call    draw_screen
    jmp     input_loop

act_drive:
    call    action_change_drive
    call    draw_screen
    jmp     input_loop

act_shell:
    call    action_shell
    call    draw_screen
    jmp     input_loop

/* ═══════════════════════════════════════════════════════════════════════
 * SCREEN DRAWING
 * ═══════════════════════════════════════════════════════════════════════ */

draw_screen:
    call    fill_background
    call    draw_title_bar
    call    draw_status_bar
    call    draw_window_frame
    call    draw_window_title_row
    call    draw_menu_items
    call    draw_info_row
    ret

/* Fill rows 1-23 with the cyan background colour */
fill_background:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    mov     $0x0600, %ax        /* INT 10h scroll-up / fill */
    mov     $C_BG, %bh
    mov     $0x0100, %cx        /* top-left:  row 1, col 0  */
    mov     $0x174F, %dx        /* bot-right: row 23, col 79 */
    int     $0x10
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* Title bar: row 0, blue bg with centred "OS8 DOS SETUP" text */
draw_title_bar:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    mov     $0x0600, %ax
    mov     $C_TITLE, %bh
    xor     %cx, %cx            /* row 0, col 0  */
    mov     $0x004F, %dx        /* row 0, col 79 */
    int     $0x10
    /* "OS8 DOS SETUP" = 13 chars → start at col (80-13)/2 = 33 */
    mov     $0, %dh
    mov     $33, %dl
    call    gotoxy
    mov     $str_title_bar, %si
    mov     $C_TITLE, %bl
    call    print_str_attr
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* Status bar: row 24, same style as title bar */
draw_status_bar:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    mov     $0x0600, %ax
    mov     $C_TITLE, %bh
    mov     $0x1800, %cx        /* row 24, col 0  */
    mov     $0x184F, %dx        /* row 24, col 79 */
    int     $0x10
    mov     $24, %dh
    mov     $2, %dl
    call    gotoxy
    mov     $str_status_bar, %si
    mov     $C_TITLE, %bl
    call    print_str_attr
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* Draw the window: fill interior, all four borders, two separators */
draw_window_frame:
    push    %ax
    push    %bx
    push    %cx
    push    %dx

    /* 1. Flood-fill the whole window rectangle with C_WIN */
    mov     $0x0600, %ax
    mov     $C_WIN, %bh
    mov     $(WIN_ROW_TOP << 8 | WIN_COL_L), %cx
    mov     $(WIN_ROW_BOT << 8 | WIN_COL_R), %dx
    int     $0x10

    /* 2. Top border: ╔ + 50×═ + ╗ */
    mov     $ROW_TOP, %dh
    mov     $WIN_COL_L, %dl
    call    gotoxy
    mov     $CH_TL, %al
    mov     $C_WIN, %bl
    mov     $1, %cx
    call    put_char_n

    mov     $ROW_TOP, %dh
    mov     $(WIN_COL_L + 1), %dl
    call    gotoxy
    mov     $CH_H, %al
    mov     $C_WIN, %bl
    mov     $WIN_INNER_W, %cx
    call    put_char_n

    mov     $ROW_TOP, %dh
    mov     $WIN_COL_R, %dl
    call    gotoxy
    mov     $CH_TR, %al
    mov     $C_WIN, %bl
    mov     $1, %cx
    call    put_char_n

    /* 3. Bottom border: ╚ + 50×═ + ╝ */
    mov     $ROW_BOT, %dh
    mov     $WIN_COL_L, %dl
    call    gotoxy
    mov     $CH_BL, %al
    mov     $C_WIN, %bl
    mov     $1, %cx
    call    put_char_n

    mov     $ROW_BOT, %dh
    mov     $(WIN_COL_L + 1), %dl
    call    gotoxy
    mov     $CH_H, %al
    mov     $C_WIN, %bl
    mov     $WIN_INNER_W, %cx
    call    put_char_n

    mov     $ROW_BOT, %dh
    mov     $WIN_COL_R, %dl
    call    gotoxy
    mov     $CH_BR, %al
    mov     $C_WIN, %bl
    mov     $1, %cx
    call    put_char_n

    /* 4. Separator 1 after window title: ╠ + 50×═ + ╣ */
    mov     $ROW_SEP1, %dh
    mov     $WIN_COL_L, %dl
    call    gotoxy
    mov     $CH_ML, %al
    mov     $C_WIN, %bl
    mov     $1, %cx
    call    put_char_n

    mov     $ROW_SEP1, %dh
    mov     $(WIN_COL_L + 1), %dl
    call    gotoxy
    mov     $CH_H, %al
    mov     $C_WIN, %bl
    mov     $WIN_INNER_W, %cx
    call    put_char_n

    mov     $ROW_SEP1, %dh
    mov     $WIN_COL_R, %dl
    call    gotoxy
    mov     $CH_MR, %al
    mov     $C_WIN, %bl
    mov     $1, %cx
    call    put_char_n

    /* 5. Separator 2 after menu items: ╠ + 50×═ + ╣ */
    mov     $ROW_SEP2, %dh
    mov     $WIN_COL_L, %dl
    call    gotoxy
    mov     $CH_ML, %al
    mov     $C_WIN, %bl
    mov     $1, %cx
    call    put_char_n

    mov     $ROW_SEP2, %dh
    mov     $(WIN_COL_L + 1), %dl
    call    gotoxy
    mov     $CH_H, %al
    mov     $C_WIN, %bl
    mov     $WIN_INNER_W, %cx
    call    put_char_n

    mov     $ROW_SEP2, %dh
    mov     $WIN_COL_R, %dl
    call    gotoxy
    mov     $CH_MR, %al
    mov     $C_WIN, %bl
    mov     $1, %cx
    call    put_char_n

    /* 6. Vertical borders ║ for every non-separator interior row */
    call    draw_side_borders

    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* Draw ║ at left and right columns for interior rows */
draw_side_borders:
    push    %ax
    push    %bx
    push    %cx
    push    %dx

    /* ROW_WTITLE = 6 */
    mov     $ROW_WTITLE, %dh
    mov     $WIN_COL_L, %dl
    call    gotoxy
    mov     $CH_V, %al
    mov     $C_WIN, %bl
    mov     $1, %cx
    call    put_char_n
    mov     $WIN_COL_R, %dl
    call    gotoxy
    call    put_char_n              /* AL, BL, CX unchanged */

    /* ROW_ITEM0 … ROW_ITEM3 = 8 … 11 */
    mov     $ROW_ITEM0, %dh
dsb_loop:
    mov     $WIN_COL_L, %dl
    call    gotoxy
    mov     $CH_V, %al
    mov     $C_WIN, %bl
    mov     $1, %cx
    call    put_char_n
    mov     $WIN_COL_R, %dl
    call    gotoxy
    call    put_char_n
    inc     %dh
    cmp     $(ROW_ITEM3 + 1), %dh
    jl      dsb_loop

    /* ROW_INFO = 13 */
    mov     $ROW_INFO, %dh
    mov     $WIN_COL_L, %dl
    call    gotoxy
    mov     $CH_V, %al
    mov     $C_WIN, %bl
    mov     $1, %cx
    call    put_char_n
    mov     $WIN_COL_R, %dl
    call    gotoxy
    call    put_char_n

    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* Window title centred on ROW_WTITLE */
draw_window_title_row:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    /* "OS8 DOS SETUP PROGRAM" = 21 chars
     * centre col = WIN_COL_L + 1 + (WIN_INNER_W - 21) / 2 = 29 */
    mov     $ROW_WTITLE, %dh
    mov     $(WIN_COL_L + 1 + (WIN_INNER_W - 21) / 2), %dl
    call    gotoxy
    mov     $str_win_title, %si
    mov     $C_WIN, %bl
    call    print_str_attr
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* Redraw all four menu item rows with correct highlight.
 * Safe to call any time the selection changes.                           */
draw_menu_items:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    push    %si

    movb    $0, item_idx
dmi_loop:
    movb    item_idx, %al
    cmp     $MENU_COUNT, %al
    jge     dmi_done

    /* DH = row for this item */
    movb    item_idx, %dh
    addb    $ROW_ITEM0, %dh

    /* Choose colour: selected (C_SELECTED) or normal (C_ITEM) */
    movb    menu_sel, %cl
    cmp     %cl, %al
    je      dmi_selected
    mov     $C_ITEM, %bl
    jmp     dmi_color_set
dmi_selected:
    mov     $C_SELECTED, %bl
dmi_color_set:
    /* Flood-fill entire inner row with the chosen colour */
    mov     $(WIN_COL_L + 1), %dl
    call    gotoxy
    mov     $' ', %al
    mov     $WIN_INNER_W, %cx
    call    put_char_n

    /* Print item text indented by 2 extra spaces */
    movb    item_idx, %dh
    addb    $ROW_ITEM0, %dh
    mov     $(WIN_COL_L + 3), %dl
    call    gotoxy

    /* Load string pointer from word table using BX (valid 16-bit base reg) */
    push    %bx             /* save BL = colour */
    xor     %bx, %bx
    movb    item_idx, %bl
    add     %bx, %bx        /* BX = index × 2 */
    mov     str_menu_items(%bx), %si
    pop     %bx             /* restore BL = colour */

    call    print_str_attr      /* BL still holds the chosen colour */

    incb    item_idx
    jmp     dmi_loop
dmi_done:
    pop     %si
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* Redraw the target-drive info row (ROW_INFO) */
draw_info_row:
    push    %ax
    push    %bx
    push    %cx
    push    %dx
    push    %si

    /* Clear the inner area first */
    mov     $ROW_INFO, %dh
    mov     $(WIN_COL_L + 1), %dl
    call    gotoxy
    mov     $' ', %al
    mov     $C_WIN, %bl
    mov     $WIN_INNER_W, %cx
    call    put_char_n

    /* Print "Target drive: " label */
    mov     $ROW_INFO, %dh
    mov     $(WIN_COL_L + 3), %dl
    call    gotoxy
    mov     $str_target, %si
    mov     $C_WIN, %bl
    call    print_str_attr

    /* Print the drive value or "none" */
    movb    selected_drive, %al
    test    %al, %al
    jz      dir_none
    call    print_hex_byte_attr     /* AL=byte, BL=C_WIN (from above) */
    jmp     dir_done
dir_none:
    mov     $str_none, %si
    call    print_str_attr
dir_done:
    pop     %si
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* ═══════════════════════════════════════════════════════════════════════
 * LOW-LEVEL DRAWING PRIMITIVES
 * ═══════════════════════════════════════════════════════════════════════ */

/* gotoxy – position cursor at DH=row, DL=col; preserves all registers */
gotoxy:
    push    %ax
    push    %bx
    mov     $0x02, %ah
    xor     %bh, %bh
    int     $0x10
    pop     %bx
    pop     %ax
    ret

/* put_char_n – write AL=char with BL=attr, CX times at current cursor.
 * Does NOT advance the cursor.  Preserves AL, BL, CX.                   */
put_char_n:
    push    %ax
    push    %bx
    push    %cx
    mov     $0x09, %ah
    xor     %bh, %bh
    int     $0x10
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* advance_cursor – move cursor one column to the right */
advance_cursor:
    push    %ax
    push    %bx
    push    %dx
    mov     $0x03, %ah          /* INT 10h: get cursor position */
    xor     %bh, %bh
    int     $0x10               /* → DH=row, DL=col */
    inc     %dl
    mov     $0x02, %ah          /* INT 10h: set cursor position */
    int     $0x10
    pop     %dx
    pop     %bx
    pop     %ax
    ret

/* print_str_attr – print NUL-terminated string at current cursor with
 * BL=colour attribute, advancing the cursor after each character.
 * Preserves BL and all other registers.                                  */
print_str_attr:
    push    %ax
    push    %bx
    push    %cx
psa_loop:
    lodsb
    test    %al, %al
    jz      psa_done
    xor     %bh, %bh
    mov     $1, %cx
    mov     $0x09, %ah
    int     $0x10               /* write char + attribute (no advance) */
    call    advance_cursor
    jmp     psa_loop
psa_done:
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* print_hex_byte_attr – print AL as "0xXX" using BL=colour attribute.
 * Cursor must already be positioned.  Preserves AL, BL.                 */
print_hex_byte_attr:
    push    %ax
    push    %bx
    push    %cx
    push    %si

    push    %ax
    mov     $str_hex_prefix, %si
    call    print_str_attr
    pop     %ax

    /* High nibble */
    push    %ax
    shr     $4, %al
    and     $0x0F, %al
    call    print_nibble_attr
    pop     %ax

    /* Low nibble */
    and     $0x0F, %al
    call    print_nibble_attr

    pop     %si
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* print_nibble_attr – print single nibble (AL=0..F) as hex char with BL=attr */
print_nibble_attr:
    push    %ax
    push    %bx
    push    %cx
    and     $0x0F, %al
    cmp     $10, %al
    jb      pna_digit
    add     $('A' - 10), %al
    jmp     pna_emit
pna_digit:
    add     $'0', %al
pna_emit:
    xor     %bh, %bh
    mov     $1, %cx
    mov     $0x09, %ah
    int     $0x10
    call    advance_cursor
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* ═══════════════════════════════════════════════════════════════════════
 * ACTION HANDLERS
 * ═══════════════════════════════════════════════════════════════════════ */

/* action_install – confirm and (placeholder) install */
action_install:
    push    %ax
    push    %bx
    push    %cx
    push    %dx

    /* Clear rows 15-21 for the dialog area */
    mov     $0x0600, %ax
    mov     $C_WIN, %bh
    mov     $0x0F00, %cx        /* row 15, col 0  */
    mov     $0x154F, %dx        /* row 21, col 79 */
    int     $0x10

    mov     $15, %dh
    mov     $5, %dl
    call    gotoxy
    mov     $str_confirm_install, %si
    mov     $C_WIN, %bl
    call    print_str_attr

    xor     %ax, %ax
    int     $0x16               /* wait for keypress */

    cmp     $'y', %al
    je      ai_confirmed
    cmp     $'Y', %al
    je      ai_confirmed
    jmp     ai_done

ai_confirmed:
    movb    selected_drive, %al
    test    %al, %al
    jz      ai_no_target

    mov     $16, %dh
    mov     $5, %dl
    call    gotoxy
    mov     $str_installing, %si
    mov     $C_WIN, %bl
    call    print_str_attr

    /* ── actual install payload would be invoked here ── */

    mov     $17, %dh
    mov     $5, %dl
    call    gotoxy
    mov     $str_install_done, %si
    call    print_str_attr
    xor     %ax, %ax
    int     $0x16
    jmp     ai_done

ai_no_target:
    mov     $16, %dh
    mov     $5, %dl
    call    gotoxy
    mov     $str_no_target, %si
    mov     $C_WIN, %bl
    call    print_str_attr
    xor     %ax, %ax
    int     $0x16

ai_done:
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* action_change_drive – list BIOS disks and let user pick with 1-8 */
action_change_drive:
    push    %ax
    push    %bx
    push    %cx
    push    %dx

    /* Clear rows 15-23 for the dialog */
    mov     $0x0600, %ax
    mov     $C_WIN, %bh
    mov     $0x0F00, %cx
    mov     $0x174F, %dx
    int     $0x10

    mov     $15, %dh
    mov     $5, %dl
    call    gotoxy
    mov     $str_choose_drive, %si
    mov     $C_WIN, %bl
    call    print_str_attr

    /* List drives 0x80-0x87 as "1) 0x80" … "8) 0x87" */
    movb    $0x80, tmp_drive
    movb    $1,    tmp_num

acd_list:
    movb    tmp_num, %al
    cmp     $9, %al
    jge     acd_wait

    movb    tmp_num, %dh
    addb    $15, %dh            /* row 16..23 */
    mov     $7, %dl
    call    gotoxy

    /* Print "N) 0xXX" */
    movb    tmp_num, %al
    add     $'0', %al
    xor     %bh, %bh
    mov     $C_WIN, %bl
    mov     $1, %cx
    mov     $0x09, %ah
    int     $0x10
    call    advance_cursor

    mov     $str_rparen, %si
    mov     $C_WIN, %bl
    call    print_str_attr

    movb    tmp_drive, %al
    call    print_hex_byte_attr

    incb    tmp_drive
    incb    tmp_num
    jmp     acd_list

acd_wait:
    xor     %ax, %ax
    int     $0x16               /* wait for '1'..'8' or ESC */

    cmp     $0x1B, %al
    je      acd_done
    cmp     $'1', %al
    jb      acd_done
    cmp     $'8', %al
    ja      acd_done
    sub     $'1', %al           /* 0..7 */
    add     $0x80, %al          /* 0x80..0x87 */
    movb    %al, selected_drive

acd_done:
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* action_shell – minimal echo shell, ESC to return */
action_shell:
    push    %ax
    push    %bx
    push    %cx
    push    %dx

    /* Restore a visible hardware cursor while in shell mode */
    mov     $0x0100, %ax
    mov     $0x0607, %cx
    int     $0x10

    /* Switch to clean text mode (clears screen) */
    mov     $0x0003, %ax
    int     $0x10

    mov     $0, %dh
    mov     $0, %dl
    call    gotoxy
    mov     $str_shell_intro, %si
    mov     $0x07, %bl
    call    print_str_attr

ash_prompt:
    mov     $str_shell_prompt, %si
    mov     $0x07, %bl
    call    print_str_attr

ash_wait:
    xor     %ax, %ax
    int     $0x16

    cmp     $0x1B, %al          /* ESC → exit shell */
    je      ash_exit
    cmp     $0x0D, %al          /* Enter → new prompt */
    je      ash_prompt
    cmp     $0x20, %al          /* ignore non-printable */
    jb      ash_wait

    /* Echo the character via teletype (auto-advances cursor) */
    mov     $0x0E, %ah
    xor     %bh, %bh
    int     $0x10
    jmp     ash_wait

ash_exit:
    /* Re-hide cursor before returning to the GUI */
    mov     $0x0100, %ax
    mov     $0x2000, %cx
    int     $0x10

    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* ═══════════════════════════════════════════════════════════════════════
 * DRIVE DETECTION (same algorithm as original)
 * ═══════════════════════════════════════════════════════════════════════ */

find_default_target:
    push    %ax
    push    %bx
    push    %cx
    push    %dx

    mov     boot_drive, %dl
    mov     $0x80, %bl
    mov     $0x80, %cl
    add     $8, %cl

fdt_scan:
    cmp     %bl, %dl
    je      fdt_next

    mov     $0x08, %ah
    mov     %bl, %dl
    int     $0x13
    jc      fdt_next

    mov     %bl, selected_drive
    clc
    jmp     fdt_done

fdt_next:
    inc     %bl
    cmp     %cl, %bl
    jb      fdt_scan

    movb    $0, selected_drive
    stc

fdt_done:
    pop     %dx
    pop     %cx
    pop     %bx
    pop     %ax
    ret

/* ═══════════════════════════════════════════════════════════════════════
 * STRING / READ-ONLY DATA
 * ═══════════════════════════════════════════════════════════════════════ */

.section .rodata

str_title_bar:
    .asciz "OS8 DOS SETUP"

str_status_bar:
    .asciz "Up/Dn=Select   Enter=OK   Esc=Reboot"

str_win_title:
    .asciz "OS8 DOS SETUP PROGRAM"

str_menu_item0:  .asciz "Install OS8"
str_menu_item1:  .asciz "Change target drive"
str_menu_item2:  .asciz "Shell"
str_menu_item3:  .asciz "Reboot"

/* Word table of pointers to menu item strings */
str_menu_items:
    .word str_menu_item0
    .word str_menu_item1
    .word str_menu_item2
    .word str_menu_item3

str_target:     .asciz "Target drive: "
str_none:       .asciz "none"
str_hex_prefix: .asciz "0x"
str_rparen:     .asciz ") "

str_confirm_install:
    .asciz "Install OS8 to the selected disk?  Press Y to confirm, any other key to cancel."

str_installing:
    .asciz "Installing OS8 to target disk, please wait..."

str_install_done:
    .asciz "Installation complete.  Press any key to return."

str_no_target:
    .asciz "No target drive selected.  Press any key."

str_choose_drive:
    .asciz "Choose target drive (1-8, ESC=cancel):"

str_shell_intro:
    .asciz "Mini shell  --  type characters, press ESC to return to setup.\r\n\r\n"

str_shell_prompt:
    .asciz "setup> "

/* ═══════════════════════════════════════════════════════════════════════
 * BSS – uninitialised variables
 * ═══════════════════════════════════════════════════════════════════════ */

.section .bss

boot_drive:     .byte 0
selected_drive: .byte 0
menu_sel:       .byte 0
item_idx:       .byte 0
tmp_drive:      .byte 0
tmp_num:        .byte 0
