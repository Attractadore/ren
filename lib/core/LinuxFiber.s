.intel_syntax noprefix

.type ren_fiber_save_context_system_v, @function
.global ren_fiber_save_context_system_v

.type ren_fiber_load_context_system_v, @function
.global ren_fiber_load_context_system_v

.type ren_fiber_switch_context_system_v, @function
.global ren_fiber_switch_context_system_v

.type ren_fiber_start_system_v, @function
.global ren_fiber_start_system_v

.text

ren_fiber_save_context_system_v:
  # Save RIP.
  mov r8, [rsp]
  mov [rdi + 8*0], r8

  # Save RSP.
  lea r8, [rsp + 8]
  mov [rdi + 8*1], r8

  # Save callee saved registers.
  mov [rdi + 8*2], rbx
  mov [rdi + 8*3], rbp
  mov [rdi + 8*4], r12
  mov [rdi + 8*5], r13
  mov [rdi + 8*6], r14
  mov [rdi + 8*7], r15

  xor eax, eax
  ret

ren_fiber_load_context_system_v:
  mov r8, [rdi + 8*0]

  # Load RSP.
  mov rsp, [rdi + 8*1]

  # Load callee saved registers.
  mov rbx, [rdi + 8*2]
  mov rbp, [rdi + 8*3]
  mov r12, [rdi + 8*4]
  mov r13, [rdi + 8*5]
  mov r14, [rdi + 8*6]
  mov r15, [rdi + 8*7]

  # Load RIP. 
  push r8 

  xor eax, eax
  ret

ren_fiber_switch_context_system_v:
  # Save RIP.
  mov r8, [rsp]
  mov [rdi + 8*0], r8

  # Save RSP.
  lea r8, [rsp + 8]
  mov [rdi + 8*1], r8

  # Save callee saved registers.
  mov [rdi + 8*2], rbx
  mov [rdi + 8*3], rbp
  mov [rdi + 8*4], r12
  mov [rdi + 8*5], r13
  mov [rdi + 8*6], r14
  mov [rdi + 8*7], r15

  mov r8, [rsi + 8*0]

  # Load RSP.
  mov rsp, [rsi + 8*1]

  # Load callee saved registers.
  mov rbx, [rsi + 8*2]
  mov rbp, [rsi + 8*3]
  mov r12, [rsi + 8*4]
  mov r13, [rsi + 8*5]
  mov r14, [rsi + 8*6]
  mov r15, [rsi + 8*7]

  # Load RIP. 
  push r8 

  xor eax, eax
  ret

ren_fiber_start_system_v:
  mov rbx, [rsp+8]
  mov rbp, [rsp+0]
  call ren_fiber_start_cpp
  push rbp
  jmp rbx
