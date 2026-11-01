#include "LogViewWidget.h"

#include "Text.h"

#include "dvcore/Clock.h"
#include "dvcore/FileFormat.h"
#include "dvcore/FilterEngine.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <optional>

namespace {

constexpr int columnCount = static_cast<int>(dv::Column::Count);
const char* const columnTitles[columnCount] = {"", "Line", "Date", "Time", "PID", "Process", "Message"};
constexpr int textMargin = 4;

QColor toQColor(const dv::Color& c)
{
    return QColor(c.red(), c.green(), c.blue());
}

std::string toLowerAscii(std::string_view s)
{
    std::string r(s);
    for (auto& c : r)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return r;
}

QString expandTabs(std::string_view text)
{
    if (text.find('\t') == std::string_view::npos)
        return toQString(text);
    std::string s;
    s.reserve(text.size() + 16);
    for (char c : text)
    {
        if (c == '\t')
            s.append(4 - (s.size() % 4), ' ');
        else
            s.push_back(c);
    }
    return toQString(s);
}

} // namespace

LogViewWidget::LogViewWidget(ViewContext& context, std::shared_ptr<dv::View> view, const dv::ViewSettings& settings, QWidget* parent) :
    QAbstractScrollArea(parent),
    m_context(context),
    m_view(std::move(view)),
    m_name(toQString(settings.name)),
    m_clockTime(settings.clockTime),
    m_processColors(settings.processColors),
    m_autoScroll(settings.autoScroll),
    m_autoScrollStop(settings.autoScrollStop)
{
    m_headerModel = new QStandardItemModel(0, columnCount, this);
    QStringList titles;
    for (auto t : columnTitles)
        titles.append(QString::fromUtf8(t));
    m_headerModel->setHorizontalHeaderLabels(titles);

    m_header = new QHeaderView(Qt::Horizontal, this);
    m_header->setModel(m_headerModel);
    m_header->setSectionsMovable(true);
    m_header->setStretchLastSection(false);
    m_header->setContextMenuPolicy(Qt::CustomContextMenu);
    for (int i = 0; i < columnCount && i < static_cast<int>(settings.columns.size()); ++i)
    {
        m_header->resizeSection(i, settings.columns[i].width);
        m_header->setSectionHidden(i, !settings.columns[i].visible);
    }
    m_header->setSectionHidden(0, false); // the bookmark column is always shown
    // restore the column order
    std::vector<std::pair<int, int>> order;
    for (int i = 0; i < columnCount && i < static_cast<int>(settings.columns.size()); ++i)
        order.emplace_back(settings.columns[i].order, i);
    std::sort(order.begin(), order.end());
    for (int visual = 0; visual < static_cast<int>(order.size()); ++visual)
        m_header->moveSection(m_header->visualIndex(order[visual].second), visual);

    connect(m_header, &QHeaderView::sectionResized, this, [this](int, int, int) {
        updateScrollBars();
        viewport()->update();
    });
    connect(m_header, &QHeaderView::sectionMoved, this, [this](int, int, int) { viewport()->update(); });
    connect(m_header, &QHeaderView::customContextMenuRequested, this, [this](const QPoint&) {
        // the column menu is handled by the main window through the same callback
        if (contextMenuRequested)
            contextMenuRequested(QCursor::pos(), false);
    });

    m_bookmarkIcon = QPixmap(":/icons/bookmark-marker.png");
    setFocusPolicy(Qt::StrongFocus);
    verticalScrollBar()->setSingleStep(1);
    viewport()->setAutoFillBackground(false);
    // every pixel is painted in paintEvent; an opaque surface also allows sub-pixel (ClearType) text rendering
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    setRowFont(font());
}

