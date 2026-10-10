#pragma once

// The app version comes from the VERSION file at the repository root: the project passes
// it in as CDX_VERSION (e.g. 0.1.0) and CDX_VERSION_COMMAS (0,1,0,0). Shared by C++ and
// the resource compiler (version info in the exe's file properties).
#ifndef CDX_VERSION
#define CDX_VERSION 0.0.0
#define CDX_VERSION_COMMAS 0,0,0,0
#endif

#define CDX_STRINGIZE_(x) #x
#define CDX_STRINGIZE(x) CDX_STRINGIZE_(x)
#define CDX_WIDEN_(x) L##x
#define CDX_WIDEN(x) CDX_WIDEN_(x)

#define CDX_VERSION_STRING CDX_STRINGIZE(CDX_VERSION)
#define CDX_VERSION_WSTRING CDX_WIDEN(CDX_VERSION_STRING)
