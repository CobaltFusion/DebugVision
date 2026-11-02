#include "TimelineRulesEditor.h"

#include "ColorButton.h"
#include "Text.h"
#include "UnderlinedHeaderView.h"

#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

enum TableColumn
{
    ColEnabled,
    ColName,
    ColKind,
    ColBegin,
    ColEnd,
    ColMatch,
    ColShape,
    ColColor,
    ColCount,
};

const std::vector<dv::MatchType> matchTypes = {dv::MatchType::Simple, dv::MatchType::Wildcard, dv::MatchType::Regex, dv::MatchType::RegexCase};

QComboBox* combo(QTableWidget* table, int row, int column)
{
    return static_cast<QComboBox*>(table->cellWidget(row, column));
}

QString cellText(QTableWidget* table, int row, int column)
{
    auto item = table->item(row, column);
    return item ? item->text() : QString();
}

} // namespace

TimelineRulesEditor::TimelineRulesEditor(const std::vector<dv::TimelineRule>& rules, QWidget* parent) :
    QWidget(parent)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto help = new QLabel(
        "A <b>span</b> draws a bar from a line matching <i>Begin</i> to the next line of the same process matching <i>End</i>. "
        "With capture groups both must capture the same text, so <tt>frame (\\d+) start</tt> pairs with <tt>frame (\\d+) done</tt> "
        "of the same frame. In a regular expression <i>End</i>, <tt>\\1</tt> to <tt>\\9</tt> refer to the groups of <i>Begin</i>: "
        "<tt>Enter (\\w+)</tt> pairs with <tt>Exit \\1</tt> of the same name. A bar shows what <i>Begin</i> captured, or else the name. "
        "An <b>event</b> marks every line matching <i>Begin</i>. Only the lines shown in the view are used.");
    help->setWordWrap(true);
    help->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(help);

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeader(new UnderlinedHeaderView(Qt::Horizontal, m_table));
    QStringList headers;
    headers << "" << "Name" << "Kind" << "Begin / Event" << "End" << "Match" << "Shape" << "Color";
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(ColBegin, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColEnd, QHeaderView::Stretch);
    m_table->setColumnWidth(ColEnabled, 28);
    m_table->setColumnWidth(ColName, 120);
    m_table->setColumnWidth(ColKind, 80);
    m_table->setColumnWidth(ColMatch, 100);
    m_table->setColumnWidth(ColShape, 90);
    m_table->setColumnWidth(ColColor, 60);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_table);

    for (auto& rule : rules)
        addRow(rule);
}

void TimelineRulesEditor::addRow(const dv::TimelineRule& rule)
{
    auto row = m_table->rowCount();
    m_table->insertRow(row);

    auto enabled = new QTableWidgetItem;
    enabled->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    enabled->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    m_table->setItem(row, ColEnabled, enabled);
    m_table->setItem(row, ColName, new QTableWidgetItem(toQString(rule.name)));
    m_table->setItem(row, ColBegin, new QTableWidgetItem(toQString(rule.begin)));
    m_table->setItem(row, ColEnd, new QTableWidgetItem(toQString(rule.end)));

    auto kind = new QComboBox;
    kind->addItem("Span", static_cast<int>(dv::TimelineKind::Span));
    kind->addItem("Event", static_cast<int>(dv::TimelineKind::Event));
    kind->setCurrentIndex(std::max(0, kind->findData(static_cast<int>(rule.kind))));
    m_table->setCellWidget(row, ColKind, kind);

    auto match = new QComboBox;
    for (auto m : matchTypes)
        match->addItem(QString::fromUtf8(dv::toText(m)), static_cast<int>(m));
    match->setCurrentIndex(std::max(0, match->findData(static_cast<int>(rule.matchType))));
    m_table->setCellWidget(row, ColMatch, match);

    auto shape = new QComboBox;
    for (auto s : {dv::MarkerShape::Lollipop, dv::MarkerShape::Square, dv::MarkerShape::Triangle})
        shape->addItem(QString::fromUtf8(dv::toText(s)), static_cast<int>(s));
    shape->setCurrentIndex(std::max(0, shape->findData(static_cast<int>(rule.shape))));
    m_table->setCellWidget(row, ColShape, shape);

    m_table->setCellWidget(row, ColColor, new ColorButton(rule.color, "a color per rule", m_table));

    // End only applies to spans, Shape only to events
    connect(kind, &QComboBox::cs_currentIndexChanged, this, [this, kind](int) {
        for (int r = 0; r < m_table->rowCount(); ++r)
            if (m_table->cellWidget(r, ColKind) == kind)
                updateRow(r);
    });
    updateRow(row);
}

