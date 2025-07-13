
#ifndef MDNSD_EXPORT_H
#define MDNSD_EXPORT_H

#ifdef MDNSD_STATIC_DEFINE
#  define MDNSD_EXPORT
#  define MDNSD_NO_EXPORT
#else
#  ifndef MDNSD_EXPORT
#    ifdef MDNSD_DYNAMIC_LINKING_EXPORT
        /* We are building this library */
#      define MDNSD_EXPORT 
#    else
        /* We are using this library */
#      define MDNSD_EXPORT 
#    endif
#  endif

#  ifndef MDNSD_NO_EXPORT
#    define MDNSD_NO_EXPORT 
#  endif
#endif

#ifndef MDNSD_DEPRECATED
#  define MDNSD_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef MDNSD_DEPRECATED_EXPORT
#  define MDNSD_DEPRECATED_EXPORT MDNSD_EXPORT MDNSD_DEPRECATED
#endif

#ifndef MDNSD_DEPRECATED_NO_EXPORT
#  define MDNSD_DEPRECATED_NO_EXPORT MDNSD_NO_EXPORT MDNSD_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 1 /* DEFINE_NO_DEPRECATED */
#  ifndef MDNSD_NO_DEPRECATED
#    define MDNSD_NO_DEPRECATED
#  endif
#endif
#ifndef _XOPEN_SOURCE
# define _XOPEN_SOURCE 500
#endif
#ifndef _DEFAULT_SOURCE
# define _DEFAULT_SOURCE 1
#endif

#if defined __APPLE__
// required for ip_mreq
#define _DARWIN_C_SOURCE 1
#endif

#define MDNSD_free(ptr) free(ptr)
#define MDNSD_malloc(size) malloc(size)
#define MDNSD_calloc(num, size) calloc(num, size)
#define MDNSD_realloc(ptr, size) realloc(ptr, size)

#define MDNSD_LOGLEVEL 300

/**
 * Inline Functions
 * ---------------- */
#ifdef _MSC_VER
# define MDNSD_INLINE __inline
#else
# define MDNSD_INLINE inline
#endif

#endif /* MDNSD_EXPORT_H */
