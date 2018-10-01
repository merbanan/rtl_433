// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file

#ifndef PCH_H
#define PCH_H

#include "rtl_433.h"
#include "data.h"
#include "util.h"

#ifdef _WIN32
	#ifndef _WIN32_WINNT
		#define _WIN32_WINNT _WIN32_WINNT_VISTA
	#endif
	#pragma warning( disable : 4244 4090 4305 )
	#pragma comment( lib, "Ws2_32" )
	// Some defines to use proper POSIX function names.
	#define access _access
	#define strdup	_strdup
	#define putenv _putenv
	#if defined( x64 )
		#if defined( _DEBUG )
			#pragma comment( lib, "..\\rtlsdr_libs\\x64\\debug\\rtlsdr" )
		#else
			#pragma comment( lib, "..\\rtlsdr_libs\\x64\\release\\rtlsdr" )
		#endif
	#else
		#if defined( _DEBUG )
			#pragma comment( lib, "..\\rtlsdr_libs\\x86\\debug\\rtlsdr" )
		#else
			#pragma comment( lib, "..\\rtlsdr_libs\\x86\\release\\rtlsdr" )
		#endif
	#endif
#endif

#endif //PCH_H
