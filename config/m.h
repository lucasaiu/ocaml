#ifndef __PIC__
#  define ARCH_CODE32
#endif
#define ARCH_SIXTYFOUR
#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_PTR 8
#define SIZEOF_SHORT 2
#define ARCH_INT64_TYPE long
#define ARCH_UINT64_TYPE unsigned long
#define ARCH_INT64_PRINTF_FORMAT "l"
#undef ARCH_BIG_ENDIAN
#undef ARCH_ALIGN_DOUBLE
#undef ARCH_ALIGN_INT64
#undef NONSTANDARD_DIV_MOD
#define ASM_CFI_SUPPORTED
