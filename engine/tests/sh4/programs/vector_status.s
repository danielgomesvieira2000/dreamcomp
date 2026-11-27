	.text
	.global _vector_status
_vector_status:
	fipr	fv12,fv0
	sts	fpscr,r0
	fmov	fr3,fr1
	fneg	fr1
	sts	fpscr,r1
	fcmp/eq	fr0,fr0
	sts	fpscr,r2
	ftrv	xmtrx,fv4
	sts	fpscr,r3
	lds	r4,fpscr
	rts
	nop
