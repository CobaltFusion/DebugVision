#pragma once

#include "dvcore/LineStore.h"
#include "dvcore/LogView.h"
#include "dvcore/Settings.h"

#include <QAbstractScrollArea>
#include <QFont>
#include <QHeaderView>
#include <QPixmap>
#include <QStandardItemModel>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

struct ViewContext
{
    dv::LineStore* store = nullptr;
    int64_t* timeBase = nullptr; // relative time is shown from this steady clock value
    bool prefixPid = false;
};

// Paints only the visible rows of a dv::View, directly from the LineStore. No item model, no copies of the log.
class LogViewWidget : public QAbstractScrollArea
{
    CS_OBJECT(LogViewWidget)

public:
    LogViewWidget(ViewContext& context, std::shared_ptr<dv::View> view, const dv::ViewSettings& settings, QWidget* parent = nullptr);

    std::shared_ptr<dv::View> view() const { return m_view; }
    dv::ViewSettings settings() const;
    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    // called on the GUI update tick; returns true when new lines arrived
    bool refresh();
    void setRowFont(const QFont& font);
    bool isColumnVisible(dv::Column column) const;
    void setColumnVisible(dv::Column column, bool visible);

    bool clockTime() const { return m_clockTime; }
    void setClockTime(bool value);
    bool processColors() const { return m_processColors; }
    void setProcessColors(bool value);
    bool autoScroll() const { return m_autoScroll; }
    void setAutoScroll(bool value);
    bool autoScrollStop() const { return m_autoScrollStop; }
    void setAutoScrollStop(bool value) { m_autoScrollStop = value; }

    QString highlightText() const { return m_highlight; }
    void setHighlightText(const QString& text);
    // searches the message text (case-insensitive), selects and centers the match; returns false when not found
    bool find(const QString& text, bool forward);

    void toggleBookmark();
    void nextBookmark(bool forward);
    void clearBookmarks();

    void selectAll();
    void copy(bool messagesOnly) const;
    std::vector<uint64_t> selectedSeqs() const;
    bool hasSelection() const;
    uint64_t focusSeq() const { return m_focusSeq; }
    void setFocusSeq(uint64_t seq); // selects the nearest visible line at or before 'seq' (linked views)
    void scrollToEnd();
    // text of the selected lines in the DebugView++ log format
    std::string selectionAsLog() const;
    std::string viewAsLog() const;
    // distinct process names of the selected lines
    std::vector<std::string> selectedProcesses() const;
    std::vector<std::string> selectedMessages(size_t max) const;
    QString statusText() const; // e.g. "Selected: 3 lines, 0.012 s"

    std::function<void(QPoint globalPos, bool onProcessColumn)> contextMenuRequested;
    std::function<void()> selectionChanged;
    std::function<void(int delta)> zoomRequested;
    std::function<void()> autoScrollChanged;

    void resetTimeBase() { viewport()->update(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct Row
    {
        uint64_t seq = 0;
        dv::LineRecord record{};
        std::string_view text;
        dv::ChunkPtr chunk; // keeps 'text' alive
        uint16_t style = 0;
    };

    bool rowAt(size_t index, Row& row) const;
    size_t rowCount() const { return m_rowCount; }
    int rowHeight() const { return m_rowHeight; }
    int visibleRows() const;
    size_t indexAtY(int y) const;
    void updateScrollBars();
    void updateHeaderGeometry();
    void ensureVisible(size_t index, bool center);
    void setFocusIndex(size_t index, bool extend, bool toggle);
    size_t focusIndex() const;
    bool isSelected(uint64_t seq) const;
    QString columnText(int column, size_t index, const Row& row) const;
    QString processName(const dv::LineRecord& record) const;
    QColor processColor(uint32_t originId) const;
    void drawMessage(QPainter& painter, const QRect& rect, const Row& row, const QColor& foreground);
    std::string lineAsLog(const Row& row) const;
    void notifySelection();
    void onUserScroll();

    ViewContext& m_context;
    std::shared_ptr<dv::View> m_view;
    QString m_name;
    QHeaderView* m_header = nullptr;
    QStandardItemModel* m_headerModel = nullptr;
    int m_rowHeight = 16;
    size_t m_rowCount = 0;
    uint64_t m_generation = 0;

    bool m_clockTime = true;
    bool m_processColors = false;
    bool m_autoScroll = true;
    bool m_autoScrollStop = true;
    QString m_highlight;
    std::string m_highlightLower;

    // selection, by sequence number so it survives re-filtering
    uint64_t m_anchorSeq = UINT64_MAX;
    uint64_t m_focusSeq = UINT64_MAX;
    std::set<uint64_t> m_toggled;
    bool m_selectAll = false;
    std::set<uint64_t> m_bookmarks;
    uint64_t m_centerOnSeq = UINT64_MAX; // Track: keep centering until enough lines exist below

    mutable std::unordered_map<uint32_t, QColor> m_processColorCache;
    mutable dv::ColorGenerator m_processColorGenerator{0.2, 0.95};
    mutable std::vector<dv::ViewLine> m_lineBuffer;
    QPixmap m_bookmarkIcon;
};
