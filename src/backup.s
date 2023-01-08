# backup memory

	.global _bram_mem
	.global _fxbmp_mem
	.global _bram_buffer

_bram_mem  = 0xE0000000
_fxbmp_mem = 0xE8000000


# This is an address smack in the middle of memory
# chosen to far away from any actual usage
#
_bram_buffer = 0x100000

