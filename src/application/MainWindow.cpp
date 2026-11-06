#include "MainWindow.h"

#include "FilterDialog.h"
#include "Help.h"
#include "RunDialog.h"
#include "SourcesDialog.h"
#include "Text.h"
#include "TrafficIndicator.h"
#include "ViewTab.h"

#include "dvcore/Clock.h"
#include "dvcore/FileFormat.h"
#include "dvcore/Platform.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QSize>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <fstream>

namespace {

using namespace std::chrono_literals;

const int updateEventType = QEvent::registerEventType();
const int sourcesEventType = QEvent::registerEventType();
constexpr auto minUpdateInterval = 33ms; // at most ~30 updates per second, and none when nothing happens
constexpr auto statusInterval = 1s;

QString formatCount(uint64_t n)
{
    // 1234567 -> "1,234,567"
    auto s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
        s.insert(static_cast<size_t>(i), ",");
    return toQString(s);
}

bool hasExtension(const std::filesystem::path& path, std::initializer_list<const char*> extensions)
{
    auto ext = path.extension().string();
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto e : extensions)
        if (ext == e)
            return true;
    return false;
}

} // namespace

MainWindow::MainWindow(const StartupOptions& options) :
    m_settingsPath(options.settingsFile.empty() ? dv::defaultSettingsPath() : dv::platform::utf8ToPath(options.settingsFile)),
    m_timeBase(dv::nowSteadyNs()),
    m_worker(m_store),
    m_ingest(m_store)
{
    try
    {
        m_settings = dv::loadSettings(m_settingsPath);
    }
    catch (const std::exception& e)
    {
        QMessageBox::warning(this, "DebugVision", toQString(e.what()) + "\n\nDefault settings are used.");
        m_settings = dv::AppSettings::defaults();
    }
    m_store.setHistoryLimit(m_settings.historyLimit);
    m_context = ViewContext{&m_store, &m_timeBase, m_settings.prefixPid};

    setWindowTitle("DebugVision");
    setAcceptDrops(true);
    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) { closeView(index); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) { onTabChanged(index); });

    m_updateTimer = new QTimer(this);
    m_updateTimer->setSingleShot(true);
    connect(m_updateTimer, &QTimer::timeout, this, [this]() { onUpdate(); });

    createActions();
    createStatusBar();

    for (auto& view : m_settings.views)
        addView(view);
    m_tabs->setCurrentIndex(std::clamp(m_settings.currentView, 0, m_tabs->count() - 1));
    updateTabTitles();

    // the worker and the ingest thread only post an event on a state change; nothing runs while idle
    m_worker.setNotify([this] { QCoreApplication::postEvent(this, new QEvent(static_cast<QEvent::Type>(updateEventType))); });
    m_ingest.setStatusCallback([this] { QCoreApplication::postEvent(this, new QEvent(static_cast<QEvent::Type>(sourcesEventType))); });
    m_worker.start();
    m_ingest.start();

    for (auto config : options.extraSources)
    {
        config.temporary = true;
        m_settings.sources.push_back(config);
    }
    if (options.captureStdin)
    {
        auto stdinSource = dv::SourceRegistry::instance().makeDefault("stdin");
        stdinSource.temporary = true;
        m_settings.sources.push_back(stdinSource);
    }
    applySources();

    if (!options.loadFile.empty())
        addFileSource(options.loadFile, false, true);

    applySettings();
    if (options.minimized)
        showMinimized();
    else if (m_settings.maximized)
        showMaximized();
    else
        show();
}

MainWindow::~MainWindow()
{
    m_ingest.stop();
    m_worker.stop();
}

// ---------------------------------------------------------------------------------------------------- setup

