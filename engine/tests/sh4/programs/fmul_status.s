	.text
	.global _fmul_status
_fmul_status:
	fmul	fr0,fr13
	sts	fpscr,r0
	fmul	fr4,fr4
	sts	fpscr,r1
	rts
	nop
