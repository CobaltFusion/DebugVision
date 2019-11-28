#pragma warning(push)
#pragma warning(disable : 4251 4275 4244)
#include <QDebug>
#pragma warning(pop)

#ifdef _WIN32
#include "windows.h"
#endif

#include "infra/cdbg.h"
#include "cs_debugview.h"

namespace infra::dbgstream::output
{
	void write(const char* msg)
	{
		fprintf(stderr, "Debug: %s\n", msg);
#ifdef _WIN32
		OutputDebugStringA(msg);
#endif
	}

	void write(const wchar_t* msg)
	{
		fprintf(stderr, "Debug: %S\n", msg);
#ifdef _WIN32
		OutputDebugStringW(msg);
#endif
	}
}

int exec(int argc, char* argv[]);

int main(int argc, char *argv[])
{
	qInstallMsgHandler(CsDebugViewOutput);
	for (std::size_t i = 0; i < argc; ++i)
	{
		qDebug() << argv[i];
	}

	try
	{
		return exec(argc, argv);
	}
	catch (const std::exception & ex)
	{
		cdbg() << "Exception: " << ex.what() << "\n";
		throw;
	}
	catch (...)
	{
		cdbg() << "Exception: A non standard exception was thrown.\n";
		throw;
	}
	return -2;
}

#ifdef _WIN32

int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	return main(__argc, __argv);
}

#endif
