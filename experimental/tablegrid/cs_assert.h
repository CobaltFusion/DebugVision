#pragma once

#ifdef _WIN32
#ifdef _DEBUG
#error "Please make sure _DEBUG is not defined and the /MDd flag is not specified!, See C++->Code Generation->Runtime Library and choose /MD"
#endif
#endif

#undef assert
#ifndef CSDEBUG

#define assert(expression) ((void)0)

#else

_ACRTIMP void __cdecl _wassert(
	_In_z_ wchar_t const* _Message,
	_In_z_ wchar_t const* _File,
	_In_   unsigned       _Line
);

#define assert(expression) (void)(                                                       \
            (!!(expression)) ||                                                              \
            (_wassert(_CRT_WIDE(#expression), _CRT_WIDE(__FILE__), (unsigned)(__LINE__)), 0) \
        )

#endif