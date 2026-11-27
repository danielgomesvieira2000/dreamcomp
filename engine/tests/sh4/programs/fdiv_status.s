	.text
	.global _fdiv_status
_fdiv_status:
	fdiv	fr6,fr0
	sts	fpscr,r0
	fdiv	fr5,fr4
	sts	fpscr,r1
	rts
	nop
