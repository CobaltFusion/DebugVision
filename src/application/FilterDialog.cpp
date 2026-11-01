#include "FilterDialog.h"

#include "Text.h"

#include "dvcore/FilterEngine.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

enum TableColumn
{
    ColEnabled,
    ColText,
    ColMatch,
    ColType,
    ColBackground,
    ColForeground,
    ColCount,
};

const std::vector<dv::FilterType> messageTypes = {dv::FilterType::Include, dv::FilterType::Exclude, dv::FilterType::Highlight, dv::FilterType::Bookmark,
    dv::FilterType::Token, dv::FilterType::Stop, dv::FilterType::Track, dv::FilterType::Once, dv::FilterType::Clear, dv::FilterType::Beep};
const std::vector<dv::FilterType> processTypes = {dv::FilterType::Include, dv::FilterType::Exclude, dv::FilterType::Highlight, dv::FilterType::Stop,
    dv::FilterType::Track, dv::FilterType::Once, dv::FilterType::Beep};
const std::vector<dv::MatchType> messageMatches = {dv::MatchType::Simple, dv::MatchType::Wildcard, dv::MatchType::Regex, dv::MatchType::RegexGroups, dv::MatchType::RegexCase};
const std::vector<dv::MatchType> processMatches = {dv::MatchType::Simple, dv::MatchType::Wildcard, dv::MatchType::Regex};

constexpr int colorRole = Qt::UserRole + 1;
constexpr int autoRole = Qt::UserRole + 2;

// a button that shows and edits a dv::Color; 'Auto' is offered for backgrounds of message filters
class ColorButton : public QPushButton
{
public:
    ColorButton(dv::Color color, bool allowAuto, QWidget* parent) :
        QPushButton(parent),
        m_color(color),
        m_allowAuto(allowAuto)
    {
        update();
        connect(this, &QPushButton::clicked, this, [this](bool) { choose(); });
    }

    dv::Color color() const { return m_color; }

private:
    void choose()
    {
        if (m_allowAuto)
        {
            QMenu menu(this);
            auto pick = menu.addAction("Choose color...");
            auto automatic = menu.addAction("Auto (a color per distinct match)");
            auto chosen = menu.exec(mapToGlobal(QPoint(0, height())));
            if (chosen == automatic)
            {
                m_color = dv::Color::autoColor();
                update();
                return;
            }
            if (chosen != pick)
                return;
        }
        auto initial = m_color.isAuto ? QColor(Qt::white) : QColor(m_color.red(), m_color.green(), m_color.blue());
        auto c = QColorDialog::getColor(initial, this);
        if (!c.isValid())
            return;
        m_color = dv::Color::fromRgb(static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()), static_cast<uint8_t>(c.blue()));
        update();
    }

    void update()
    {
        if (m_color.isAuto)
        {
            setText("Auto");
            setStyleSheet(QString());
            return;
        }
        setText(QString());
        setStyleSheet(QString("background-color: rgb(%1,%2,%3)").formatArg(QString::number(m_color.red())).formatArg(QString::number(m_color.green())).formatArg(QString::number(m_color.blue())));
    }

    dv::Color m_color;
    bool m_allowAuto;
};

} // namespace