dv::ViewSettings LogViewWidget::settings() const
{
    dv::ViewSettings s;
    s.name = toStd(m_name);
    s.filters = m_view->filters();
    s.filters.name = s.name;
    s.clockTime = m_clockTime;
    s.processColors = m_processColors;
    s.autoScroll = m_autoScroll;
    s.autoScrollStop = m_autoScrollStop;
    for (int i = 0; i < columnCount; ++i)
    {
        s.columns[i].visible = !m_header->isSectionHidden(i);
        s.columns[i].width = m_header->sectionSize(i) > 0 ? m_header->sectionSize(i) : s.columns[i].width;
        s.columns[i].order = m_header->visualIndex(i);
    }
    return s;
}

void LogViewWidget::setRowFont(const QFont& font)
{
    setFont(font);
    QFontMetrics fm(font);
    m_rowHeight = std::max(fm.height() + 2, 18);
    // make sure the fixed-format columns fit their content in this font
    auto fit = [&](dv::Column column, const char* sample) {
        auto needed = fm.width(QString::fromUtf8(sample)) + 2 * textMargin + 4;
        if (m_header->sectionSize(static_cast<int>(column)) < needed)
            m_header->resizeSection(static_cast<int>(column), needed);
    };
    fit(dv::Column::Line, "8888888");
    fit(dv::Column::Date, "8888/88/88");
    fit(dv::Column::Time, "88:88:88.888");
    fit(dv::Column::Pid, "888888");
    updateHeaderGeometry();
    updateScrollBars();
    viewport()->update();
}

bool LogViewWidget::isColumnVisible(dv::Column column) const
{
    return !m_header->isSectionHidden(static_cast<int>(column));
}

void LogViewWidget::setColumnVisible(dv::Column column, bool visible)
{
    m_header->setSectionHidden(static_cast<int>(column), !visible);
    updateScrollBars();
    viewport()->update();
}

void LogViewWidget::setClockTime(bool value)
{
    m_clockTime = value;
    viewport()->update();
}

void LogViewWidget::setProcessColors(bool value)
{
    m_processColors = value;
    viewport()->update();
}

void LogViewWidget::setAutoScroll(bool value)
{
    if (m_autoScroll == value)
        return;
    m_autoScroll = value;
    if (value)
    {
        m_centerOnSeq = UINT64_MAX;
        scrollToEnd();
    }
    if (autoScrollChanged)
        autoScrollChanged();
}

void LogViewWidget::setHighlightText(const QString& text)
{
    m_highlight = text;
    m_highlightLower = toLowerAscii(toStd(text));
    viewport()->update();
}

int LogViewWidget::visibleRows() const
{
    return std::max(1, viewport()->height() / m_rowHeight);
}

size_t LogViewWidget::indexAtY(int y) const
{
    return static_cast<size_t>(verticalScrollBar()->value()) + static_cast<size_t>(std::max(0, y) / m_rowHeight);
}

void LogViewWidget::updateHeaderGeometry()
{
    auto h = m_header->sizeHint().height();
    setViewportMargins(0, h, 0, 0);
    auto fw = frameWidth();
    m_header->setGeometry(fw, fw, viewport()->width(), h);
}

void LogViewWidget::updateScrollBars()
{
    auto rows = visibleRows();
    auto count = static_cast<int>(std::min<size_t>(m_rowCount, 0x7ffffff0));
    verticalScrollBar()->setPageStep(rows);
    verticalScrollBar()->setRange(0, std::max(0, count - rows));
    horizontalScrollBar()->setPageStep(viewport()->width());
    horizontalScrollBar()->setRange(0, std::max(0, m_header->length() - viewport()->width()));
}

void LogViewWidget::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateHeaderGeometry();
    updateScrollBars();
}

void LogViewWidget::scrollContentsBy(int, int)
{
    m_header->setOffset(horizontalScrollBar()->value());
    viewport()->update();
}

