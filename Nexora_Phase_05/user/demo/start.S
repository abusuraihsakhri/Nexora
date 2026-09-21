.section .text
.code64
.global _user_start
.extern user_main
_user_start:
    xor %rbp, %rbp
    call user_main
1:
    pause
    jmp 1b
