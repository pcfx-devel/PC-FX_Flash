/* 
 * Flashfuncs.s - Routines to write and erase flash memory
*/

#=============================
# Macros 
#=============================

.macro  movw data, reg1
        movhi   hi(\data),r0,\reg1
        movea   lo(\data),\reg1,\reg1
        /* could be made smarter to omit if not required */
.endm

#===============================

     .global _flash_erase_sector
     .global _flash_write


.equiv r_tmp,    r8
.equiv r_cmd,    r9
.equiv r_base1,  r10
.equiv r_base2,  r11

#
#  flash_erase_sector(addr);
#
#    Erases a 4KB sector of a SST39SF040
#    'addr' points to any address within the memory range
#
_flash_erase_sector:
    #
    # r6   will enter with the target address (should be in 0xE8xxxxxx range)
    #
    mov  lp, r18

    movw 0xe800aaaa, r_base1     # base external + 0x5555 offset (times 2, as A0 is missing)
    movw 0xe8005554, r_base2     # base external + 0x2AAA offset (times 2, as A0 is missing)

    movw 0xAA, r_cmd             # command byte 1
    st.b r_cmd, 0[r_base1]
    movw 0x55, r_cmd             # command byte 2
    st.b r_cmd, 0[r_base2]
    movw 0x80, r_cmd             # erase major command byte
    st.b r_cmd, 0[r_base1]

    movw 0xAA, r_cmd             # Sector Erase command - subcommand byte 1
    st.b r_cmd, 0[r_base1]       # 0x5555 offset (times 2), relative to r_base2
    movw 0x55, r_cmd             # subcommand byte 2
    st.b r_cmd, 0[r_base2]

    movw 0x30, r_cmd             # sector erase subcommand, in sector to be erased
    st.b r_cmd, 0[r6]            # save to the location in the appropriate sector address

    movw 0xFF, r7                # erased data should show as 0xFF when complete

eraseloop:    
    ld.b 0[r6], r_cmd            # check the value at the original location
    and  r7, r_cmd               # ensure only lowest 8 bits are relevant

    cmp  r7, r_cmd               # loop if it's not done yet
    bne  eraseloop

    mov  r18, lp
    jmp  [lp]
    
#------------------------------------

#
#  flash_write(addr, data);
#
#    Writes data to a memory location in a SST39SF040
#    'addr' points to any address within the memory range
#           (Note: must not have been written previously)
#    'data' is the value to write at that location
#
_flash_write:
    #
    # r6   will enter with the target address (should be in 0xE8xxxxxx range)
    # r7   will enter with the target data value to be written
    #
    mov  lp, r18

    movw 0xe800aaaa, r_base1     # base external + 0x5555 offset (times 2, as A0 is missing)
    movw 0xe8005554, r_base2     # base external + 0x2AAA offset (times 2, as A0 is missing)

    movw 0xAA, r_cmd             # command byte 1
    st.b r_cmd, 0[r_base1]
    movw 0x55, r_cmd             # command byte 2
    st.b r_cmd, 0[r_base2]
    movw 0xA0, r_cmd             # write byte command byte
    st.b r_cmd, 0[r_base1]

    movw 0xFF, r_tmp             # ensure only lowest 8 bits are relevant
    and  r_tmp, r7

    st.b r7, 0[r6]

checkloop:    
    ld.b 0[r6], r_cmd            # check the value at the original location
    and  r_tmp, r_cmd            # ensure only lowest 8 bits are relevant

    cmp  r7, r_cmd               # loop if it's not done yet
    bne  checkloop

    mov  r18, lp
    jmp  [lp]


#-----------------------------------