bool LogViewWidget::refresh()
{
    auto events = m_view->takeEvents();
    auto oldCount = m_rowCount;
    m_rowCount = m_view->size();
    auto generation = m_view->generation();
    bool replaced = generation != m_generation;
    m_generation = generation;

    if (events.cleared && m_rowCount == 0)
    {
        m_anchorSeq = m_focusSeq = UINT64_MAX;
        m_toggled.clear();
        m_selectAll = false;
        if (!m_autoScroll)
            setAutoScroll(true);
    }
    for (auto seq : events.bookmarks)
        m_bookmarks.insert(seq);
    if (!events.stops.empty() && m_autoScroll)
    {
        setAutoScroll(false);
        m_centerOnSeq = events.stops.front();
    }
    if (!events.tracks.empty())
    {
        if (m_autoScroll)
            setAutoScroll(false);
        m_centerOnSeq = events.tracks.back();
    }
    for (int i = 0; i < std::min(events.beeps, 1); ++i)
        QApplication::beep();

    updateScrollBars();
    auto first = static_cast<size_t>(verticalScrollBar()->value());
    if (m_centerOnSeq != UINT64_MAX)
    {
        auto index = m_view->lowerBound(m_centerOnSeq);
        setFocusIndex(index, false, false);
        ensureVisible(index, true);
        // keep centering (Track) until enough lines exist below the line
        if (index + static_cast<size_t>(visibleRows() / 2) < m_rowCount)
            m_centerOnSeq = UINT64_MAX;
    }
    else if (m_autoScroll)
    {
        scrollToEnd();
    }

    bool grew = m_rowCount != oldCount;
    // repaint only when something visible changed: new lines below the visible area cost nothing
    if (replaced || events.cleared || events.changed || first + static_cast<size_t>(visibleRows()) >= oldCount)
        viewport()->update();
    if (grew || replaced)
        notifySelection();
    return grew;
}