void MainWindow::createActions()
{
    auto add = [this](QMenu* menu, const QString& text, const QString& shortcut, auto handler) {
        auto action = menu->addAction(text);
        if (!shortcut.isEmpty())
            action->setShortcut(QKeySequence(shortcut));
        connect(action, &QAction::triggered, this, [handler](bool) { handler(); });
        return action;
    };
    auto addToggle = [this](QMenu* menu, const QString& text, const QString& shortcut, bool checked, auto handler) {
        auto action = menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(checked);
        if (!shortcut.isEmpty())
            action->setShortcut(QKeySequence(shortcut));
        connect(action, &QAction::triggered, this, [handler](bool on) { handler(on); });
        return action;
    };

    auto file = menuBar()->addMenu("&File");
    add(file, "&New View", "Ctrl+N", [this] { newView(); });
    add(file, "&Copy View", "", [this] { copyView(); });
    add(file, "C&lose View", "Ctrl+W", [this] { closeView(m_tabs->currentIndex()); });
    file->addSeparator();
    add(file, "&Open...", "Ctrl+O", [this] { openFile(false); });
    add(file, "&Tail File...", "Ctrl+Shift+O", [this] { openFile(true); });
    add(file, "&Run...", "", [this] { runProcess(); });
    file->addSeparator();
    add(file, "Load Configuration...", "", [this] { loadConfiguration(); });
    add(file, "Save Configuration...", "", [this] { saveConfiguration(); });
    file->addSeparator();
    add(file, "Save &Selection...", "Alt+S", [this] {
        if (auto v = currentView())
            saveText(v->selectionAsLog(), "Save Selection");
    });
    add(file, "Save &View...", "Ctrl+S", [this] {
        if (auto v = currentView())
            saveText(v->viewAsLog(), "Save View");
    });
    add(file, "Save &Log...", "Ctrl+Shift+S", [this] {
        std::string out;
        m_store.forEach(m_store.beginSeq(), m_store.endSeq(), [&](const dv::LineRef& line) {
            dv::appendDbLogLine(out, static_cast<double>(line.record->steadyNs - m_timeBase) / 1e9, line.record->systemNs, line.record->pid,
                m_store.origins().get(line.record->originId).processName, line.text);
        });
        saveText(out, "Save Log");
    });
    file->addSeparator();
    add(file, "E&xit", "", [this] { close(); });

    auto edit = menuBar()->addMenu("&Edit");
    add(edit, "&Copy", "Ctrl+C", [this] {
        if (auto v = currentView())
            v->copy(false);
    });
    add(edit, "Copy &Messages", "Ctrl+Shift+C", [this] {
        if (auto v = currentView())
            v->copy(true);
    });
    add(edit, "Select &All", "Ctrl+A", [this] {
        if (auto v = currentView())
            v->selectAll();
    });
    edit->addSeparator();
    add(edit, "&Find", "Ctrl+F", [this] {
        m_findText->setFocus();
        m_findText->selectAll();
    });
    add(edit, "Find &Next", "F3", [this] { find(true); });
    add(edit, "Find &Previous", "Shift+F3", [this] { find(false); });
    edit->addSeparator();
    add(edit, "Toggle &Bookmark", "Ctrl+F2", [this] {
        if (auto v = currentView())
            v->toggleBookmark();
    });
    add(edit, "Next Bookmark", "F2", [this] {
        if (auto v = currentView())
            v->nextBookmark(true);
    });
    add(edit, "Previous Bookmark", "Shift+F2", [this] {
        if (auto v = currentView())
            v->nextBookmark(false);
    });
    add(edit, "Clear Bookmarks", "Ctrl+Shift+F2", [this] {
        if (auto v = currentView())
            v->clearBookmarks();
    });
    edit->addSeparator();
    add(edit, "&Highlight Text", "Ctrl+H", [this] { addFilterFromHighlight(dv::FilterType::Highlight); });
    add(edit, "&Include Text", "Ctrl+I", [this] { addFilterFromHighlight(dv::FilterType::Include); });
    add(edit, "&Exclude Text", "Ctrl+E", [this] { addFilterFromHighlight(dv::FilterType::Exclude); });
    add(edit, "&Token Text", "Ctrl+K", [this] { addFilterFromHighlight(dv::FilterType::Token); });
    add(edit, "Exclude &Lines", "", [this] { excludeSelectedLines(); });

    auto view = menuBar()->addMenu("&View");
    add(view, "&Filters...", "F5", [this] { editFilters(); });
    view->addSeparator();
    m_clockTime = addToggle(view, "&Clock Time", "Ctrl+T", true, [this](bool on) {
        if (auto v = currentView())
            v->setClockTime(on);
    });
    m_processColors = addToggle(view, "&Process Colors", "", false, [this](bool on) {
        if (auto v = currentView())
            v->setProcessColors(on);
    });
    m_autoScroll = addToggle(view, "&Auto Scroll", "", true, [this](bool on) {
        if (auto v = currentView())
            v->setAutoScroll(on);
    });
    m_autoScrollStop = addToggle(view, "Auto Scroll &Stop", "", true, [this](bool on) {
        if (auto v = currentView())
            v->setAutoScrollStop(on);
    });
    view->addSeparator();
    add(view, "Clear &View", "", [this] {
        if (auto v = currentView())
            v->view()->clearView();
        m_worker.wake();
    });
    add(view, "&Reset View", "", [this] {
        if (auto v = currentView())
            v->view()->resetView(0);
        m_worker.wake();
    });
    add(view, "Reset to This &Line", "", [this] {
        if (auto v = currentView(); v && v->focusSeq() != UINT64_MAX)
            v->view()->resetView(v->focusSeq());
        m_worker.wake();
    });
    view->addSeparator();
    m_timeline = addToggle(view, "&Timeline", "Ctrl+L", false, [this](bool on) {
        if (auto v = currentView())
            ViewTab::of(v)->setTimelineVisible(on);
        saveSettings();
    });
    add(view, "Timeline Rul&es...", "", [this] { editFilters(true); });
    view->addSeparator();
    m_linkViews = addToggle(view, "&Link Views", "", m_settings.linkViews, [this](bool on) { m_settings.linkViews = on; });

    auto log = menuBar()->addMenu("&Log");
    add(log, "&Clear Log", "Ctrl+Shift+X", [this] { clearLog(); });
    add(log, "&Remove Lines Before Selection", "", [this] { cropLog(); });
    log->addSeparator();
#ifdef __APPLE__
    m_pause = addToggle(log, "&Pause", "Ctrl+Shift+P", false, [this](bool) { togglePause(); });
#else
    m_pause = addToggle(log, "&Pause", "Pause", false, [this](bool) { togglePause(); });
    // Ctrl+Shift+P as an alternative for keyboards without a Pause key
    auto pauseAlternative = new QAction(this);
    pauseAlternative->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(pauseAlternative, &QAction::triggered, this, [this](bool) { m_pause->trigger(); });
    addAction(pauseAlternative);
#endif
    log->addSeparator();
    add(log, "&Sources...", "", [this] { editSources(); });
#ifdef _WIN32
    m_globalCapture = addToggle(log, "Capture &Global Win32", "", false, [this](bool) { toggleGlobalCapture(); });
#endif
    add(log, "&History Size...", "", [this] { editHistorySize(); });
    m_autoNewline = addToggle(log, "Auto &Newline", "", true, [this](bool on) {
        for (auto& s : m_settings.sources)
            s.autoNewline = on;
        applySources();
    });
    m_prefixPid = addToggle(log, "Prefix &PID", "", m_settings.prefixPid, [this](bool on) {
        m_settings.prefixPid = on;
        m_context.prefixPid = on;
        for (auto v : views())
            v->viewport()->update();
    });

    auto options = menuBar()->addMenu("&Options");
    add(options, "&Font...", "", [this] { chooseFont(); });
    m_alwaysOnTop = addToggle(options, "&Always On Top", "", m_settings.alwaysOnTop, [this](bool on) { setAlwaysOnTop(on); });
#ifndef __APPLE__
    // restricted on macOS: minimized windows go to the Dock
    m_hideWhenMinimized = addToggle(options, "&Hide When Minimized", "", m_settings.hideWhenMinimized, [this](bool on) { m_settings.hideWhenMinimized = on; });
#endif
    addToggle(options, "&Traffic Indicator", "", m_settings.trafficIndicator, [this](bool on) {
        m_settings.trafficIndicator = on;
        m_trafficIndicator->setVisible(on);
    });
    add(options, "HTTP &Proxy...", "", [this] { chooseHttpProxy(); });

    auto help = menuBar()->addMenu("&Help");
    add(help, "&User Manual", "F1", [this] {
        if (!openUserManual())
            QMessageBox::warning(this, "DebugVision", "The user manual could not be opened.");
    });
    add(help, "&About DebugVision", "", [this] { showAbout(); });

    // icons (from DebugView++) on the menu items, and the toolbar in DebugView++'s button order
    auto findAction = [this](const QString& text) -> QAction* {
        for (auto menuAction : menuBar()->actions())
            for (auto action : menuAction->menu()->actions())
                if (action->text() == text)
                    return action;
        return nullptr;
    };
    struct ToolButton
    {
        const char* text; // menu text, nullptr = separator
        const char* icon;
    };
    const ToolButton buttons[] = {
        {"&Open...", "open"},
        {"Save &Log...", "save"},
        {nullptr, nullptr},
        {"&Copy", "copy"},
        {"&Clear Log", "clear"},
        {nullptr, nullptr},
        {"&Auto Scroll", "autoscroll"},
        {"&Clock Time", "clocktime"},
        {"&Font...", "font"},
        {"&Filters...", "filter"},
        {"&Link Views", "linkviews"},
        {nullptr, nullptr},
        {"Toggle &Bookmark", "bookmark"},
        {"Previous Bookmark", "bookmark-previous"},
        {"Next Bookmark", "bookmark-next"},
        {"Clear Bookmarks", "bookmark-clear"},
        {nullptr, nullptr},
        {"&Pause", "pause"},
    };
    auto toolbar = addToolBar("Toolbar");
    toolbar->setObjectName("toolbar");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(16, 16));
    for (auto& b : buttons)
    {
        if (!b.text)
        {
            toolbar->addSeparator();
            continue;
        }
        if (auto action = findAction(QString::fromUtf8(b.text)))
        {
            action->setIcon(QIcon(":/icons/" + QString::fromUtf8(b.icon) + ".png"));
            toolbar->addAction(action);
        }
    }

    // find bar
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(" Find: "));
    m_findText = new QLineEdit;
    m_findText->setMaximumWidth(300);
    m_findText->setPlaceholderText("Ctrl+F");
    m_findText->setClearButtonEnabled(true);
    toolbar->addWidget(m_findText);
    auto next = toolbar->addAction("Next");
    auto previous = toolbar->addAction("Previous");
    connect(m_findText, &QLineEdit::returnPressed, this, [this]() { find(true); });
    connect(next, &QAction::triggered, this, [this](bool) { find(true); });
    connect(previous, &QAction::triggered, this, [this](bool) { find(false); });

    // the traffic indicator, at the right end
    auto spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    // a widget in a toolbar is shown and hidden by its action
    m_trafficIndicator = toolbar->addWidget(new TrafficIndicator(m_store, toolbar));
    m_trafficIndicator->setVisible(m_settings.trafficIndicator);
}

