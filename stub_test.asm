option casemap:none
EXTERN CommandTail:PROC
PUBLIC ProcessCommand
PUBLIC original_command_tail
.data
original_command_tail QWORD ReturnTail
received QWORD 0
.code
ReturnTail PROC
    ret
ReturnTail ENDP
ProcessCommand PROC
    mov received, rcx
    xor eax, eax
    xor ecx, ecx
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    ret
ProcessCommand ENDP
RunStubTest PROC
    sub rsp, 68h
    mov QWORD PTR [rsp+28h], 12345678h
    mov eax, 11
    mov ecx, 22
    mov edx, 33
    mov r8d, 44
    mov r9d, 55
    mov r10d, 66
    mov r11d, 77
    pcmpeqd xmm0, xmm0
    stc
    call CommandTail
    setc BYTE PTR [rsp]
    cmp eax, 11
    jne failed
    cmp ecx, 22
    jne failed
    cmp edx, 33
    jne failed
    cmp r8d, 44
    jne failed
    cmp r9d, 55
    jne failed
    cmp r10d, 66
    jne failed
    cmp r11d, 77
    jne failed
    cmp BYTE PTR [rsp], 1
    jne failed
    cmp received, 12345678h
    jne failed
    pmovmskb eax, xmm0
    cmp eax, 0FFFFh
    jne failed
    xor eax, eax
    add rsp, 68h
    ret
failed:
    mov eax, 1
    add rsp, 68h
    ret
RunStubTest ENDP
END
