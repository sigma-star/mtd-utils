#ifndef __LINUX_TYPES_H__
#define __LINUX_TYPES_H__

#include <linux/types.h>
#include <sys/types.h>
#include <byteswap.h>
#include <stdint.h>
#include <unistd.h>

#include "compiler_attributes.h"

typedef __u8		u8;
typedef __u16		u16;
typedef __u32		u32;
typedef __u64		u64;

typedef __s64		time64_t;

struct qstr {
	const char *name;
	size_t len;
};

struct fscrypt_name {
	struct qstr disk_name;
};

#define fname_name(p)	((p)->disk_name.name)
#define fname_len(p)	((p)->disk_name.len)

#define S_IRUGO		(S_IRUSR|S_IRGRP|S_IROTH)
#define S_IXUGO		(S_IXUSR|S_IXGRP|S_IXOTH)

#define t16(x) ({ \
	uint16_t __b = (x); \
	(__LITTLE_ENDIAN==__BYTE_ORDER) ? __b : bswap_16(__b); \
})

#define t32(x) ({ \
	uint32_t __b = (x); \
	(__LITTLE_ENDIAN==__BYTE_ORDER) ? __b : bswap_32(__b); \
})

#define t64(x) ({ \
	uint64_t __b = (x); \
	(__LITTLE_ENDIAN==__BYTE_ORDER) ? __b : bswap_64(__b); \
})

#define cpu_to_le16(x) ((__le16){t16(x)})
#define cpu_to_le32(x) ((__le32){t32(x)})
#define cpu_to_le64(x) ((__le64){t64(x)})

#define le16_to_cpu(x) (t16((x)))
#define le32_to_cpu(x) (t32((x)))
#define le64_to_cpu(x) (t64((x)))

#define check_mul_overflow(a, b, d) ({		\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	typeof(d) __d = (d);			\
	(void) (&__a == &__b);			\
	(void) (&__a == __d);			\
	__builtin_mul_overflow(__a, __b, __d);	\
})

static inline __must_check size_t array_size(size_t a, size_t b)
{
	size_t bytes;
	if (check_mul_overflow(a, b, &bytes))
		return SIZE_MAX;

	return bytes;
}

static inline int int_log2(unsigned int arg)
{
	int  l = 0;

	arg >>= 1;
	while (arg) {
		l++;
		arg >>= 1;
	}
	return l;
}

#undef PAGE_SIZE
#define PAGE_SIZE (getpagesize())
#undef PAGE_SHIFT
#define PAGE_SHIFT (int_log2(PAGE_SIZE))

#endif
