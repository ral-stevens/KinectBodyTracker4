//------------------------------------------------------------------------------
// <copyright file="stdafx.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

// include file for standard system and project includes

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#endif

// Windows Header Files
#include <windows.h>
#include <strsafe.h>

#include <Shlobj.h>

// Direct2D Header Files
#include <d2d1.h>

// Kinect Header files
#include <Kinect.h>

// ROS Header files
#undef ERROR
#include "ros.h"
#include <geometry_msgs/Twist.h>

#include "Aria.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <locale> 
#include <codecvt>
#include "Eigen/core"

#pragma comment (lib, "d2d1.lib")

#ifdef _UNICODE
#if defined _M_IX86
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif

// Safe release for interfaces
template<class Interface>
inline void SafeRelease(Interface *& pInterfaceToRelease)
{
    if (pInterfaceToRelease != NULL)
    {
        pInterfaceToRelease->Release();
        pInterfaceToRelease = NULL;
    }
}


template<typename T>
T saturate(T VAL, T MIN, T MAX) {
	return min(max(VAL, MIN), MAX);
}

template <typename T> int sgn(T val) {
	return (T(0) < val) - (val < T(0));
}

namespace BodyTracker {
	typedef Eigen::Vector3d Vector3d;
	typedef const Eigen::Ref<const Eigen::Vector3d>& rcVector3d;
	typedef std::function<void(rcVector3d, rcVector3d)> CalibFunctor;
}