FilterDialog::FilterDialog(const QString& name, const dv::FilterSet& filters, QWidget* parent) :
    QDialog(parent)
{
    setWindowTitle("Filters");
    resize(820, 480);
    auto layout = new QVBoxLayout(this);

    auto nameRow = new QHBoxLayout;
    nameRow->addWidget(new QLabel("Name:"));
    m_name = new QLineEdit(name);
    nameRow->addWidget(m_name);
    layout->addLayout(nameRow);

    m_tabs = new QTabWidget;
    m_messages = createTable(false);
    m_processes = createTable(true);
    m_tabs->addTab(m_messages, "Messages");
    m_tabs->addTab(m_processes, "Processes");
    layout->addWidget(m_tabs);

    auto buttons = new QHBoxLayout;
    auto add = new QPushButton("Add");
    auto remove = new QPushButton("Remove");
    auto up = new QPushButton("Move Up");
    auto down = new QPushButton("Move Down");
    auto clear = new QPushButton("Remove All");
    auto load = new QPushButton("Load...");
    auto save = new QPushButton("Save...");
    auto help = new QPushButton("RegEx...");
    for (auto b : {add, remove, up, down, clear})
        buttons->addWidget(b);
    buttons->addStretch();
    for (auto b : {load, save, help})
        buttons->addWidget(b);
    layout->addLayout(buttons);

    auto box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &FilterDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &FilterDialog::reject);

    connect(add, &QPushButton::clicked, this, [this](bool) {
        auto table = currentTable();
        addRow(table, dv::Filter{}, table == m_processes);
        table->setCurrentCell(table->rowCount() - 1, ColText);
        table->editItem(table->item(table->rowCount() - 1, ColText));
    });
    connect(remove, &QPushButton::clicked, this, [this](bool) {
        auto table = currentTable();
        if (table->currentRow() >= 0)
            table->removeRow(table->currentRow());
    });
    connect(up, &QPushButton::clicked, this, [this](bool) { moveRow(-1); });
    connect(down, &QPushButton::clicked, this, [this](bool) { moveRow(1); });
    connect(clear, &QPushButton::clicked, this, [this](bool) { currentTable()->setRowCount(0); });
    connect(load, &QPushButton::clicked, this, [this](bool) { this->load(); });
    connect(save, &QPushButton::clicked, this, [this](bool) { this->save(); });
    connect(help, &QPushButton::clicked, this, [this](bool) { showRegexHelp(); });

    setFilters(filters, false);
}

QTableWidget* FilterDialog::createTable(bool)
{
    auto table = new QTableWidget(0, ColCount);
    QStringList headers;
    headers << "" << "Filter" << "Match" << "Type" << "Bg" << "Fg";
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(ColText, QHeaderView::Stretch);
    table->setColumnWidth(ColEnabled, 28);
    table->setColumnWidth(ColMatch, 110);
    table->setColumnWidth(ColType, 100);
    table->setColumnWidth(ColBackground, 60);
    table->setColumnWidth(ColForeground, 60);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    return table;
}

QTableWidget* FilterDialog::currentTable() const
{
    return m_tabs->currentIndex() == 1 ? m_processes : m_messages;
}

void FilterDialog::addRow(QTableWidget* table, const dv::Filter& filter, bool process)
{
    auto row = table->rowCount();
    table->insertRow(row);

    auto enabled = new QTableWidgetItem;
    enabled->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    enabled->setCheckState(filter.enabled ? Qt::Checked : Qt::Unchecked);
    table->setItem(row, ColEnabled, enabled);
    table->setItem(row, ColText, new QTableWidgetItem(toQString(filter.text)));

    auto match = new QComboBox;
    auto& matches = process ? processMatches : messageMatches;
    for (auto m : matches)
        match->addItem(QString::fromUtf8(dv::toText(m)), static_cast<int>(m));
    match->setCurrentIndex(std::max(0, match->findData(static_cast<int>(filter.matchType))));
    table->setCellWidget(row, ColMatch, match);

    auto type = new QComboBox;
    auto& types = process ? processTypes : messageTypes;
    for (auto t : types)
        type->addItem(QString::fromUtf8(dv::toText(t)), static_cast<int>(t));
    type->setCurrentIndex(std::max(0, type->findData(static_cast<int>(filter.filterType))));
    table->setCellWidget(row, ColType, type);

    // RegexGroups is only meaningful for Token filters
    connect(match, &QComboBox::cs_currentIndexChanged, type, [match, type](int) {
        if (match->currentData().toInt() == static_cast<int>(dv::MatchType::RegexGroups))
            type->setCurrentIndex(type->findData(static_cast<int>(dv::FilterType::Token)));
    });

    table->setCellWidget(row, ColBackground, new ColorButton(filter.background, !process, table));
    table->setCellWidget(row, ColForeground, new ColorButton(filter.foreground, false, table));
}

std::vector<dv::Filter> FilterDialog::readTable(QTableWidget* table) const
{
    std::vector<dv::Filter> result;
    for (int row = 0; row < table->rowCount(); ++row)
    {
        dv::Filter f;
        f.enabled = table->item(row, ColEnabled)->checkState() == Qt::Checked;
        f.text = toStd(table->item(row, ColText) ? table->item(row, ColText)->text() : QString());
        f.matchType = static_cast<dv::MatchType>(static_cast<QComboBox*>(table->cellWidget(row, ColMatch))->currentData().toInt());
        f.filterType = static_cast<dv::FilterType>(static_cast<QComboBox*>(table->cellWidget(row, ColType))->currentData().toInt());
        f.background = static_cast<ColorButton*>(table->cellWidget(row, ColBackground))->color();
        f.foreground = static_cast<ColorButton*>(table->cellWidget(row, ColForeground))->color();
        if (f.text.empty())
            continue;
        result.push_back(f);
    }
    return result;
}

