# backup memory

	.global _fxbmp_mem
	.global _program_buffer

_fxbmp_mem = 0xE8000000


# This is an address smack in the middle of memory
# chosen to be far away from any actual usage
#
_program_buffer = 0x100000

