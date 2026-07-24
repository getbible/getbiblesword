// SPDX-License-Identifier: GPL-2.0-only
#ifndef GETBIBLESWORD_EXPORT_H
#define GETBIBLESWORD_EXPORT_H

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(GETBIBLESWORD_BUILDING_SHARED_LIBRARY)
#    define GBS_API __declspec(dllexport)
#  else
#    define GBS_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define GBS_API __attribute__((visibility("default")))
#else
#  define GBS_API
#endif

#endif
