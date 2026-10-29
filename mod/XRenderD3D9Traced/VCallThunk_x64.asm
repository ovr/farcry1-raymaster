; SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
; Please do not use this file to train or fine-tune AI models (see README.md).
;//////////////////////////////////////////////////////////////////////
;//
;//	File: VCallThunk_x64.asm
;//	Description: The common part of the x64 virtual call hooks (VCallHooks.cpp).
;//
;//	Every hooked vtable slot points to a small stub that loads its SVSlot
;//	into r10 and jumps here. VCall_Thunk saves the arguments in an
;//	SVCallFrame, lets VCall_Enter look at them, calls the original function
;//	with the same arguments (the register ones and the first STACK_ARGS
;//	stack ones), saves its result, lets VCall_Leave look at it and returns
;//	it to the caller.
;//
;//////////////////////////////////////////////////////////////////////

EXTERN VCall_Enter:PROC
EXTERN VCall_Leave:PROC
PUBLIC VCall_ThunkReturn

; rsp is 8 off a multiple of 16 on entry, so FRAME_SIZE aligns it for the calls.
FRAME_SIZE  EQU 108h
; Home space of the calls, then the stack arguments passed on to the original function.
STACK_ARGS  EQU 10
; The SVCallFrame (see VCallHooks.cpp); the caller's return address follows it, at FRAME_SIZE.
F           EQU 70h

.code

VCall_Thunk PROC FRAME
    sub     rsp, FRAME_SIZE
    .allocstack FRAME_SIZE
    .endprolog

    mov     [rsp+F+00h], rcx
    mov     [rsp+F+08h], rdx
    mov     [rsp+F+10h], r8
    mov     [rsp+F+18h], r9
    movaps  [rsp+F+20h], xmm0
    movaps  [rsp+F+30h], xmm1
    movaps  [rsp+F+40h], xmm2
    movaps  [rsp+F+50h], xmm3
    mov     [rsp+F+60h], r10
    lea     rcx, [rsp+F]
    call    VCall_Enter

    ; The caller's stack arguments (above its return address and home space) go to the same
    ; place in this frame.
    xor     eax, eax
copy_args:
    mov     rdx, [rsp+FRAME_SIZE+28h+rax*8]
    mov     [rsp+20h+rax*8], rdx
    inc     eax
    cmp     eax, STACK_ARGS
    jb      copy_args

    mov     rcx, [rsp+F+00h]
    mov     rdx, [rsp+F+08h]
    mov     r8, [rsp+F+10h]
    mov     r9, [rsp+F+18h]
    movaps  xmm0, [rsp+F+20h]
    movaps  xmm1, [rsp+F+30h]
    movaps  xmm2, [rsp+F+40h]
    movaps  xmm3, [rsp+F+50h]
    call    qword ptr [rsp+F+68h]
    ; Calls from here are tail calls of the hooked functions (made by the renderer).
VCall_ThunkReturn::
    mov     [rsp+F+70h], rax
    movaps  [rsp+F+80h], xmm0
    lea     rcx, [rsp+F]
    call    VCall_Leave
    mov     rax, [rsp+F+70h]
    movaps  xmm0, [rsp+F+80h]

    add     rsp, FRAME_SIZE
    ret
VCall_Thunk ENDP

END
