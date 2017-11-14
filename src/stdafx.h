#pragma once

#pragma warning(push, 3)

#include "targetver.h"

// TODO: Possibly use this?
//#ifdef _DEBUG
//#define new DEBUG_NEW
//#endif

// Exclude rarely-used stuff from Windows headers.
#define VC_EXTRALEAN

// Prevent the definition of the min()/max() macros.
#define NOMINMAX

// Make CString constructors explicit.
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS

// Enable all of MFC's warning messages.
#define _AFX_ALL_WARNINGS

// Enable M_PI etc. defines.
#define _USE_MATH_DEFINES

// Make Expect/Ensure throw gsl::fail_fast with a detailed debug description, instead of terminating the program.
#define GSL_THROW_ON_CONTRACT_VIOLATION

// MFC core and standard components + extensions.
#include <afxwin.h>
#include <afxext.h>

// Defining NOMINMAX is incompatible with GDI+ which we have to patch here.
#define min(x,y) ((x) < (y) ? (x) : (y))
#define max(x,y) ((x) > (y) ? (x) : (y))
#include <gdiplus.h>
#undef min
#undef max

// MFC support for Internet Explorer 4 Common Controls.
#ifndef _AFX_NO_OLE_SUPPORT
#include <afxdtctl.h>
#endif

// MFC support for Windows Common Controls.
#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>
#endif

// MFC support for ribbons and control bars.
#include <afxcontrolbars.h>

#include <mmsystem.h>
#include <dsound.h>

#pragma warning(pop)

#include <gsl/gsl>
#include <winrt/Windows.Foundation.h>

#include "defer.h"
#include "utils.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
