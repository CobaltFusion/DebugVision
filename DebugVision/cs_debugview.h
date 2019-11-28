#pragma once

inline void CsDebugViewOutput(QtMsgType type, QStringView  msg)
{
	switch (type) {
	case QtDebugMsg:
		fprintf(stderr, "Debug: %s\n", msg.charData());
		break;
	case QtWarningMsg:
		fprintf(stderr, "Warning: %s\n", msg.charData());
		break;
	case QtCriticalMsg:
		fprintf(stderr, "Critical: %s\n", msg.charData());
		break;
	case QtFatalMsg:
		fprintf(stderr, "Fatal: %s\n", msg.charData());
		abort();
	}
#ifdef _WIN32
	OutputDebugStringA(msg.charData());
#endif
}
