option casemap:none
EXTERN ProcessCommand:PROC
EXTERN original_command_tail:QWORD
.code
CommandTail PROC
    pushfq
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r12, rsp
    sub rsp, 240h
    and rsp, -16
    fxsave [rsp+20h]
    ; Saved state is 80h bytes; the command is at original RSP+30h.
    mov rcx, [r12+0B0h]
    call ProcessCommand
    fxrstor [rsp+20h]
    mov rsp, r12
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    popfq
    jmp QWORD PTR [original_command_tail]
CommandTail ENDP
END