void TimelineRulesEditor::updateRow(int row)
{
    bool span = combo(m_table, row, ColKind)->currentData().toInt() == static_cast<int>(dv::TimelineKind::Span);
    if (auto end = m_table->item(row, ColEnd))
        end->setFlags(span ? end->flags() | Qt::ItemIsEnabled | Qt::ItemIsEditable : end->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsEditable));
    combo(m_table, row, ColShape)->setEnabled(!span);
}

void TimelineRulesEditor::addNew(dv::TimelineKind kind)
{
    dv::TimelineRule rule;
    rule.kind = kind;
    addRow(rule);
    auto row = m_table->rowCount() - 1;
    m_table->setCurrentCell(row, ColBegin);
    m_table->editItem(m_table->item(row, ColBegin));
}

std::vector<dv::TimelineRule> TimelineRulesEditor::rules() const
{
    std::vector<dv::TimelineRule> result;
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        dv::TimelineRule r;
        r.enabled = m_table->item(row, ColEnabled)->checkState() == Qt::Checked;
        r.name = toStd(cellText(m_table, row, ColName));
        r.kind = static_cast<dv::TimelineKind>(combo(m_table, row, ColKind)->currentData().toInt());
        r.begin = toStd(cellText(m_table, row, ColBegin));
        if (r.kind == dv::TimelineKind::Span)
            r.end = toStd(cellText(m_table, row, ColEnd));
        r.matchType = static_cast<dv::MatchType>(combo(m_table, row, ColMatch)->currentData().toInt());
        r.shape = static_cast<dv::MarkerShape>(combo(m_table, row, ColShape)->currentData().toInt());
        r.color = static_cast<ColorButton*>(m_table->cellWidget(row, ColColor))->color();
        if (r.begin.empty() && r.end.empty() && r.name.empty())
            continue; // an untouched new row
        result.push_back(r);
    }
    return result;
}

void TimelineRulesEditor::removeCurrent()
{
    if (m_table->currentRow() >= 0)
        m_table->removeRow(m_table->currentRow());
}

void TimelineRulesEditor::removeAll()
{
    m_table->setRowCount(0);
}

void TimelineRulesEditor::moveRow(int delta)
{
    auto row = m_table->currentRow();
    auto target = row + delta;
    if (row < 0 || target < 0 || target >= m_table->rowCount())
        return;
    auto all = rules(); // empty rows are dropped; rebuild the table
    if (row >= static_cast<int>(all.size()) || target >= static_cast<int>(all.size()))
        return;
    std::swap(all[static_cast<size_t>(row)], all[static_cast<size_t>(target)]);
    m_table->setRowCount(0);
    for (auto& r : all)
        addRow(r);
    m_table->setCurrentCell(target, ColBegin);
}

QString TimelineRulesEditor::validate()
{
    // compile the rules, and point at the row with an error
    try
    {
        dv::TimelineModel model(rules());
    }
    catch (const dv::TimelineError& e)
    {
        // rules() skips empty rows; find the table row of the n-th rule
        int count = -1;
        for (int row = 0; row < m_table->rowCount(); ++row)
        {
            if (!cellText(m_table, row, ColBegin).isEmpty() || !cellText(m_table, row, ColEnd).isEmpty() || !cellText(m_table, row, ColName).isEmpty())
                ++count;
            if (count == static_cast<int>(e.index))
            {
                m_table->setCurrentCell(row, ColBegin);
                break;
            }
        }
        return toQString(e.what());
    }
    return QString();
}
