/*
 * FC-SDK CMake port: since Visual Studio 2015 <crtdbg.h> pulls in <vcruntime_new.h>, which
 * declares std::nothrow_t. Wrap it like the other C headers so that it is never processed
 * while "std" is redefined to the STLport namespace (see stl/_epilog.h).
 */

# if !defined (_STLP_OUTERMOST_HEADER_ID)
#  define _STLP_OUTERMOST_HEADER_ID 0x8a0
#  include <stl/_prolog.h>
# elif (_STLP_OUTERMOST_HEADER_ID == 0x8a0) && ! defined (_STLP_DONT_POP_0x8a0)
#  define _STLP_DONT_POP_0x8a0
# endif

# include _STLP_NATIVE_C_HEADER(crtdbg.h)

# if (_STLP_OUTERMOST_HEADER_ID == 0x8a0)
#  if ! defined (_STLP_DONT_POP_0x8a0)
#   include <stl/_epilog.h>
#   undef  _STLP_OUTERMOST_HEADER_ID
#   endif
#   undef  _STLP_DONT_POP_0x8a0
# endif