void MainWindow::createStatusBar()
{
    m_statusState = new QLabel("Ready");
    m_statusSelection = new QLabel;
    m_statusView = new QLabel;
    m_statusLog = new QLabel;
    m_statusMemory = new QLabel;
    statusBar()->addWidget(m_statusState, 1);
    statusBar()->addPermanentWidget(m_statusSelection);
    statusBar()->addPermanentWidget(m_statusView);
    statusBar()->addPermanentWidget(m_statusLog);
    statusBar()->addPermanentWidget(m_statusMemory);
}

void MainWindow::applySettings()
{
    if (m_settings.x >= 0 && m_settings.y >= 0)
        move(m_settings.x, m_settings.y);
    resize(m_settings.width, m_settings.height);
    // an explicit default: a bare 'typewriter' style hint resolves to Consolas on Windows
#if defined(_WIN32)
    const char* defaultFont = "Courier New";
#elif defined(__APPLE__)
    const char* defaultFont = "Menlo";
#else
    const char* defaultFont = "DejaVu Sans Mono";
#endif
    QFont font = this->font();
    font.setFamily(toQString(m_settings.fontName.empty() ? std::string(defaultFont) : m_settings.fontName));
    font.setStyleHint(QFont::TypeWriter); // fallback when the family is not installed
    font.setPointSize(m_settings.fontSize);
    for (auto v : views())
        v->setRowFont(font);
    if (m_settings.alwaysOnTop)
        setAlwaysOnTop(true);
}

