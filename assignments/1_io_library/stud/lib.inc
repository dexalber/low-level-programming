%ifndef INC_LIB
%define INC_LIB
; asmsyntax=nasm

; @param src  -- r/m8 to read
; ZF gives the answer
%macro is_digit 1
%push is_digit
    cmp %1,'0'
    jb %%no
    cmp %1,'9'
    ja %%no
    test %1,0 ; force set ZF
    jmp %%done
%%no:
    test rsp,-1 ; force clear ZF
%%done:
%pop
%endmacro
; @param src  -- r/m8 to read
; ZF gives the answer
%macro is_space 1
%push is_space
    cmp %1,' '
    je %%done
    cmp %1,`\t`
    je %%done
    cmp %1,`\n`
    je %%done
    cmp %1,`\r`
    %%done:
%pop
%endmacro

section .text
bits 64

sys_read  equ 0
sys_write equ 1
sys_open  equ 2
sys_close equ 3
sys_exit  equ 60

stdin  equ 0
stdout equ 1
stderr equ 2

; @IN  byte *str -- Pointer to null-terminated string.
; @OUT qword uint -- Length of `str`.
string_length:
    xor rax,rax
    test rdi,rdi
    jz .done
.loop:
    test byte [rdi + rax], 0xff
    jz .done
    inc rax
    jmp .loop
.done:
    ret

; @IN byte *buf --  Pointer to a buffer.
; @IN qword uint buf_len -- Number of bytes to write.
; @OUT qword uint -- Number of bytes written.
print_string_n: 
    mov rdx,rsi
    mov rax,sys_write
    mov rsi,rdi
    mov rdi,stdout
    syscall
    ret

; @IN reg{rdi} `str` Pointer to null-terminated string.
; @OUT reg{rax} Number of bytes written.
print_string: 
    call string_length
    mov rsi,rax
    jmp print_string_n

; @IN byte *data
; @IN byte *sink
; @IN qword uint sink_len
; @OUT sink if strlen(data) + 1 <= sink_len else 0
string_copy:
    test rdx,rdx
    jz .done
    mov rcx,rdx
    xor rdx,rdx
.loop:
    mov al,byte[rdi+rdx]
    mov byte[rsi+rdx],al
    inc rdx
    test al,al
    loopnz .loop
.done:
    mov r8,rax
    xor rax,rax
    test r8b,r8b
    cmovz rax,rsi
    ret

print_newline:
    mov dil,`\n`
; @IN byte char
print_char:
    and rdi,0xff ; TODO: is this necesarry?
    push rdi
    mov rdi,rsp
    call print_string
    add rsp,8
    ret


print_int:
    test rdi,rdi
    jns print_uint
    push rdi
    mov rdi,'-'
    call print_char
    pop rdi
    neg rdi
; @IN qword uint
print_uint:
    sub rsp,24

    mov r10,10
    mov rax,rdi
    mov rsi,20
.loop:
    dec rsi
    xor rdx,rdx    
    div r10
    add rdx,'0'
    mov byte[rsp+rsi],dl
    test rax,rax
    jnz .loop

    lea rdi,[rsp+rsi]

    ; Since we know how long the string
    ; is, we don't need to null-terminate it.
    mov r8,20
    sub r8,rsi
    mov rsi,r8

    call print_string_n
    add rsp,24
    ret


; @OUT byte -- character read, 0 otherwise
read_char:
    push qword 0
    mov rax,sys_read
    mov rdi,stdin
    mov rsi,rsp
    mov rdx,1
    syscall
    ; if rax != 1:
    ;     return 0    
    ; return rsp[0]
    xor rdi,rdi
    cmp rax,1
    cmovne rax,rdi
    cmove rax,[rsp]
    pop rdx
    ret 

; @IN byte *buffer
; @IN qword uint buflen
; @OUT byte* -- buffer if successful, otherwise 0
; @OUT qword uint -- length of string in buffer
read_word:
    test rsi,rsi
    jnz .go
    xor rax,rax
    xor rdx,rdx
    ret
.go:
    push rbx
    push r12
    push r13
    push r14

    mov rbx,rdi
    mov r12,rsi
    xor r13,r13 ; counter
    xor r14,r14 ; zero for cmov

    dec rsi ; leave space for '\0'

.loop.1:
    call read_char
    test rax,rax
    cmovz rax,rbx
    jz .done
    is_space al
    je .loop.1
.loop.2:
    cmp r13,r12
    cmovae rax,r14
    jae .done

    mov byte[rbx+r13],al
    inc r13

    call read_char
    test rax,rax
    jz .break

    is_space al
    jne .loop.2
.break:
    mov rax,rbx
.done:
    mov byte[rbx+r13],0
    mov rdx,r13
    pop r14
    pop r13
    pop r12
    pop rbx

    ret

; @IN byte* -- null-terminated string.
;              Stops parsing on overflow,
;              which is not treated as an error.
; @OUT qword uint -- result
; @OUT qword uint -- length of prefix parsed to yield the result.
;                    i.e. floor(log10(result)) + 1
;                    rdx == 0 indicates a nil result.
parse_uint:
    xor rax,rax
    xor rdx,rdx
    xor rsi,rsi
    mov r10,10
    test rdi,rdi
    jz .break
.loop:
    movzx rcx,byte[rdi+rsi]
    is_digit cl
    jne .break
    sub rcx,'0'
    xor rdx,rdx
    ; detecting overflow
    mov r11,rax
    mul r10
    test rdx,rdx
    cmovnz rax,r11
    jnz .break

    add rax,rcx
    cmovo rax,r11
    jo .break

    inc rsi
    jmp .loop
.break:
    mov rdx,rsi

    ret

; @IN byte* -- null-terminated string
; @OUT qword int -- result
; @OUT qword uint -- number of bytes consumed to get result
;              i.e. is_neg(result) + floor(log10(abs(result))) + 1
parse_int:
    mov cl,byte[rdi]
    cmp cl,'-'
    jne parse_uint

    inc rdi
    call parse_uint
    ; check if no digits followed '-'
    test rdx,rdx
    jz .done
    neg rax
    js .done
    ; if it's positive now, there was overflow
    neg rax
    ; need to undo the last iteration
    dec rdx
    movzx rcx,byte[rdi+rdx]
    sub rcx,'0'
    sub rax,rcx
.done:
    inc rdx
    ret 

string_equals:
    mov cl,[rdi]
    cmp cl,[rsi]
    jne .no
    test cl,cl
    jz .yes
    inc rdi
    inc rsi
    jmp string_equals
.yes:
    mov rax,1
    ret
.no:
    xor rax,rax
    ret
    


%endif
