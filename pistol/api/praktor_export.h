#ifndef PRAKTOR_EXPORT_H
#define PRAKTOR_EXPORT_H

/* Praktor owns its ABI marker. TurboUtils' TURBO_API only describes
 * TurboUtils libraries and must not leak their producer/consumer state here. */
#ifndef PRAKTOR_API
#  if defined(_WIN32) || defined(__CYGWIN__)
#    if defined(PRAKTOR_BUILD_SHARED)
#      define PRAKTOR_API __declspec(dllexport)
#    else
#      define PRAKTOR_API __declspec(dllimport)
#    endif
#  elif !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
#    define PRAKTOR_API __attribute__((visibility("default")))
#  else
#    define PRAKTOR_API
#  endif
#endif

#ifndef PRAKTOR_C_API
#  ifdef __cplusplus
#    define PRAKTOR_C_API extern "C" PRAKTOR_API
#  else
#    define PRAKTOR_C_API PRAKTOR_API
#  endif
#endif

#endif /* PRAKTOR_EXPORT_H */
