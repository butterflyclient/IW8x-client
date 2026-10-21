; Observe the variadic Replay Com_Error boundary without reconstructing its
; arguments. Restore all volatile argument registers and tail-jump with the
; caller's original stack (including every stack argument) intact.
EXTERN iw8x_record_native_error:PROC
EXTERN iw8x_native_error:QWORD

.code
iw8x_error_trace PROC FRAME
    sub rsp, 0E8h
    .allocstack 0E8h
    .endprolog
    mov [rsp+28h], rax
    mov [rsp+30h], rcx
    mov [rsp+38h], rdx
    mov [rsp+40h], r8
    mov [rsp+48h], r9
    mov [rsp+50h], r10
    mov [rsp+58h], r11
    movaps [rsp+60h], xmm0
    movaps [rsp+70h], xmm1
    movaps [rsp+80h], xmm2
    movaps [rsp+90h], xmm3
    movaps [rsp+0A0h], xmm4
    movaps [rsp+0B0h], xmm5
    mov rax, [rsp+0E8h]
    mov [rsp+20h], rax
    call iw8x_record_native_error
    movaps xmm0, [rsp+60h]
    movaps xmm1, [rsp+70h]
    movaps xmm2, [rsp+80h]
    movaps xmm3, [rsp+90h]
    movaps xmm4, [rsp+0A0h]
    movaps xmm5, [rsp+0B0h]
    mov rax, [rsp+28h]
    mov rcx, [rsp+30h]
    mov rdx, [rsp+38h]
    mov r8, [rsp+40h]
    mov r9, [rsp+48h]
    mov r10, [rsp+50h]
    mov r11, [rsp+58h]
    add rsp, 0E8h
    jmp QWORD PTR [iw8x_native_error]
iw8x_error_trace ENDP
END
