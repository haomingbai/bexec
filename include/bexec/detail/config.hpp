#pragma once

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define BEXEC_DETAIL_EXCEPTIONS_ENABLED 1
#else
#define BEXEC_DETAIL_EXCEPTIONS_ENABLED 0
#endif
