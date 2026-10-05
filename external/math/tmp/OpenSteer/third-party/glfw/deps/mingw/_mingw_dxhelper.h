
#if defined(_MSC_VER) && !defined(_MSC_EXTENSIONS)
#define NONAMELESSUNION		1
#endif
#if defined(NONAMELESSSTRUCT) && \
   !defined(NONAMELESSUNION)
#define NONAMELESSUNION		1
#endif
#if defined(NONAMELESSUNION)  && \
   !defined(NONAMELESSSTRUCT)
#define NONAMELESSSTRUCT	1
#endif
#if !defined(__GNU_EXTENSION)
#if defined(__GNUC__) || defined(__GNUG__)
#define __GNU_EXTENSION		__extension__
#else
#define __GNU_EXTENSION
#endif
#endif 

#ifndef __ANONYMOUS_DEFINED
#define __ANONYMOUS_DEFINED
#if defined(__GNUC__) || defined(__GNUG__)
#define _ANONYMOUS_UNION	__extension__
#define _ANONYMOUS_STRUCT	__extension__
#else
#define _ANONYMOUS_UNION
#define _ANONYMOUS_STRUCT
#endif
#ifndef NONAMELESSUNION
#define _UNION_NAME(x)
#define _STRUCT_NAME(x)
#else 
#define _UNION_NAME(x)  x
#define _STRUCT_NAME(x) x
#endif
#endif	

#ifndef DUMMYUNIONNAME
# ifdef NONAMELESSUNION
#  define DUMMYUNIONNAME  u
#  define DUMMYUNIONNAME1 u1	
#  define DUMMYUNIONNAME2 u2
#  define DUMMYUNIONNAME3 u3
#  define DUMMYUNIONNAME4 u4
#  define DUMMYUNIONNAME5 u5
#  define DUMMYUNIONNAME6 u6
#  define DUMMYUNIONNAME7 u7
#  define DUMMYUNIONNAME8 u8
#  define DUMMYUNIONNAME9 u9
# else 
#  define DUMMYUNIONNAME
#  define DUMMYUNIONNAME1	
#  define DUMMYUNIONNAME2
#  define DUMMYUNIONNAME3
#  define DUMMYUNIONNAME4
#  define DUMMYUNIONNAME5
#  define DUMMYUNIONNAME6
#  define DUMMYUNIONNAME7
#  define DUMMYUNIONNAME8
#  define DUMMYUNIONNAME9
# endif
#endif	

#if !defined(DUMMYUNIONNAME1)	
# ifdef NONAMELESSUNION
#  define DUMMYUNIONNAME1 u1	
# else
#  define DUMMYUNIONNAME1	
# endif
#endif	

#ifndef DUMMYSTRUCTNAME
# ifdef NONAMELESSUNION
#  define DUMMYSTRUCTNAME  s
#  define DUMMYSTRUCTNAME1 s1	
#  define DUMMYSTRUCTNAME2 s2
#  define DUMMYSTRUCTNAME3 s3
#  define DUMMYSTRUCTNAME4 s4
#  define DUMMYSTRUCTNAME5 s5
# else
#  define DUMMYSTRUCTNAME
#  define DUMMYSTRUCTNAME1	
#  define DUMMYSTRUCTNAME2
#  define DUMMYSTRUCTNAME3
#  define DUMMYSTRUCTNAME4
#  define DUMMYSTRUCTNAME5
# endif
#endif 

#ifndef WINELIB_NAME_AW
# ifdef __MINGW_NAME_AW
#   define WINELIB_NAME_AW  __MINGW_NAME_AW
# else
#  ifdef UNICODE
#   define WINELIB_NAME_AW(func) func##W
#  else
#   define WINELIB_NAME_AW(func) func##A
#  endif
# endif
#endif	

#ifndef DECL_WINELIB_TYPE_AW
# ifdef __MINGW_TYPEDEF_AW
#  define DECL_WINELIB_TYPE_AW  __MINGW_TYPEDEF_AW
# else
#  define DECL_WINELIB_TYPE_AW(type)  typedef WINELIB_NAME_AW(type) type;
# endif
#endif	