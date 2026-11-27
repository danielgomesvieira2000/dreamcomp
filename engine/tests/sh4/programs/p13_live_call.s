! r4 supplies a replacement for the original call target in the literal pool.
! Each delay slot overwrites r2 after the call has sampled it.
	.text
	.global _live_jsr, _live_bsrf, _first, _second
_live_jsr:
	sts	pr,r6
	mova	.Labsolute,r0
	mov.l	r4,@r0
	mov.l	.Labsolute,r2
	jsr	@r2
	mov	#0,r2
	lds	r6,pr
	rts
	nop
	.align 2
.Labsolute:
	.long _first

_live_bsrf:
	sts	pr,r6
	mova	.Lrelative,r0
	mov.l	r4,@r0
	mov.l	.Lrelative,r2
.Lcall:
	bsrf	r2
	mov	#0,r2
	lds	r6,pr
	rts
	nop
	.align 2
.Lrelative:
	.long _first - (.Lcall + 4)

_first:
	rts
	mov	#1,r0
_second:
	rts
	mov	#2,r0
