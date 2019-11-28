#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

inline void CsDebugViewOutput(QtMsgType type, const char* msg)
{
	switch (type) {
	case QtDebugMsg:
		fprintf(stderr, "Debug: %s\n", msg);
		break;
	case QtWarningMsg:
		fprintf(stderr, "Warning: %s\n", msg);
		break;
	case QtCriticalMsg:
		fprintf(stderr, "Critical: %s\n", msg);
		break;
	case QtFatalMsg:
		fprintf(stderr, "Fatal: %s\n", msg);
		abort();
	}
#ifdef _WIN32
	OutputDebugStringA(msg);
#endif
}
