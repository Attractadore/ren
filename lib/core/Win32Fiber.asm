public fiber_save_context_x64
public fiber_load_context_x64
public fiber_switch_context_x64

.code

fiber_save_context_x64 proc
  ; Save RIP.
  mov r8, [rsp]
  mov [rcx + 8*0], r8

  ; Save RSP.
  lea r8, [rsp + 8]
  mov [rcx + 8*1], r8

  ; Save callee saved registers.
  mov [rcx + 8*2], rdi
  mov [rcx + 8*3], rsi
  mov [rcx + 8*4], rbx
  mov [rcx + 8*5], rbp
  mov [rcx + 8*6], r12
  mov [rcx + 8*7], r13
  mov [rcx + 8*8], r14
  mov [rcx + 8*9], r15

  ; Save callee saved SSE registers.
  movdqa [rcx + 8*10], xmm6
  movdqa [rcx + 8*12], xmm7
  movdqa [rcx + 8*14], xmm8
  movdqa [rcx + 8*16], xmm9
  movdqa [rcx + 8*18], xmm10
  movdqa [rcx + 8*20], xmm11
  movdqa [rcx + 8*22], xmm12
  movdqa [rcx + 8*24], xmm13
  movdqa [rcx + 8*26], xmm14
  movdqa [rcx + 8*28], xmm15

  xor eax, eax
  ret
fiber_save_context_x64 endp

fiber_load_context_x64 proc
  mov r8, [rcx + 8*0]

  ; Load RSP.
  mov rsp, [rcx + 8*1]

  ; Load callee saved registers.
  mov rdi, [rcx + 8*2]
  mov rsi, [rcx + 8*3]
  mov rbx, [rcx + 8*4]
  mov rbp, [rcx + 8*5]
  mov r12, [rcx + 8*6]
  mov r13, [rcx + 8*7]
  mov r14, [rcx + 8*8]
  mov r15, [rcx + 8*9]

  ; Load callee saved SSE registers.
  movdqa xmm6,  [rcx + 8*10]
  movdqa xmm7,  [rcx + 8*12]
  movdqa xmm8,  [rcx + 8*14]
  movdqa xmm9,  [rcx + 8*16]
  movdqa xmm10, [rcx + 8*18]
  movdqa xmm11, [rcx + 8*20]
  movdqa xmm12, [rcx + 8*22]
  movdqa xmm13, [rcx + 8*24]
  movdqa xmm14, [rcx + 8*26]
  movdqa xmm15, [rcx + 8*28]

  ; Load RIP. 
  push r8 

  xor eax, eax
  ret
fiber_load_context_x64 endp

fiber_switch_context_x64 proc
  ; Save RIP.
  mov r8, [rsp]
  mov [rcx + 8*0], r8

  ; Save RSP.
  lea r8, [rsp + 8]
  mov [rcx + 8*1], r8

  ; Save callee saved registers.
  mov [rcx + 8*2], rdi
  mov [rcx + 8*3], rsi
  mov [rcx + 8*4], rbx
  mov [rcx + 8*5], rbp
  mov [rcx + 8*6], r12
  mov [rcx + 8*7], r13
  mov [rcx + 8*8], r14
  mov [rcx + 8*9], r15

  ; Save callee saved SSE registers.
  movdqa [rcx + 8*10], xmm6
  movdqa [rcx + 8*12], xmm7
  movdqa [rcx + 8*14], xmm8
  movdqa [rcx + 8*16], xmm9
  movdqa [rcx + 8*18], xmm10
  movdqa [rcx + 8*20], xmm11
  movdqa [rcx + 8*22], xmm12
  movdqa [rcx + 8*24], xmm13
  movdqa [rcx + 8*26], xmm14
  movdqa [rcx + 8*28], xmm15

  mov r8, [rdx + 8*0]

  ; Load RSP.
  mov rsp, [rdx + 8*1]

  ; Load callee saved registers.
  mov rdi, [rdx + 8*2]
  mov rsi, [rdx + 8*3]
  mov rbx, [rdx + 8*4]
  mov rbp, [rdx + 8*5]
  mov r12, [rdx + 8*6]
  mov r13, [rdx + 8*7]
  mov r14, [rdx + 8*8]
  mov r15, [rdx + 8*9]

  ; Load callee saved SSE registers.
  movdqa xmm6,  [rdx + 8*10]
  movdqa xmm7,  [rdx + 8*12]
  movdqa xmm8,  [rdx + 8*14]
  movdqa xmm9,  [rdx + 8*16]
  movdqa xmm10, [rdx + 8*18]
  movdqa xmm11, [rdx + 8*20]
  movdqa xmm12, [rdx + 8*22]
  movdqa xmm13, [rdx + 8*24]
  movdqa xmm14, [rdx + 8*26]
  movdqa xmm15, [rdx + 8*28]

  ; Load RIP. 
  push r8 

  xor eax, eax
  ret
fiber_switch_context_x64 endp

end
