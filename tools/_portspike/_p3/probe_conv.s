	.file	"probe_conv.cpp"
	.text
	.text
	.align 1
	.align 2
	.global	__ZN3FooC2Ev
	.type	__ZN3FooC2Ev, @function
__ZN3FooC2Ev:
	mov	#7,r1
	rts	
	mov.l	r1,@r4
	.size	__ZN3FooC2Ev, .-__ZN3FooC2Ev
	.global	__ZN3FooC1Ev
	.set	__ZN3FooC1Ev,__ZN3FooC2Ev
	.section	.text.startup,"ax",@progbits
	.align 1
	.align 2
	.global	_main
	.type	_main, @function
_main:
	mov.l	.L4,r1
	rts	
	mov.l	@r1,r0
.L5:
	.align 2
.L4:
	.long	_g_foo
	.size	_main, .-_main
	.align 1
	.align 2
	.type	__GLOBAL__sub_I__ZN3FooC2Ev, @function
__GLOBAL__sub_I__ZN3FooC2Ev:
	mov.l	.L7,r1
	mov	#7,r2
	rts	
	mov.l	r2,@r1
.L8:
	.align 2
.L7:
	.long	_g_foo
	.size	__GLOBAL__sub_I__ZN3FooC2Ev, .-__GLOBAL__sub_I__ZN3FooC2Ev
	.section	.ctors,"aw",@progbits
	.align 2
	.long	__GLOBAL__sub_I__ZN3FooC2Ev
	.global	_g_foo
	.section	.bss
	.align 2
	.type	_g_foo, @object
	.size	_g_foo, 4
_g_foo:
	.zero	4
	.ident	"GCC: (GNU) 8.2.0"
