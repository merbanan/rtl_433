#ifndef RTLSDR_EXPORT_H
#define RTLSDR_EXPORT_H

#if defined __GNUC__
#  if __GNUC__ >= 4
#    define __SDR_EXPORT   __attribute__((visibility("default")))
#    define __SDR_IMPORT   __attribute__((visibility("default")))
#  else
#    define __SDR_EXPORT
#    define __SDR_IMPORT
#  endif
#elif _MSC_VER
#  define __SDR_EXPORT     __declspec(dllexport)
#  define __SDR_IMPORT     __declspec(dllimport)
#else
#  define __SDR_EXPORT
#  define __SDR_IMPORT
#endif

#ifndef rtlsdr_STATIC
#	ifdef rtlsdr_EXPORTS
#	define RTLSDR_API __SDR_EXPORT
#	else
#	define RTLSDR_API __SDR_IMPORT
#	endif
#else
#define RTLSDR_API
#endif
#endif /* RTLSDR_EXPORT_H */