void MainWindow::saveSettings()
{
    m_settings.views.clear();
    for (auto v : views())
        m_settings.views.push_back(ViewTab::of(v)->settings());
    m_settings.currentView = m_tabs->currentIndex();
    m_settings.maximized = isMaximized();
    if (!isMaximized() && !isMinimized())
    {
        m_settings.x = x();
        m_settings.y = y();
        m_settings.width = width();
        m_settings.height = height();
    }
    try
    {
        dv::saveSettings(m_settingsPath, m_settings);
    }
    catch (const std::exception& e)
    {
        statusBar()->showMessage(toQString(e.what()), 5000);
    }
}

// ---------------------------------------------------------------------------------------------------- events

bool MainWindow::event(QEvent* event)
{
    if (event->type() == updateEventType)
    {
        scheduleUpdate();
        return true;
    }
    if (event->type() == sourcesEventType)
    {
        if (m_sourcesDialog)
            m_sourcesDialog->updateStatus(m_ingest.sources());
        updateStatus(true);
        return true;
    }
    return QMainWindow::event(event);
}

void MainWindow::scheduleUpdate()
{
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastUpdate >= minUpdateInterval)
    {
        onUpdate();
        return;
    }
    if (!m_updateTimer->isActive())
        m_updateTimer->start(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(minUpdateInterval - (now - m_lastUpdate)).count()) + 1);
}

void MainWindow::onUpdate()
{
    m_lastUpdate = std::chrono::steady_clock::now();
    // acknowledge first: changes made from now on will post a new event
    m_worker.acknowledge();
    auto current = currentView();
    for (auto v : views())
    {
        auto grown = v->refresh();
        ViewTab::of(v)->refresh();
        if (grown && v != current && !v->property("newLines").toBool())
        {
            v->setProperty("newLines", true);
            updateTabTitles();
        }
    }
    updateStatus(false);
}

void MainWindow::updateStatus(bool force)
{
    auto v = currentView();
    QString state = m_paused ? QString("Paused") : QString("Ready");
    if (v && v->view()->isFiltering())
        state = "Filtering...";
    m_statusState->setText(state);
    m_statusView->setText(v ? "View: " + formatCount(v->view()->size()) + " lines" : QString());
    m_statusLog->setText("Log: " + formatCount(m_store.size()) + " lines");
    auto now = std::chrono::steady_clock::now();
    if (force || now - m_lastStatus >= statusInterval)
    {
        m_lastStatus = now;
        m_statusMemory->setText("Memory: " + formatCount(dv::platform::privateMemoryUsage() / (1024 * 1024)) + " MB");
        m_statusSelection->setText(v ? v->statusText() : QString());
    }
}

void MainWindow::updateActionStates()
{
    auto v = currentView();
    if (!v)
        return;
    m_clockTime->setChecked(v->clockTime());
    m_processColors->setChecked(v->processColors());
    m_autoScroll->setChecked(v->autoScroll());
    m_autoScrollStop->setChecked(v->autoScrollStop());
    m_timeline->setChecked(ViewTab::of(v)->timelineVisible());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    if (m_tray)
        m_tray->hide();
    event->accept();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
#ifndef __APPLE__
    if (event->type() == QEvent::WindowStateChange && isMinimized() && m_settings.hideWhenMinimized && QSystemTrayIcon::isSystemTrayAvailable())
    {
        if (!m_tray)
        {
            m_tray = new QSystemTrayIcon(windowIcon(), this);
            m_tray->setToolTip("DebugVision");
            connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                {
                    showNormal();
                    activateWindow();
                    m_tray->hide();
                }
            });
        }
        m_tray->show();
        QTimer::singleShot(0, this, [this]() { hide(); });
    }
#endif
}

