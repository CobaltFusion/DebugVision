// DebugVision: a portable, CPU-efficient log viewer in the spirit of DebugView++

#include "Help.h"
#include "MainWindow.h"
#include "Text.h"

#include "dvcore/Platform.h"
#include "dvcore/SourceConfig.h"

#include <QApplication>
#include <QIcon>
#include <QMessageBox>

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <windows.h>

namespace {

void enablePrivilege(const wchar_t* name)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid))
        AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(token);
}

} // namespace

int main(int argc, char* argv[]);

int APIENTRY WinMain(HINSTANCE, HINSTANCE, PSTR, INT)
{
    return main(__argc, __argv);
}
#endif

namespace {

const char* const usage =
    "Usage: debugvision [options] [logfile]\n\n"
    "  --min                  start minimized (also /min)\n"
    "  --source <spec>        add a source for this session, e.g. udp:port=2020 or file:path=app.log\n"
    "  --udp <port>           shorthand for --source udp:port=<port>\n"
    "  --file <path>          shorthand for --source file:path=<path> (tail the file)\n"
    "  --settings <file>      use this settings file (also: a .json file argument)\n"
    "  logfile                load a log file (.log, .dblog, .txt)\n"
    "  -h, --help             open the user manual in the web browser (also /h, /?)\n\n"
    "Data piped into standard input is captured as well.\n";

} // namespace

int main(int argc, char* argv[])
{
#ifdef _WIN32
    // needed to capture OutputDebugString from services (Global\DBWIN) and to resolve process names
    enablePrivilege(L"SeDebugPrivilege");
    enablePrivilege(L"SeCreateGlobalPrivilege");
#endif

    StartupOptions options;
    std::string error;
    bool showHelp = false;
    try
    {
        auto& registry = dv::SourceRegistry::instance();
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc)
                    throw std::runtime_error("option " + arg + " requires a value");
                return argv[++i];
            };
            if (arg == "--min" || arg == "/min" || arg == "-min")
                options.minimized = true;
            else if (arg == "--source")
                options.extraSources.push_back(registry.parseSpec(value()));
            else if (arg == "--udp")
                options.extraSources.push_back(registry.parseSpec("udp:port=" + value()));
            else if (arg == "--file")
                options.extraSources.push_back(registry.parseSpec("file:path=" + value()));
            else if (arg == "--settings")
                options.settingsFile = value();
            else if (arg == "--help" || arg == "-h" || arg == "/h" || arg == "/?")
            {
                showHelp = true;
                break;
            }
            else if (!arg.empty() && arg[0] == '-')
                throw std::runtime_error("unknown option '" + arg + "'");
            else
            {
                auto path = dv::platform::utf8ToPath(arg);
                auto ext = path.extension().string();
                if (ext == ".json" || ext == ".JSON")
                    options.settingsFile = arg;
                else if (!options.loadFile.empty())
                    throw std::runtime_error("only one log file can be given");
                else
                    options.loadFile = arg;
            }
        }
        options.captureStdin = dv::platform::isPipe(0) || dv::platform::isRegularFile(0);
    }
    catch (const std::exception& e)
    {
        error = e.what();
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName("DebugVision");
    QApplication::setWindowIcon(QIcon(":/icons/debugvision.png"));
#if !defined(_WIN32) && !defined(__APPLE__)
    // Windows and macOS use their native style; the generic fallback style on Linux looks dated
    QApplication::setStyle(QString("fusion"));
#endif
    if (showHelp)
    {
        std::fputs(usage, stdout);
        return openUserManual() ? 0 : 1;
    }
    if (!error.empty())
    {
        QMessageBox::critical(nullptr, "DebugVision", toQString(error) + "\n\n" + toQString(usage));
        return 1;
    }
    MainWindow window(options);
    return app.exec();
}
