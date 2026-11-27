!
! Cheat Codes for Pseudo Saturn Kai
! by cafe-alpha and Yabause Team.
! WWW: http://ppcenter.free.fr/pskai/
!
! See LICENSE file for details.
!

.text
.align 2

!; Cheat codes handler : in-RAM based implementation
!;
!; This module is executed by cheat codes interrupt bootstrap
!; handler, with the following parameters provided :
!;  - R4 : pointer to configuration memory
!;
!; Note: The following registers are saved/restored on
!;       caller (asm_cheat_bootstrap_ihr) side, hence
!;       don't need to be stacked/restored :
!;        - R0-R4
!;        - PR

.global _asm_cheatcodes_hdlr_ram
.type _asm_cheatcodes_hdlr_ram, @function
_asm_cheatcodes_hdlr_ram:


!;  /* Check for enable code. */
!;  unsigned short* enable_ptr = (unsigned short*)cfg->enable_addr;
    mov.l   @(48,r4),r1
    mov r4,r3
!;  if(enable_ptr != NULL)
    tst r1,r1
    bt.s    enable_code_skip
    add #32,r3
!;  if((*enable_ptr) != cfg->enable_val)
    mov.w   @r1,r2
    mov.w   @(20,r3),r0
    extu.w  r2,r2
    extu.w  r0,r1

!;  /* Memory contents at enable code address doesn't
!;   * matches : don't process user code(s)
!;   */
!;  return;
    cmp/eq  r1,r2
    bf  codes_process_finish

enable_code_skip:
!;  /* Apply cached codes. */
!;  ct_code_t* ct = (ct_code_t*)((unsigned char*)(cfg) + sizeof(ct_config_t));
!;  unsigned short user_codes_cnt = cfg->user_codes_cnt;
    mov.w   @(26,r3),r0

    add #64,r4
    extu.w  r0,r1
!;  for(i=0; i<user_codes_cnt; i++)
    add #1,r1

codes_process_loop:
    dt  r1
    bt  codes_process_finish

!;  /*
!;   * Enabled codes are not copied in cache, 
!;   * hence not checked here.
!;   */

    mov.b   @(6,r4),r0
    extu.b  r0,r0
    cmp/eq  #1,r0

    bf.s    ct_command_03
    cmp/eq  #3,r0


ct_command_01:
!;  if(ct->command == 0x01)
!;  /* Continuous 16 bit word write. */
!;   *((unsigned short*)ct->addr) = ct->code;
!;   */
    mov.l   @r4,r2
    mov.w   @(4,r4),r0
    bra codes_exec_done

    mov.w   r0,@r2
    bf  codes_exec_done


ct_command_03:
!;  else if(ct->command == 0x03)
!;  /* Continuous 8 bit byte write with lower 8 bits of code value. */
!;   *((unsigned char*)ct->addr) = (unsigned char)ct->code;
!;   */
    mov.l   @r4,r2
    mov.w   @(4,r4),r0
    mov.b   r0,@r2

!;  /* Ignore non-official "8" code in in-RAM cheat codes handler. */


codes_exec_done:
!;  /* Jump to next cheat code. */
!;  ct++;
    bra codes_process_loop
    add #8,r4


codes_process_finish:
    rts
    nop


!; Dummy data, in order to verify where the cheat codes handler ends.
.align 4
_cheatcodes_hdlr_ram_dmyval:    .long 0xCAFEA1FA