namespace {

bool isHttpUrl(const QUrl& url)
{
    auto scheme = url.scheme().toLower();
    return url.isValid() && (scheme == "http" || scheme == "https");
}

// the URLs of a drop; a link dragged as plain text (e.g. from a terminal or an editor) counts as well
QList<QUrl> droppedUrls(const QMimeData* data)
{
    if (data->hasUrls())
        return data->urls();
    QList<QUrl> urls;
    if (data->hasText())
    {
        QUrl url(data->text().trimmed());
        if (isHttpUrl(url))
            urls.append(url);
    }
    return urls;
}

} // namespace

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!droppedUrls(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (auto& url : droppedUrls(event->mimeData()))
    {
        if (isHttpUrl(url))
        {
            addUrlSource(toStd(url.toString()), true);
            setWindowTitle("[" + url.toString() + "] - DebugVision");
            continue;
        }
        if (!url.isLocalFile())
            continue;
        auto pathText = toStd(url.toLocalFile());
        auto path = dv::platform::utf8ToPath(pathText);
#ifdef _WIN32
        if (hasExtension(path, {".exe"}))
        {
            runProcess(toQString(pathText));
            continue;
        }
        if (hasExtension(path, {".cmd", ".bat"}))
        {
            runScript(pathText);
            continue;
        }
#else
        if (hasExtension(path, {".sh"}))
        {
            runScript(pathText);
            continue;
        }
        std::error_code ec;
        auto perms = std::filesystem::status(path, ec).permissions();
        if (!ec && path.extension().empty() && (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none)
        {
            runProcess(toQString(pathText));
            continue;
        }
#endif
        addFileSource(pathText, true, true);
        setWindowTitle("[" + toQString(dv::platform::pathToUtf8(path.filename())) + "] - DebugVision");
    }
    event->acceptProposedAction();
}

// ---------------------------------------------------------------------------------------------------- views

ViewTab* MainWindow::tabAt(int index) const
{
    return static_cast<ViewTab*>(m_tabs->widget(index));
}

LogViewWidget* MainWindow::currentView() const
{
    auto tab = tabAt(m_tabs->currentIndex());
    return tab ? tab->logView() : nullptr;
}

std::vector<LogViewWidget*> MainWindow::views() const
{
    std::vector<LogViewWidget*> result;
    for (int i = 0; i < m_tabs->count(); ++i)
        result.push_back(tabAt(i)->logView());
    return result;
}

LogViewWidget* MainWindow::addView(const dv::ViewSettings& settings)
{
    std::shared_ptr<dv::View> view;
    try
    {
        view = m_worker.addView(settings.filters);
    }
    catch (const dv::FilterError& e)
    {
        QMessageBox::warning(this, "DebugVision", "View '" + toQString(settings.name) + "': " + toQString(e.what()) + "\nThe filters of this view are disabled.");
        view = m_worker.addView(dv::FilterSet{});
    }
    auto widget = new LogViewWidget(m_context, view, settings);
    if (auto current = currentView())
        widget->setRowFont(current->font());
    widget->contextMenuRequested = [this, widget](QPoint pos, bool onProcess) { showViewContextMenu(widget, pos, onProcess); };
    widget->selectionChanged = [this, widget] {
        if (auto tab = ViewTab::of(widget))
            tab->focusChanged();
        if (widget == currentView())
            m_statusSelection->setText(widget->statusText());
    };
    widget->autoScrollChanged = [this, widget] {
        if (widget == currentView())
            m_autoScroll->setChecked(widget->autoScroll());
    };
    widget->zoomRequested = [this](int delta) { zoom(delta); };
    m_tabs->addTab(new ViewTab(widget, m_context, settings), widget->name());
    updateTabTitles();
    return widget;
}

void MainWindow::closeView(int index)
{
    if (m_tabs->count() <= 1 || index < 0)
        return;
    auto tab = tabAt(index);
    m_worker.removeView(tab->logView()->view());
    m_tabs->removeTab(index);
    tab->deleteLater();
    updateTabTitles();
}

void MainWindow::newView()
{
    dv::ViewSettings settings;
    settings.name = "View " + std::to_string(m_tabs->count() + 1);
    FilterDialog dialog(toQString(settings.name), dv::FilterSet{}, {}, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    settings.name = toStd(dialog.name());
    settings.filters = dialog.filters();
    settings.timelineRules = dialog.timelineRules();
    settings.showTimeline = !settings.timelineRules.empty();
    m_tabs->setCurrentWidget(ViewTab::of(addView(settings)));
    saveSettings();
}

void MainWindow::copyView()
{
    auto v = currentView();
    if (!v)
        return;
    auto settings = ViewTab::of(v)->settings();
    settings.name += " (copy)";
    m_tabs->setCurrentWidget(ViewTab::of(addView(settings)));
    saveSettings();
}

void MainWindow::setFilters(LogViewWidget* view, const dv::FilterSet& filters)
{
    try
    {
        view->view()->setFilters(filters);
        m_worker.wake();
    }
    catch (const dv::FilterError& e)
    {
        QMessageBox::warning(this, "Filters", toQString(e.what()));
    }
}

void MainWindow::editFilters(bool timelineRules)
{
    auto v = currentView();
    if (!v)
        return;
    auto tab = ViewTab::of(v);
    FilterDialog dialog(v->name(), v->view()->filters(), tab->timelineRules(), this);
    if (timelineRules)
        dialog.showTimelineRules();
    if (dialog.exec() != QDialog::Accepted)
        return;
    v->setName(dialog.name());
    setFilters(v, dialog.filters());
    auto rules = dialog.timelineRules();
    if (rules != tab->timelineRules())
    {
        try
        {
            tab->setTimelineRules(rules);
        }
        catch (const dv::TimelineError& e)
        {
            QMessageBox::warning(this, "Timeline Rules", toQString(e.what()));
        }
        // show what the new rules make of the view
        if (!rules.empty())
        {
            tab->setTimelineVisible(true);
            m_timeline->setChecked(true);
        }
    }
    updateTabTitles();
    saveSettings();
}

void MainWindow::onTabChanged(int index)
{
    auto v = currentView();
    if (!v)
        return;
    // linked views: show the line that was focused in the previous view
    if (m_linkViews && m_linkViews->isChecked() && m_previousTab >= 0 && m_previousTab < m_tabs->count() && m_previousTab != index)
    {
        auto previous = tabAt(m_previousTab)->logView();
        if (previous && previous->focusSeq() != UINT64_MAX)
            v->setFocusSeq(previous->focusSeq());
    }
    m_previousTab = index;
    if (v->property("newLines").toBool())
    {
        v->setProperty("newLines", false);
        updateTabTitles();
    }
    updateActionStates();
    updateStatus(true);
}

void MainWindow::updateTabTitles()
{
    for (auto v : views())
    {
        auto index = m_tabs->indexOf(ViewTab::of(v));
        m_tabs->setTabText(index, v->name() + (v->property("newLines").toBool() ? QString("*") : QString()));
    }
    m_tabs->tabBar()->setVisible(m_tabs->count() > 1);
    if (m_linkViews)
        m_linkViews->setEnabled(m_tabs->count() > 1);
}

void MainWindow::showViewContextMenu(LogViewWidget* view, QPoint globalPos, bool onProcessColumn)
{
    QMenu menu(this);
    auto add = [&](QMenu* m, const QString& text, auto handler) {
        auto a = m->addAction(text);
        connect(a, &QAction::triggered, this, [handler](bool) { handler(); });
        return a;
    };
    add(&menu, "Copy", [view] { view->copy(false); });
    add(&menu, "Copy Messages", [view] { view->copy(true); });
    add(&menu, "Select All", [view] { view->selectAll(); });
    menu.addSeparator();
    add(&menu, "Toggle Bookmark", [view] { view->toggleBookmark(); });
    menu.addSeparator();
    auto highlight = view->highlightText();
    if (!highlight.isEmpty())
    {
        auto quoted = "'" + highlight + "'";
        add(&menu, "Highlight " + quoted, [this] { addFilterFromHighlight(dv::FilterType::Highlight); });
        add(&menu, "Include " + quoted, [this] { addFilterFromHighlight(dv::FilterType::Include); });
        add(&menu, "Exclude " + quoted, [this] { addFilterFromHighlight(dv::FilterType::Exclude); });
        add(&menu, "Token " + quoted, [this] { addFilterFromHighlight(dv::FilterType::Token); });
        add(&menu, "Track " + quoted, [this] { addFilterFromHighlight(dv::FilterType::Track); });
        add(&menu, "Once " + quoted, [this] { addFilterFromHighlight(dv::FilterType::Once); });
        menu.addSeparator();
    }
    add(&menu, "Exclude Lines", [this] { excludeSelectedLines(); });
    auto process = onProcessColumn ? &menu : menu.addMenu("Process");
    if (onProcessColumn)
        menu.addSeparator();
    add(process, "Include Process", [this] { addProcessFilters(dv::FilterType::Include); });
    add(process, "Exclude Process", [this] { addProcessFilters(dv::FilterType::Exclude); });
    add(process, "Highlight Process", [this] { addProcessFilters(dv::FilterType::Highlight); });
    add(process, "Track Process", [this] { addProcessFilters(dv::FilterType::Track); });
    add(process, "Once Process", [this] { addProcessFilters(dv::FilterType::Once); });
    menu.addSeparator();
    auto columns = menu.addMenu("Columns");
    for (auto column : {dv::Column::Line, dv::Column::Date, dv::Column::Time, dv::Column::Pid, dv::Column::Process})
    {
        auto name = QString::fromUtf8(dv::columnName(column));
        name = name.left(1).toUpper() + name.mid(1);
        auto a = columns->addAction(name);
        a->setCheckable(true);
        a->setChecked(view->isColumnVisible(column));
        connect(a, &QAction::triggered, this, [view, column](bool on) { view->setColumnVisible(column, on); });
    }
    menu.addSeparator();
    add(&menu, "Clear View", [this, view] {
        view->view()->clearView();
        m_worker.wake();
    });
    add(&menu, "Reset View", [this, view] {
        view->view()->resetView(0);
        m_worker.wake();
    });
    add(&menu, "Reset to This Line", [this, view] {
        if (view->focusSeq() != UINT64_MAX)
            view->view()->resetView(view->focusSeq());
        m_worker.wake();
    });
    menu.exec(globalPos);
}

void MainWindow::addFilterFromHighlight(dv::FilterType type)
{
    auto v = currentView();
    if (!v || v->highlightText().isEmpty())
        return;
    auto filters = v->view()->filters();
    dv::Filter f;
    f.text = toStd(v->highlightText());
    f.filterType = type;
    static dv::ColorGenerator backgrounds(0.5, 0.95);
    static dv::ColorGenerator texts(0.9, 0.7);
    if (type == dv::FilterType::Highlight || type == dv::FilterType::Track)
        f.background = backgrounds.next();
    if (type == dv::FilterType::Token)
        f.foreground = texts.next();
    filters.messageFilters.push_back(f);
    setFilters(v, filters);
    saveSettings();
}

void MainWindow::addProcessFilters(dv::FilterType type)
{
    auto v = currentView();
    if (!v)
        return;
    auto filters = v->view()->filters();
    static dv::ColorGenerator backgrounds(0.5, 0.95);
    for (auto& name : v->selectedProcesses())
    {
        dv::Filter f;
        f.text = name;
        f.filterType = type;
        if (type == dv::FilterType::Highlight || type == dv::FilterType::Track)
            f.background = backgrounds.next();
        filters.processFilters.push_back(f);
    }
    setFilters(v, filters);
    saveSettings();
}

void MainWindow::excludeSelectedLines()
{
    auto v = currentView();
    if (!v)
        return;
    auto filters = v->view()->filters();
    for (auto& message : v->selectedMessages(100))
    {
        if (message.empty())
            continue;
        dv::Filter f;
        f.text = message;
        f.filterType = dv::FilterType::Exclude;
        filters.messageFilters.push_back(f);
    }
    setFilters(v, filters);
    saveSettings();
}

// ---------------------------------------------------------------------------------------------------- commands

void MainWindow::applySources()
{
    auto sources = m_settings.sources;
    dv::applyHttpProxyDefault(sources, m_settings.httpProxy);
    m_ingest.applySources(sources);
#ifdef _WIN32
    if (m_globalCapture)
    {
        bool global = false;
        for (auto& s : m_settings.sources)
            if (s.type == "outputdebugstring" && s.enabled && s.getParams<dv::OutputDebugStringParams>().scope != "local")
                global = true;
        m_globalCapture->setChecked(global);
    }
#endif
}

void MainWindow::addFileSource(const std::string& path, bool tail, bool temporary)
{
    auto config = dv::SourceRegistry::instance().makeDefault("file");
    dv::FileParams params;
    params.path = path;
    params.tail = tail;
    config.setParams(params);
    config.temporary = temporary;
    m_settings.sources.push_back(config);
    applySources();
}

void MainWindow::chooseHttpProxy()
{
    QDialog dialog(this);
    dialog.setWindowTitle("HTTP Proxy");
    auto layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Proxy for the http(s) sources that do not have their own proxy setting.\n"
                                 "Empty: the system proxy settings; 'direct': no proxy;\n"
                                 "e.g. http://proxy.example.com:8080, socks5h://127.0.0.1:1080 or http://user:password@proxy:3128"));
    auto edit = new QLineEdit(toQString(m_settings.httpProxy));
    edit->setPlaceholderText("system proxy settings");
    layout->addWidget(edit);

    // local SOCKS proxies (e.g. 'ssh -D 1080 user@host') are only suggested: one is used only when chosen here
    auto detected = dv::platform::detectLocalSocksProxies(dv::platform::defaultSocksPorts(), std::chrono::milliseconds(300));
    for (auto& proxy : detected)
    {
        auto row = new QHBoxLayout;
        auto text = "Detected on this computer: " + toQString(proxy.proxy) + (proxy.note.empty() ? QString() : " (" + toQString(proxy.note) + ")");
        row->addWidget(new QLabel(text), 1);
        auto use = new QPushButton("Use");
        use->setToolTip("Puts this proxy in the field above; it is saved with OK");
        auto value = toQString(proxy.proxy);
        connect(use, &QPushButton::clicked, &dialog, [edit, value](bool) {
            edit->setText(value);
            edit->setFocus();
        });
        row->addWidget(use);
        layout->addLayout(row);
    }

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, edit]() {
        auto error = dv::platform::validateHttpProxy(toStd(edit->text().trimmed()));
        if (!error.empty())
        {
            QMessageBox::warning(&dialog, "HTTP Proxy", toQString(error));
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_settings.httpProxy = toStd(edit->text().trimmed());
    applySources(); // restarts the http sources that use the default proxy
}

void MainWindow::addUrlSource(const std::string& url, bool temporary)
{
    auto config = dv::SourceRegistry::instance().makeDefault("http");
    dv::HttpParams params;
    params.url = url;
    config.setParams(params);
    config.temporary = temporary;
    m_settings.sources.push_back(config);
    applySources();
}

void MainWindow::openFile(bool tail)
{
    auto file = QFileDialog::getOpenFileName(this, tail ? QString("Tail File") : QString("Open"), QString(), "Log files (*.log *.dblog *.txt);;All files (*)");
    if (file.isEmpty())
        return;
    if (!tail)
        clearLog(); // as DebugView++: opening a log replaces the current one
    addFileSource(toStd(file), tail, true);
    setWindowTitle("[" + QFileInfo(file).fileName() + "] - DebugVision");
}

void MainWindow::runProcess(const QString& program)
{
    RunDialog dialog(program, this);
    if (dialog.exec() != QDialog::Accepted || dialog.program().empty())
        return;
    auto config = dv::SourceRegistry::instance().makeDefault("process");
    dv::ProcessParams params;
    params.command = dialog.program();
    params.arguments = dialog.arguments();
    params.workingDirectory = dialog.workingDirectory();
    config.setParams(params);
    config.temporary = true;
    m_settings.sources.push_back(config);
    applySources();
}

void MainWindow::runScript(const std::string& path)
{
    auto config = dv::SourceRegistry::instance().makeDefault("process");
    dv::ProcessParams params;
#ifdef _WIN32
    params.command = "cmd.exe";
    params.arguments = {"/Q", "/C", path};
#else
    params.command = "/bin/sh";
    params.arguments = {path};
#endif
    params.workingDirectory = dv::platform::pathToUtf8(dv::platform::utf8ToPath(path).parent_path());
    config.setParams(params);
    config.name = dv::platform::pathToUtf8(dv::platform::utf8ToPath(path).filename());
    config.processNameOverride = config.name;
    config.temporary = true;
    m_settings.sources.push_back(config);
    applySources();
}

void MainWindow::saveText(const std::string& text, const QString& title)
{
    auto file = QFileDialog::getSaveFileName(this, title, QString(), "DebugVision log (*.dblog);;Text files (*.txt *.log);;All files (*)");
    if (file.isEmpty())
        return;
    std::ofstream out(dv::platform::utf8ToPath(toStd(file)), std::ios::binary | std::ios::trunc);
    out << "\xEF\xBB\xBF" << dv::dbLogHeaderLine(dv::nowSystemNs()) << text;
    if (!out)
        QMessageBox::warning(this, title, "Cannot write '" + file + "'");
}

void MainWindow::loadConfiguration()
{
    auto file = QFileDialog::getOpenFileName(this, "Load Configuration", QString(), "DebugVision configuration (*.json)");
    if (file.isEmpty())
        return;
    try
    {
        auto loaded = dv::loadSettings(dv::platform::utf8ToPath(toStd(file)));
        // replace the views and sources, keep the window settings
        while (m_tabs->count() > 0)
        {
            auto tab = tabAt(0);
            m_worker.removeView(tab->logView()->view());
            m_tabs->removeTab(0);
            tab->deleteLater();
        }
        for (auto& view : loaded.views)
            addView(view);
        m_settings.sources = loaded.sources;
        m_settings.linkViews = loaded.linkViews;
        m_linkViews->setChecked(loaded.linkViews);
        applySources();
        applySettings();
        saveSettings();
    }
    catch (const std::exception& e)
    {
        QMessageBox::warning(this, "Load Configuration", toQString(e.what()));
    }
}

void MainWindow::saveConfiguration()
{
    auto file = QFileDialog::getSaveFileName(this, "Save Configuration", "debugvision.json", "DebugVision configuration (*.json)");
    if (file.isEmpty())
        return;
    saveSettings();
    try
    {
        dv::saveSettings(dv::platform::utf8ToPath(toStd(file)), m_settings);
    }
    catch (const std::exception& e)
    {
        QMessageBox::warning(this, "Save Configuration", toQString(e.what()));
    }
}

void MainWindow::clearLog()
{
    m_store.clear();
    m_timeBase = dv::nowSteadyNs();
    for (auto v : views())
        v->resetTimeBase();
}

void MainWindow::cropLog()
{
    auto v = currentView();
    if (!v)
        return;
    auto seqs = v->selectedSeqs();
    if (seqs.empty())
        return;
    m_store.crop(seqs.front());
}

void MainWindow::togglePause()
{
    m_paused = !m_paused;
    m_pause->setChecked(m_paused);
    m_ingest.setPaused(m_paused);
    updateStatus(true);
}

void MainWindow::toggleGlobalCapture()
{
#ifdef _WIN32
    bool found = false;
    bool enable = m_globalCapture->isChecked();
    for (auto& s : m_settings.sources)
    {
        if (s.type != "outputdebugstring")
            continue;
        found = true;
        s.enabled = true;
        s.setParams(dv::OutputDebugStringParams{enable ? "both" : "local"});
    }
    if (!found)
    {
        auto config = dv::SourceRegistry::instance().makeDefault("outputdebugstring");
        config.setParams(dv::OutputDebugStringParams{enable ? "both" : "local"});
        m_settings.sources.push_back(config);
    }
    applySources();
    saveSettings();
#endif
}

void MainWindow::editSources()
{
    SourcesDialog dialog(m_settings.sources, m_ingest.sources(), this);
    m_sourcesDialog = &dialog;
    auto result = dialog.exec();
    m_sourcesDialog = nullptr;
    if (result != QDialog::Accepted)
        return;
    m_settings.sources = dialog.configs();
    applySources();
    saveSettings();
}

void MainWindow::editHistorySize()
{
    bool ok = false;
    auto current = static_cast<int>(std::min<uint64_t>(m_settings.historyLimit, 2000000000));
    auto value = QInputDialog::getInt(this, "History Size", "Maximum number of lines to keep (0 = unlimited):", current, 0, 2000000000, 100000, &ok);
    if (!ok)
        return;
    m_settings.historyLimit = static_cast<uint64_t>(value);
    m_store.setHistoryLimit(m_settings.historyLimit);
    saveSettings();
}

void MainWindow::chooseFont()
{
    bool ok = false;
    // only scalable fonts: bitmap fonts (e.g. 'Courier', as opposed to 'Courier New') are replaced by a fallback when drawn
    auto font = QFontDialog::getFont(&ok, currentView() ? currentView()->font() : this->font(), this, "Font",
        QFontDialog::MonospacedFonts | QFontDialog::ScalableFonts);
    if (!ok)
        return;
    m_settings.fontName = toStd(font.family());
    m_settings.fontSize = std::max(1, font.pointSize());
    for (auto v : views())
        v->setRowFont(font);
    saveSettings();
}

void MainWindow::zoom(int delta)
{
    auto v = currentView();
    if (!v || delta == 0)
        return;
    auto font = v->font();
    auto size = static_cast<double>(font.pointSizeF() > 0 ? font.pointSizeF() : font.pointSize());
    size = delta > 0 ? size * 1.15 : size / 1.15;
    size = std::clamp(size, 4.0, 24.0);
    font.setPointSizeF(size);
    m_settings.fontSize = static_cast<int>(size + 0.5);
    for (auto view : views())
        view->setRowFont(font);
}

void MainWindow::find(bool forward)
{
    auto v = currentView();
    if (!v)
        return;
    auto text = m_findText->text();
    if (text.isEmpty())
        text = v->highlightText();
    if (text.isEmpty())
        return;
    m_statusState->setText("Searching: \"" + text + "\"");
    if (!v->find(text, forward))
    {
        QApplication::beep();
        statusBar()->showMessage("\"" + text + "\" not found", 3000);
    }
    updateStatus(true);
}

void MainWindow::setAlwaysOnTop(bool on)
{
    m_settings.alwaysOnTop = on;
    auto flags = windowFlags();
    setWindowFlags(on ? (flags | Qt::WindowStaysOnTopHint) : (flags & ~Qt::WindowStaysOnTopHint));
    show();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, "About DebugVision",
        "DebugVision\n\n"
        "A portable, CPU-efficient log viewer in the spirit of DebugView++.\n"
        "Captures OutputDebugString (Windows), tails files, captures process output, "
        "UDP and TCP, and filters with PCRE2.\n\n"
        "Settings: " + toQString(dv::platform::pathToUtf8(m_settingsPath)));
}
