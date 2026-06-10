	.file	"probe3_raster.c"
	.text
	.text
	.align 1
	.align 2
	.global	_blit_ink_none
	.type	_blit_ink_none, @function
_blit_ink_none:
	mov.l	r8,@-r15
	mov.l	r9,@-r15
	mov.l	r10,@-r15
	mov.l	r11,@-r15
	mov.l	r12,@-r15
	mov.l	r13,@-r15
	mov.l	r14,@-r15
	mov.l	@(28,r15),r11
	tst	r11,r11
	bt.s	.L1
	mov.l	@(36,r15),r13
	mov.l	@(32,r15),r12
	mov	r7,r14
	mov.l	.L18,r10
	add	r12,r12
	add	r14,r14
	.align 2
.L6:
	mov.b	@r6+,r8
	tst	r7,r7
	extu.b	r8,r8
	shll8	r8
	bt.s	.L3
	add	r8,r8
	mov	r5,r9
	add	r7,r9
	mov	r9,r1
	mov	r4,r3
	sub	r5,r1
	.align 2
.L5:
	mov.b	@r5+,r2
	tst	r2,r2
	bt.s	.L4
	extu.b	r2,r0
	add	r0,r0
	add	r8,r0
	mov.w	@(r0,r10),r2
	mov.w	r2,@r3
.L4:
	dt	r1
	bf.s	.L5
	add	#2,r3
	mov	r9,r5
	add	r14,r4
.L3:
	dt	r11
	add	r12,r4
	bf.s	.L6
	add	r13,r5
.L1:
	mov.l	@r15+,r14
	mov.l	@r15+,r13
	mov.l	@r15+,r12
	mov.l	@r15+,r11
	mov.l	@r15+,r10
	mov.l	@r15+,r9
	rts	
	mov.l	@r15+,r8
.L19:
	.align 2
.L18:
	.long	_fullPalette
	.size	_blit_ink_none, .-_blit_ink_none
	.align 1
	.align 2
	.global	_blit_ink_blend
	.type	_blit_ink_blend, @function
_blit_ink_blend:
	mov.l	r8,@-r15
	mov.l	r9,@-r15
	mov.l	r10,@-r15
	mov.l	r11,@-r15
	mov.l	r12,@-r15
	mov.l	r13,@-r15
	mov.l	r14,@-r15
	add	#-8,r15
	mov.l	@(36,r15),r12
	tst	r12,r12
	bt	.L20
	mov.l	@(40,r15),r13
	mov	r7,r14
	mov.l	.L35,r9
	add	r13,r13
	mov.l	.L36,r11
	add	r14,r14
	.align 2
.L25:
	mov.b	@r6+,r8
	tst	r7,r7
	extu.b	r8,r8
	shll8	r8
	bt.s	.L22
	add	r8,r8
	mov	r5,r10
	add	r7,r10
	mov	r10,r2
	mov.l	r7,@r15
	mov	r4,r3
	sub	r5,r2
	.align 2
.L24:
	mov.b	@r5+,r1
	tst	r1,r1
	bt.s	.L23
	extu.b	r1,r0
	add	r0,r0
	add	r8,r0
	mov.w	@(r0,r11),r0
	mov.w	.L37,r1
	extu.w	r0,r0
	add	r1,r0
	add	r0,r0
	mov.w	@r3,r1
	mov.w	@(r0,r9),r7
	extu.w	r1,r1
	add	r1,r1
	mov	r7,r0
	mov.w	r0,@(6,r15)
	mov	r1,r0
	mov.w	@(r0,r9),r1
	add	r7,r1
	mov.w	r1,@r3
.L23:
	dt	r2
	bf.s	.L24
	add	#2,r3
	mov.l	@r15,r7
	mov	r10,r5
	add	r14,r4
.L22:
	mov.l	@(44,r15),r1
	dt	r12
	add	r13,r4
	bf.s	.L25
	add	r1,r5
.L20:
	add	#8,r15
	mov.l	@r15+,r14
	mov.l	@r15+,r13
	mov.l	@r15+,r12
	mov.l	@r15+,r11
	mov.l	@r15+,r10
	mov.l	@r15+,r9
	rts	
	mov.l	@r15+,r8
	.align 1
.L37:
	.short	1024
.L38:
	.align 2
.L35:
	.long	_blendLookupTable
.L36:
	.long	_fullPalette
	.size	_blit_ink_blend, .-_blit_ink_blend
	.comm	_blendLookupTable,16384,2
	.comm	_fullPalette,4096,2
	.ident	"GCC: (GNU) 8.2.0"
