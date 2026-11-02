#pragma once

#include "LogViewWidget.h"

#include "dvcore/Ingest.h"
#include "dvcore/LineStore.h"
#include "dvcore/LogView.h"
#include "dvcore/Settings.h"

#include <QMainWindow>
#include <QPointer>

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

class QAction;
class QLabel;
class QLineEdit;
class QMenu;
class QSystemTrayIcon;
class QTabWidget;
class QTimer;
class SourcesDialog;
class ViewTab;

struct StartupOptions
{
    bool minimized = false;
    std::vector<dv::SourceConfig> extraSources; // from the command line, temporary
    std::string loadFile;                       // a log file to load
    std::string settingsFile;                   // use this settings file instead of the default one
    bool captureStdin = false;
};

class MainWindow : public QMainWindow
{
    CS_OBJECT(MainWindow)

public:
    explicit MainWindow(const StartupOptions& options);
    ~MainWindow() override;

protected:
    bool event(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // setup
    void createActions();
    void createMenus();
    void createStatusBar();
    void applySettings();
    void saveSettings();

    // views
    ViewTab* tabAt(int index) const;
    LogViewWidget* currentView() const;
    std::vector<LogViewWidget*> views() const;
    LogViewWidget* addView(const dv::ViewSettings& settings);
    void closeView(int index);
    void newView();
    void copyView();
    void editFilters(bool timelineRules = false); // opens the dialog on the Timeline Rules tab
    void onTabChanged(int index);
    void updateTabTitles();
    void showViewContextMenu(LogViewWidget* view, QPoint globalPos, bool onProcessColumn);
    void addFilterFromHighlight(dv::FilterType type);
    void addProcessFilters(dv::FilterType type);
    void excludeSelectedLines();
    void setFilters(LogViewWidget* view, const dv::FilterSet& filters);

    // update cadence
    void scheduleUpdate();
    void onUpdate();
    void updateStatus(bool force);
    void updateActionStates();

    // commands
    void openFile(bool tail);
    void addFileSource(const std::string& path, bool tail, bool temporary);
    void runProcess(const QString& program = QString());
    void runScript(const std::string& path);
    void saveText(const std::string& text, const QString& title);
    void loadConfiguration();
    void saveConfiguration();
    void clearLog();
    void cropLog();
    void togglePause();
    void toggleGlobalCapture();
    void editSources();
    void editHistorySize();
    void chooseFont();
    void zoom(int delta);
    void find(bool forward);
    void applySources();
    void setAlwaysOnTop(bool on);
    void showAbout();

    dv::AppSettings m_settings;
    std::filesystem::path m_settingsPath;
    int64_t m_timeBase;
    dv::LineStore m_store;
    dv::FilterWorker m_worker;
    dv::Ingest m_ingest;
    ViewContext m_context;

    QTabWidget* m_tabs = nullptr;
    QLineEdit* m_findText = nullptr;
    QLabel* m_statusState = nullptr;
    QLabel* m_statusSelection = nullptr;
    QLabel* m_statusView = nullptr;
    QLabel* m_statusLog = nullptr;
    QLabel* m_statusMemory = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QTimer* m_updateTimer = nullptr;
    QPointer<SourcesDialog> m_sourcesDialog;
    std::chrono::steady_clock::time_point m_lastUpdate;
    std::chrono::steady_clock::time_point m_lastStatus;
    uint64_t m_lastFocusSeq = UINT64_MAX;
    int m_previousTab = -1;
    bool m_paused = false;

    // actions with state
    QAction* m_clockTime = nullptr;
    QAction* m_processColors = nullptr;
    QAction* m_autoScroll = nullptr;
    QAction* m_autoScrollStop = nullptr;
    QAction* m_timeline = nullptr;
    QAction* m_linkViews = nullptr;
    QAction* m_pause = nullptr;
    QAction* m_globalCapture = nullptr;
    QAction* m_autoNewline = nullptr;
    QAction* m_prefixPid = nullptr;
    QAction* m_alwaysOnTop = nullptr;
    QAction* m_hideWhenMinimized = nullptr;
};