void FilterDialog::setFilters(const dv::FilterSet& filters, bool append)
{
    if (!append)
    {
        m_messages->setRowCount(0);
        m_processes->setRowCount(0);
    }
    for (auto& f : filters.messageFilters)
        addRow(m_messages, f, false);
    for (auto& f : filters.processFilters)
        addRow(m_processes, f, true);
}

QString FilterDialog::name() const
{
    return m_name->text();
}

dv::FilterSet FilterDialog::filters() const
{
    dv::FilterSet set;
    set.name = toStd(name());
    set.messageFilters = readTable(m_messages);
    set.processFilters = readTable(m_processes);
    return set;
}

void FilterDialog::moveRow(int delta)
{
    auto table = currentTable();
    auto row = table->currentRow();
    auto target = row + delta;
    if (row < 0 || target < 0 || target >= table->rowCount())
        return;
    auto filters = readTable(table); // rows with empty text are dropped; rebuild the table
    if (row >= static_cast<int>(filters.size()) || target >= static_cast<int>(filters.size()))
        return;
    std::swap(filters[static_cast<size_t>(row)], filters[static_cast<size_t>(target)]);
    bool process = table == m_processes;
    table->setRowCount(0);
    for (auto& f : filters)
        addRow(table, f, process);
    table->setCurrentCell(target, ColText);
}

void FilterDialog::load()
{
    auto file = QFileDialog::getOpenFileName(this, "Load Filters", QString(), "Filter files (*.json *.xml);;All files (*)");
    if (file.isEmpty())
        return;
    try
    {
        auto set = dv::loadFilterFile(toStd(file));
        // as in DebugView++: loaded filters are appended, the name is replaced
        setFilters(set, true);
        if (!set.name.empty())
            m_name->setText(toQString(set.name));
    }
    catch (const std::exception& e)
    {
        QMessageBox::warning(this, "Load Filters", toQString(e.what()));
    }
}

void FilterDialog::save()
{
    auto file = QFileDialog::getSaveFileName(this, "Save Filters", m_name->text() + ".json", "Filter files (*.json)");
    if (file.isEmpty())
        return;
    try
    {
        dv::saveFilterFile(toStd(file), filters());
    }
    catch (const std::exception& e)
    {
        QMessageBox::warning(this, "Save Filters", toQString(e.what()));
    }
}

void FilterDialog::showRegexHelp()
{
    QMessageBox::information(this, "Regular expressions",
        "Filters use PCRE2 regular expressions (Perl compatible), case-insensitive except for RegexCase.\n\n"
        ".        any character\n"
        "\\d \\w \\s  digit, word character, whitespace\n"
        "[abc]    one of a, b or c\n"
        "x* x+ x?  zero or more, one or more, optional\n"
        "x{2,5}   between 2 and 5 times\n"
        "^ $      start and end of the message\n"
        "a|b      a or b\n"
        "(...)    group; with RegexGroups only the groups are highlighted\n"
        "(?:...)  group without capture\n\n"
        "Simple: the text is matched literally.\n"
        "Wildcard: * matches any text, ? an optional character.");
}

void FilterDialog::accept()
{
    // compile the filters before closing, and point at the row with an error
    try
    {
        dv::FilterEngine engine(filters());
    }
    catch (const dv::FilterError& e)
    {
        auto table = e.processFilter ? m_processes : m_messages;
        m_tabs->setCurrentWidget(table);
        // readTable() skips empty rows; find the table row of the n-th non-empty filter
        int count = -1;
        for (int row = 0; row < table->rowCount(); ++row)
        {
            if (table->item(row, ColText) && !table->item(row, ColText)->text().isEmpty())
                ++count;
            if (count == static_cast<int>(e.index))
            {
                table->setCurrentCell(row, ColText);
                break;
            }
        }
        QMessageBox::warning(this, "Filters", toQString(e.what()));
        return;
    }
    QDialog::accept();
}
