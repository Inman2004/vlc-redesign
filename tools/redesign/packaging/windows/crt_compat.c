/*****************************************************************************
 * crt_compat.c : msvcrt entry points for VideoLAN's prebuilt win64 contribs
 *****************************************************************************
 * VideoLAN's prebuilt contribs were built against msvcrt.dll; a current
 * mingw-w64 toolchain links against the UCRT, which exports these
 * functions only as inline wrappers. Each one is provided here on top of
 * the UCRT's common stdio entry points, with msvcrt's behaviour.
 *
 * Build-time only: linked into the Windows build of VLC, not part of VLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *****************************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ucrtbase.dll */
int __cdecl __stdio_common_vsprintf( uint64_t, char *, size_t, const char *, void *, va_list );
int __cdecl __stdio_common_vsprintf_s( uint64_t, char *, size_t, const char *, void *, va_list );
int __cdecl __stdio_common_vsnprintf_s( uint64_t, char *, size_t, size_t, const char *, void *, va_list );
int __cdecl __stdio_common_vsscanf( uint64_t, const char *, size_t, const char *, void *, va_list );

#define LEGACY_VSPRINTF_NULL_TERMINATION 0x0001ULL   /* _CRT_INTERNAL_PRINTF_... */
#define SCANF_SECURECRT                  0x0001ULL   /* _CRT_INTERNAL_SCANF_SECURECRT */

/* msvcrt's _vsnprintf: no terminator and -1 when the text is cut */
static int __cdecl compat_vsnprintf( char *buf, size_t n, const char *fmt, va_list ap )
{
    return __stdio_common_vsprintf( LEGACY_VSPRINTF_NULL_TERMINATION, buf, n, fmt, NULL, ap );
}

static int __cdecl compat_snprintf( char *buf, size_t n, const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    int r = compat_vsnprintf( buf, n, fmt, ap );
    va_end( ap );
    return r;
}

static int __cdecl compat_sprintf_s( char *buf, size_t n, const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    int r = __stdio_common_vsprintf_s( 0, buf, n, fmt, NULL, ap );
    va_end( ap );
    return r < 0 ? -1 : r;
}

static int __cdecl compat_sscanf_s( const char *src, const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    int r = __stdio_common_vsscanf( SCANF_SECURECRT, src, (size_t)-1, fmt, NULL, ap );
    va_end( ap );
    return r;
}

int __cdecl vsnprintf_s( char *buf, size_t n, size_t count, const char *fmt, va_list ap )
{
    int r = __stdio_common_vsnprintf_s( 0, buf, n, count, fmt, NULL, ap );
    return r < 0 ? -1 : r;
}

/* the import-table slots the contribs call through */
int ( __cdecl *__imp__vsnprintf )( char *, size_t, const char *, va_list ) = compat_vsnprintf;
int ( __cdecl *__imp__snprintf )( char *, size_t, const char *, ... ) = compat_snprintf;
int ( __cdecl *__imp_sprintf_s )( char *, size_t, const char *, ... ) = compat_sprintf_s;
int ( __cdecl *__imp_sscanf_s )( const char *, const char *, ... ) = compat_sscanf_s;