void LogViewWidget::scrollToEnd()
{
    updateScrollBars();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void LogViewWidget::ensureVisible(size_t index, bool center)
{
    auto rows = static_cast<size_t>(visibleRows());
    auto first = static_cast<size_t>(verticalScrollBar()->value());
    if (center)
    {
        verticalScrollBar()->setValue(static_cast<int>(index > rows / 2 ? index - rows / 2 : 0));
        return;
    }
    if (index < first)
        verticalScrollBar()->setValue(static_cast<int>(index));
    else if (index >= first + rows)
        verticalScrollBar()->setValue(static_cast<int>(index - rows + 1));
}

bool LogViewWidget::rowAt(size_t index, Row& row) const
{
    m_view->lines(index, 1, m_lineBuffer);
    if (m_lineBuffer.empty())
        return false;
    row.seq = m_lineBuffer[0].seq;
    row.style = m_lineBuffer[0].style;
    row.chunk = m_context.store->chunkFor(row.seq);
    if (!row.chunk)
        return false;
    row.record = row.chunk->record(static_cast<uint32_t>(row.seq - row.chunk->firstSeq()));
    row.text = row.chunk->text(row.record);
    return true;
}

QString LogViewWidget::processName(const dv::LineRecord& record) const
{
    return toQString(m_context.store->origins().get(record.originId).processName);
}

QColor LogViewWidget::processColor(uint32_t originId) const
{
    auto it = m_processColorCache.find(originId);
    if (it != m_processColorCache.end())
        return it->second;
    auto color = toQColor(m_processColorGenerator.next());
    m_processColorCache.emplace(originId, color);
    return color;
}

QString LogViewWidget::columnText(int column, size_t index, const Row& row) const
{
    switch (static_cast<dv::Column>(column))
    {
    case dv::Column::Line: return toQString(std::to_string(index + 1));
    case dv::Column::Date: return toQString(dv::formatDate(row.record.systemNs));
    case dv::Column::Time:
        return toQString(m_clockTime ? dv::formatClockTime(row.record.systemNs) : dv::formatRelativeTime(row.record.steadyNs, *m_context.timeBase));
    case dv::Column::Pid: return QString::number(row.record.pid);
    case dv::Column::Process: return processName(row.record);
    case dv::Column::Bookmark:
    case dv::Column::Message:
    case dv::Column::Count: break;
    }
    return QString();
}

void LogViewWidget::drawMessage(QPainter& painter, const QRect& rect, const Row& row, const QColor& foreground)
{
    std::string_view text = row.text;
    auto spans = m_view->highlights(text);
    if (!m_highlightLower.empty())
    {
        for (auto pos = dv::findIgnoreCase(text, m_highlightLower); pos != std::string_view::npos; pos = dv::findIgnoreCase(text, m_highlightLower, pos + m_highlightLower.size()))
            spans.push_back(dv::HighlightSpan{pos, pos + m_highlightLower.size(), dv::Color::fromRgb(255, 255, 55), dv::Color::fromRgb(0, 0, 0)});
    }

    QFontMetrics fm(font());
    int x = rect.left() + textMargin;
    auto right = rect.right() - textMargin;
    auto baseline = rect.top() + (rect.height() - fm.height()) / 2 + fm.ascent();
    painter.setClipRect(rect);
    painter.setPen(foreground);
    if (m_context.prefixPid)
    {
        auto prefix = "[" + QString::number(row.record.pid) + "] ";
        painter.drawText(x, baseline, prefix);
        x += fm.width(prefix);
    }
    if (spans.empty())
    {
        painter.drawText(x, baseline, fm.elidedText(expandTabs(text), Qt::ElideRight, right - x));
        painter.setClipping(false);
        return;
    }

    // split the message at span boundaries; later spans override earlier ones
    std::vector<size_t> cuts{0, text.size()};
    for (auto& s : spans)
    {
        cuts.push_back(std::min(s.begin, text.size()));
        cuts.push_back(std::min(s.end, text.size()));
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    for (size_t i = 0; i + 1 < cuts.size() && x < right; ++i)
    {
        auto a = cuts[i];
        auto b = cuts[i + 1];
        const dv::HighlightSpan* span = nullptr;
        for (auto& s : spans)
            if (s.begin <= a && s.end >= b)
                span = &s;
        auto segment = expandTabs(text.substr(a, b - a));
        auto width = fm.width(segment);
        if (span)
        {
            painter.fillRect(QRect(x, rect.top(), width, rect.height()), toQColor(span->background));
            painter.setPen(toQColor(span->foreground));
        }
        else
        {
            painter.setPen(foreground);
        }
        painter.drawText(x, baseline, segment);
        x += width;
    }
    painter.setClipping(false);
}

void LogViewWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(viewport());
    painter.setFont(font());
    auto palette = this->palette();
    auto base = palette.color(QPalette::Base);
    auto text = palette.color(QPalette::Text);
    painter.fillRect(viewport()->rect(), base);

    auto first = static_cast<size_t>(verticalScrollBar()->value());
    auto rows = static_cast<size_t>(visibleRows() + 1);
    std::vector<dv::ViewLine> lines;
    m_view->lines(first, rows, lines);
    auto offset = horizontalScrollBar()->value();
    QFontMetrics fm(font());

    for (size_t i = 0; i < lines.size(); ++i)
    {
        Row row;
        row.seq = lines[i].seq;
        row.style = lines[i].style;
        row.chunk = m_context.store->chunkFor(row.seq);
        if (!row.chunk)
            continue; // dropped by the history limit
        row.record = row.chunk->record(static_cast<uint32_t>(row.seq - row.chunk->firstSeq()));
        row.text = row.chunk->text(row.record);
        auto index = first + i;
        int y = static_cast<int>(i) * m_rowHeight;
        QRect rowRect(0, y, viewport()->width(), m_rowHeight);

        QColor background = base;
        QColor foreground = text;
        auto style = row.style ? m_view->style(row.style) : dv::RowStyle{};
        if (!style.isDefault)
        {
            background = toQColor(style.background);
            foreground = toQColor(style.foreground);
        }
        else if (m_processColors)
        {
            background = processColor(row.record.originId);
        }
        bool selected = isSelected(row.seq);
        if (selected)
        {
            background = palette.color(QPalette::Highlight);
            foreground = palette.color(QPalette::HighlightedText);
        }
        painter.fillRect(rowRect, background);

        for (int visual = 0; visual < columnCount; ++visual)
        {
            auto column = m_header->logicalIndex(visual);
            if (column < 0 || m_header->isSectionHidden(column))
                continue;
            QRect cell(m_header->sectionPosition(column) - offset, y, m_header->sectionSize(column), m_rowHeight);
            if (cell.right() < 0 || cell.left() > viewport()->width())
                continue;
            if (column == static_cast<int>(dv::Column::Bookmark))
            {
                if (m_bookmarks.count(row.seq))
                    painter.drawPixmap(cell.left() + (cell.width() - 16) / 2, y + (m_rowHeight - 16) / 2, m_bookmarkIcon);
                continue;
            }
            if (column == static_cast<int>(dv::Column::Message))
            {
                drawMessage(painter, cell, row, foreground);
                continue;
            }
            auto align = (column == static_cast<int>(dv::Column::Process)) ? Qt::AlignLeft : Qt::AlignRight;
            painter.setPen(foreground);
            auto inner = cell.adjusted(textMargin, 0, -textMargin, 0);
            painter.drawText(inner, align | Qt::AlignVCenter, fm.elidedText(columnText(column, index, row), Qt::ElideRight, inner.width()));
        }
        if (row.seq == m_focusSeq && hasFocus())
        {
            painter.setPen(QPen(palette.color(QPalette::Text), 1, Qt::DotLine));
            painter.drawRect(rowRect.adjusted(0, 0, -1, -1));
        }
    }
}

bool LogViewWidget::isSelected(uint64_t seq) const
{
    if (m_selectAll)
        return true;
    bool inRange = m_anchorSeq != UINT64_MAX && seq >= std::min(m_anchorSeq, m_focusSeq) && seq <= std::max(m_anchorSeq, m_focusSeq);
    return inRange != (m_toggled.count(seq) != 0);
}

bool LogViewWidget::hasSelection() const
{
    return m_selectAll || m_anchorSeq != UINT64_MAX || !m_toggled.empty();
}

size_t LogViewWidget::focusIndex() const
{
    if (m_focusSeq == UINT64_MAX)
        return SIZE_MAX;
    return m_view->lowerBound(m_focusSeq);
}

void LogViewWidget::setFocusIndex(size_t index, bool extend, bool toggle)
{
    if (m_rowCount == 0)
        return;
    index = std::min(index, m_rowCount - 1);
    auto seq = m_view->seqAt(index);
    if (seq == UINT64_MAX)
        return;
    m_selectAll = false;
    if (toggle)
    {
        if (!m_toggled.erase(seq))
            m_toggled.insert(seq);
    }
    else if (!extend || m_anchorSeq == UINT64_MAX)
    {
        m_anchorSeq = seq;
        m_toggled.clear();
    }
    else
    {
        m_toggled.clear();
    }
    m_focusSeq = seq;
    viewport()->update();
    notifySelection();
}

void LogViewWidget::setFocusSeq(uint64_t seq)
{
    if (m_rowCount == 0 || seq == UINT64_MAX)
        return;
    // nearest visible line at or before 'seq'
    auto index = m_view->lowerBound(seq);
    if (index >= m_rowCount || m_view->seqAt(index) != seq)
        index = index > 0 ? index - 1 : 0;
    setFocusIndex(index, false, false);
    setAutoScroll(false);
    ensureVisible(index, true);
}

void LogViewWidget::notifySelection()
{
    if (selectionChanged)
        selectionChanged();
}

void LogViewWidget::onUserScroll()
{
}

void LogViewWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    auto index = indexAtY(event->pos().y());
    if (index >= m_rowCount)
        return;
    auto column = m_header->logicalIndexAt(event->pos().x() + horizontalScrollBar()->value());
    if (event->button() == Qt::LeftButton && column == static_cast<int>(dv::Column::Bookmark))
    {
        auto seq = m_view->seqAt(index);
        if (!m_bookmarks.erase(seq))
            m_bookmarks.insert(seq);
        viewport()->update();
        return;
    }
    if (event->button() == Qt::RightButton && isSelected(m_view->seqAt(index)))
        return; // keep the selection for the context menu
    bool shift = event->modifiers() & Qt::ShiftModifier;
    bool ctrl = event->modifiers() & Qt::ControlModifier;
    setFocusIndex(index, shift, ctrl && !shift);
    if (m_autoScrollStop)
        setAutoScroll(index + 1 == m_rowCount);
}

void LogViewWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    // select the word under the cursor in the message column as highlight text
    auto index = indexAtY(event->pos().y());
    Row row;
    if (!rowAt(index, row))
        return;
    auto column = m_header->logicalIndexAt(event->pos().x() + horizontalScrollBar()->value());
    if (column != static_cast<int>(dv::Column::Message))
        return;
    QFontMetrics fm(font());
    auto message = expandTabs(row.text);
    int x = event->pos().x() + horizontalScrollBar()->value() - m_header->sectionPosition(column) - textMargin;
    if (m_context.prefixPid)
        x -= fm.width("[" + QString::number(row.record.pid) + "] ");
    // find the character under x
    int pos = 0;
    auto length = message.length();
    while (pos < length && fm.width(message.left(pos + 1)) < x)
        ++pos;
    auto isWordChar = [&](int i) { return i >= 0 && i < length && (message[i].isLetterOrNumber() || message[i] == QChar('_')); };
    if (!isWordChar(pos))
        return;
    int begin = pos;
    int end = pos;
    while (isWordChar(begin - 1))
        --begin;
    while (isWordChar(end + 1))
        ++end;
    setHighlightText(message.mid(begin, end - begin + 1));
    notifySelection();
}

void LogViewWidget::keyPressEvent(QKeyEvent* event)
{
    auto index = focusIndex();
    if (index == SIZE_MAX)
        index = static_cast<size_t>(verticalScrollBar()->value());
    bool shift = event->modifiers() & Qt::ShiftModifier;
    auto page = static_cast<size_t>(visibleRows());
    size_t target = index;
    switch (event->key())
    {
    case Qt::Key_Up: target = index > 0 ? index - 1 : 0; break;
    case Qt::Key_Down: target = index + 1; break;
    case Qt::Key_PageUp: target = index > page ? index - page : 0; break;
    case Qt::Key_PageDown: target = index + page; break;
    case Qt::Key_Home: target = 0; break;
    case Qt::Key_End: target = m_rowCount > 0 ? m_rowCount - 1 : 0; break;
    case Qt::Key_Escape:
        setHighlightText(QString());
        setAutoScroll(false);
        notifySelection();
        return;
    default:
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    if (m_rowCount == 0)
        return;
    target = std::min(target, m_rowCount - 1);
    setFocusIndex(target, shift, false);
    ensureVisible(target, false);
    if (m_autoScrollStop)
        setAutoScroll(target + 1 == m_rowCount);
}

void LogViewWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        if (zoomRequested)
            zoomRequested(event->angleDelta().y());
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
    // scrolling away from the end stops auto scrolling, scrolling back to the end resumes it
    if (m_autoScrollStop)
        setAutoScroll(verticalScrollBar()->value() >= verticalScrollBar()->maximum());
}

