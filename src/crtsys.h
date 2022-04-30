#pragma once

#include <windows.h>

#pragma warning(disable : 4201)
#include <WinUser.h>

EXTERN_C _ACRTIMP const unsigned short *__cdecl __pctype_func(void);
EXTERN_C _ACRTIMP const wctype_t *__cdecl __pwctype_func(void);

#define WINBASEAPI
EXTERN_C
WINBASEAPI
int WINAPI GetLocaleInfoEx(_In_opt_ LPCWSTR lpLocaleName, _In_ LCTYPE LCType,
                           _Out_writes_to_opt_(cchData, return )
                               LPWSTR lpLCData,
                           _In_ int cchData);

EXTERN_C
WINBASEAPI
BOOL WINAPI GetStringTypeW(_In_ DWORD dwInfoType,
                           _In_NLS_string_(cchSrc) LPCWCH lpSrcStr,
                           _In_ int cchSrc, _Out_ LPWORD lpCharType);