void LogViewWidget::contextMenuEvent(QContextMenuEvent* event)
{
    auto column = m_header->logicalIndexAt(event->pos().x() + horizontalScrollBar()->value());
    if (contextMenuRequested)
        contextMenuRequested(event->globalPos(), column == static_cast<int>(dv::Column::Process));
}

void LogViewWidget::selectAll()
{
    m_selectAll = true;
    viewport()->update();
    notifySelection();
}

std::vector<uint64_t> LogViewWidget::selectedSeqs() const
{
    std::vector<uint64_t> result;
    if (m_selectAll)
    {
        std::vector<dv::ViewLine> lines;
        m_view->lines(0, m_rowCount, lines);
        for (auto& l : lines)
            result.push_back(l.seq);
        return result;
    }
    if (m_anchorSeq != UINT64_MAX)
    {
        auto a = m_view->lowerBound(std::min(m_anchorSeq, m_focusSeq));
        auto b = m_view->lowerBound(std::max(m_anchorSeq, m_focusSeq));
        std::vector<dv::ViewLine> lines;
        m_view->lines(a, b - a + 1, lines);
        for (auto& l : lines)
            if (l.seq <= std::max(m_anchorSeq, m_focusSeq) && !m_toggled.count(l.seq))
                result.push_back(l.seq);
    }
    for (auto seq : m_toggled)
    {
        bool inRange = m_anchorSeq != UINT64_MAX && seq >= std::min(m_anchorSeq, m_focusSeq) && seq <= std::max(m_anchorSeq, m_focusSeq);
        if (!inRange)
            result.push_back(seq);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string LogViewWidget::lineAsLog(const Row& row) const
{
    std::string out;
    dv::appendDbLogLine(out, static_cast<double>(row.record.steadyNs - *m_context.timeBase) / 1e9, row.record.systemNs, row.record.pid,
        m_context.store->origins().get(row.record.originId).processName, row.text);
    return out;
}

std::string LogViewWidget::selectionAsLog() const
{
    std::string out;
    for (auto seq : selectedSeqs())
    {
        dv::LineRecord rec{};
        std::string text;
        if (m_context.store->get(seq, rec, text))
            dv::appendDbLogLine(out, static_cast<double>(rec.steadyNs - *m_context.timeBase) / 1e9, rec.systemNs, rec.pid, m_context.store->origins().get(rec.originId).processName, text);
    }
    return out;
}

std::string LogViewWidget::viewAsLog() const
{
    std::string out;
    std::vector<dv::ViewLine> lines;
    m_view->lines(0, m_view->size(), lines);
    for (auto& l : lines)
    {
        Row row;
        row.chunk = m_context.store->chunkFor(l.seq);
        if (!row.chunk)
            continue;
        row.record = row.chunk->record(static_cast<uint32_t>(l.seq - row.chunk->firstSeq()));
        row.text = row.chunk->text(row.record);
        out += lineAsLog(row);
    }
    return out;
}

void LogViewWidget::copy(bool messagesOnly) const
{
    if (!m_highlight.isEmpty() && !messagesOnly)
    {
        QApplication::clipboard()->setText(m_highlight);
        return;
    }
    std::string out;
    for (auto seq : selectedSeqs())
    {
        dv::LineRecord rec{};
        std::string text;
        if (!m_context.store->get(seq, rec, text))
            continue;
        if (!messagesOnly)
        {
            auto index = m_view->lowerBound(seq);
            out += std::to_string(index + 1) + "\t";
            out += (m_clockTime ? dv::formatClockTime(rec.systemNs) : dv::formatRelativeTime(rec.steadyNs, *m_context.timeBase)) + "\t";
            out += std::to_string(rec.pid) + "\t";
            out += m_context.store->origins().get(rec.originId).processName + "\t";
        }
        out += text;
        out += "\r\n";
    }
    QApplication::clipboard()->setText(toQString(out));
}

std::vector<std::string> LogViewWidget::selectedProcesses() const
{
    std::set<std::string> names;
    for (auto seq : selectedSeqs())
    {
        auto chunk = m_context.store->chunkFor(seq);
        if (!chunk)
            continue;
        auto& rec = chunk->record(static_cast<uint32_t>(seq - chunk->firstSeq()));
        names.insert(m_context.store->origins().get(rec.originId).processName);
    }
    return {names.begin(), names.end()};
}

std::vector<std::string> LogViewWidget::selectedMessages(size_t max) const
{
    std::vector<std::string> result;
    for (auto seq : selectedSeqs())
    {
        if (result.size() >= max)
            break;
        dv::LineRecord rec{};
        std::string text;
        if (m_context.store->get(seq, rec, text))
            result.push_back(text);
    }
    return result;
}

void LogViewWidget::toggleBookmark()
{
    if (m_focusSeq == UINT64_MAX)
        return;
    if (!m_bookmarks.erase(m_focusSeq))
        m_bookmarks.insert(m_focusSeq);
    viewport()->update();
}

void LogViewWidget::nextBookmark(bool forward)
{
    if (m_bookmarks.empty())
        return;
    auto current = m_focusSeq == UINT64_MAX ? (forward ? 0 : UINT64_MAX) : m_focusSeq;
    std::optional<uint64_t> target;
    if (forward)
    {
        auto it = m_bookmarks.upper_bound(current);
        target = it != m_bookmarks.end() ? *it : *m_bookmarks.begin(); // wrap around
    }
    else
    {
        auto it = m_bookmarks.lower_bound(current);
        target = it != m_bookmarks.begin() ? *std::prev(it) : *m_bookmarks.rbegin();
    }
    auto index = m_view->lowerBound(*target);
    if (index >= m_rowCount)
        return;
    setFocusIndex(index, false, false);
    setAutoScroll(false);
    ensureVisible(index, true);
}

void LogViewWidget::clearBookmarks()
{
    m_bookmarks.clear();
    viewport()->update();
}

bool LogViewWidget::find(const QString& text, bool forward)
{
    if (text.isEmpty() || m_rowCount == 0)
        return false;
    auto needle = toStd(text);
    bool ascii = std::all_of(needle.begin(), needle.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    auto lower = toLowerAscii(needle);
    auto matches = [&](std::string_view message) {
        if (ascii)
            return dv::containsIgnoreCase(message, lower);
        return toQString(message).contains(text, Qt::CaseInsensitive);
    };

    auto start = focusIndex();
    if (start == SIZE_MAX)
        start = forward ? SIZE_MAX : m_rowCount; // so that the first candidate is 0 or the last line
    constexpr size_t batch = 4096;
    std::vector<dv::ViewLine> lines;
    // scan the view once, wrapping around
    for (size_t scanned = 0; scanned < m_rowCount;)
    {
        size_t from;
        if (forward)
            from = (start + 1 + scanned) % m_rowCount;
        else
            from = (start + m_rowCount - 1 - scanned % m_rowCount) % m_rowCount;
        auto count = std::min(batch, m_rowCount - scanned);
        if (forward)
        {
            count = std::min(count, m_rowCount - from);
            m_view->lines(from, count, lines);
            for (size_t i = 0; i < lines.size(); ++i)
            {
                Row row;
                if (!rowAt(from + i, row))
                    continue;
                if (matches(row.text))
                {
                    setFocusIndex(from + i, false, false);
                    setAutoScroll(false);
                    ensureVisible(from + i, true);
                    setHighlightText(text);
                    return true;
                }
            }
        }
        else
        {
            count = std::min(count, from + 1);
            auto begin = from + 1 - count;
            for (size_t i = from + 1; i-- > begin;)
            {
                Row row;
                if (!rowAt(i, row))
                    continue;
                if (matches(row.text))
                {
                    setFocusIndex(i, false, false);
                    setAutoScroll(false);
                    ensureVisible(i, true);
                    setHighlightText(text);
                    return true;
                }
            }
        }
        scanned += count;
    }
    return false;
}

QString LogViewWidget::statusText() const
{
    auto seqs = selectedSeqs();
    if (seqs.empty())
        return QString();
    dv::LineRecord first{};
    dv::LineRecord last{};
    std::string text;
    if (!m_context.store->get(seqs.front(), first, text) || !m_context.store->get(seqs.back(), last, text))
        return QString();
    if (seqs.size() == 1)
        return QString("Selected: line %1, %2 %3").formatArg(toQString(std::to_string(m_view->lowerBound(seqs.front()) + 1)))
            .formatArg(toQString(dv::formatDate(first.systemNs))).formatArg(toQString(dv::formatClockTime(first.systemNs)));
    auto span = static_cast<double>(last.steadyNs - first.steadyNs) / 1e9;
    return QString("Selected: %1 lines, %2 s").formatArg(toQString(std::to_string(seqs.size()))).formatArg(QString::number(span, 'f', 6));
}
