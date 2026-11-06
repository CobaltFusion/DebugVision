#include "SourcesDialog.h"

#include "Text.h"
#include "UnderlinedHeaderView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// "bindAddress" -> "Bind address"
QString humanize(const std::string& key)
{
    std::string out;
    for (size_t i = 0; i < key.size(); ++i)
    {
        auto c = key[i];
        if (i == 0)
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        else if (std::isupper(static_cast<unsigned char>(c)))
        {
            out.push_back(' ');
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        else
            out.push_back(c);
    }
    return toQString(out);
}

// fields with a fixed set of values
const std::map<std::string, std::vector<std::string>> choices = {
    {"scope", {"local", "global", "both"}},
    {"startAt", {"beginning", "end"}},
    {"format", {"auto", "text", "dblog", "sysinternals"}},
};

std::string joinList(const nlohmann::json& value)
{
    std::string out;
    if (value.is_array())
    {
        for (auto& v : value)
        {
            if (!out.empty())
                out += ";";
            out += v.is_string() ? v.get<std::string>() : v.dump();
        }
    }
    else if (value.is_object())
    {
        for (auto& [k, v] : value.items())
        {
            if (!out.empty())
                out += ";";
            out += k + "=" + (v.is_string() ? v.get<std::string>() : v.dump());
        }
    }
    return out;
}

std::vector<std::string> splitList(const std::string& text)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= text.size())
    {
        auto semi = text.find(';', pos);
        auto item = text.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        if (!item.empty())
            out.push_back(item);
        pos = semi == std::string::npos ? text.size() + 1 : semi + 1;
    }
    return out;
}

} // namespace

SourceEditDialog::SourceEditDialog(const dv::SourceConfig& config, QWidget* parent) :
    QDialog(parent),
    m_config(config)
{
    auto type = dv::SourceRegistry::instance().find(config.type);
    setWindowTitle("Source: " + toQString(type ? type->displayName : config.type));
    auto layout = new QVBoxLayout(this);
    auto form = new QFormLayout;
    layout->addLayout(form);

    auto name = new QLineEdit(toQString(config.name));
    name->setPlaceholderText(toQString(dv::describeSource(dv::SourceConfig{"", config.type, "", true, true, false, "auto", "", "", config.params})));
    form->addRow("Name", name);
    auto enabled = new QCheckBox;
    enabled->setChecked(config.enabled);
    form->addRow("Enabled", enabled);

    // type specific parameters, generated from the JSON parameters (defaults fill in missing fields)
    nlohmann::json params = type ? type->defaultParams : nlohmann::json::object();
    for (auto& [key, value] : config.params.items())
        params[key] = value;
    std::vector<std::function<void(nlohmann::json&)>> readers;
    for (auto& [key, value] : params.items())
    {
        auto label = humanize(key);
        std::string k = key;
        if (auto c = choices.find(key); c != choices.end() && value.is_string())
        {
            auto combo = new QComboBox;
            for (auto& option : c->second)
                combo->addItem(toQString(option));
            combo->setCurrentIndex(std::max(0, combo->findText(toQString(value.get<std::string>()))));
            form->addRow(label, combo);
            readers.push_back([combo, k](nlohmann::json& p) { p[k] = toStd(combo->currentText()); });
        }
        else if (value.is_boolean())
        {
            auto check = new QCheckBox;
            check->setChecked(value.get<bool>());
            form->addRow(label, check);
            readers.push_back([check, k](nlohmann::json& p) { p[k] = check->isChecked(); });
        }
        else if (value.is_number_integer())
        {
            auto spin = new QSpinBox;
            spin->setRange(0, 1 << 30);
            spin->setValue(value.get<int>());
            form->addRow(label, spin);
            readers.push_back([spin, k](nlohmann::json& p) { p[k] = spin->value(); });
        }
        else if (value.is_array() || value.is_object())
        {
            auto edit = new QLineEdit(toQString(joinList(value)));
            edit->setPlaceholderText(value.is_object() ? QString("NAME=value;NAME=value") : QString("item;item"));
            form->addRow(label, edit);
            bool isObject = value.is_object();
            readers.push_back([edit, k, isObject](nlohmann::json& p) {
                auto items = splitList(toStd(edit->text()));
                if (isObject)
                {
                    nlohmann::json obj = nlohmann::json::object();
                    for (auto& item : items)
                    {
                        auto eq = item.find('=');
                        if (eq != std::string::npos)
                            obj[item.substr(0, eq)] = item.substr(eq + 1);
                    }
                    p[k] = obj;
                }
                else
                {
                    p[k] = items;
                }
            });
        }
        else if (value.is_string())
        {
            auto edit = new QLineEdit(toQString(value.get<std::string>()));
            form->addRow(label, edit);
            readers.push_back([edit, k](nlohmann::json& p) { p[k] = toStd(edit->text()); });
        }
    }

    // common text settings
    auto encoding = new QComboBox;
    for (auto e : {dv::Encoding::Auto, dv::Encoding::Utf8, dv::Encoding::Utf8OrLegacy, dv::Encoding::Legacy, dv::Encoding::Utf16LE, dv::Encoding::Utf16BE})
        encoding->addItem(QString::fromUtf8(dv::toText(e)));
    encoding->setCurrentIndex(std::max(0, encoding->findText(toQString(config.encoding))));
    form->addRow("Encoding", encoding);
    auto charset = new QLineEdit(toQString(config.legacyCharset));
    charset->setPlaceholderText("default (e.g. windows-1252, cp437, oem)");
    form->addRow("Legacy charset", charset);
    auto autoNewline = new QCheckBox;
    autoNewline->setChecked(config.autoNewline);
    form->addRow("Auto newline", autoNewline);
    auto processName = new QLineEdit(toQString(config.processNameOverride));
    processName->setPlaceholderText("default");
    form->addRow("Process name", processName);

    auto box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &SourceEditDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &SourceEditDialog::reject);

    m_read = [=](dv::SourceConfig& c) {
        c.name = toStd(name->text());
        c.enabled = enabled->isChecked();
        for (auto& r : readers)
            r(c.params);
        c.encoding = toStd(encoding->currentText());
        c.legacyCharset = toStd(charset->text());
        c.autoNewline = autoNewline->isChecked();
        c.processNameOverride = toStd(processName->text());
    };
}

dv::SourceConfig SourceEditDialog::config() const
{
    auto c = m_config;
    m_read(c);
    return c;
}

void SourceEditDialog::accept()
{
    auto c = config();
    if (auto type = dv::SourceRegistry::instance().find(c.type))
    {
        auto error = type->validate(c);
        if (!error.empty())
        {
            QMessageBox::warning(this, windowTitle(), toQString(error));
            return;
        }
    }
    QDialog::accept();
}

SourcesDialog::SourcesDialog(std::vector<dv::SourceConfig> configs, std::vector<dv::SourceInfo> status, QWidget* parent) :
    QDialog(parent),
    m_configs(std::move(configs))
{
    setWindowTitle("Sources");
    resize(760, 360);
    auto layout = new QVBoxLayout(this);
    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeader(new UnderlinedHeaderView(Qt::Horizontal, m_table));
    QStringList headers;
    headers << "" << "Name" << "Type" << "Status";
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->setColumnWidth(0, 28);
    m_table->setColumnWidth(1, 220);
    m_table->setColumnWidth(2, 150);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table);

    auto buttons = new QHBoxLayout;
    auto add = new QPushButton("Add");
    auto editButton = new QPushButton("Edit...");
    auto duplicate = new QPushButton("Duplicate");
    auto remove = new QPushButton("Remove");
    for (auto b : {add, editButton, duplicate, remove})
        buttons->addWidget(b);
    buttons->addStretch();
    layout->addLayout(buttons);
    auto box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &SourcesDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &SourcesDialog::reject);

    // only the types available on this platform can be added (OutputDebugString only on Windows)
    auto addMenu = new QMenu(add);
    for (auto& type : dv::SourceRegistry::instance().types())
    {
        if (type.type == "stdin")
            continue; // created from the command line only
        auto action = addMenu->addAction(toQString(type.displayName));
        auto typeName = type.type;
        connect(action, &QAction::triggered, this, [this, typeName](bool) {
            auto config = dv::SourceRegistry::instance().makeDefault(typeName);
            SourceEditDialog dialog(config, this);
            if (dialog.exec() != QDialog::Accepted)
                return;
            m_configs.push_back(dialog.config());
            rebuild();
        });
    }
    add->setMenu(addMenu);

    connect(editButton, &QPushButton::clicked, this, [this](bool) { edit(m_table->currentRow()); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { edit(row); });
    connect(duplicate, &QPushButton::clicked, this, [this](bool) {
        auto row = m_table->currentRow();
        if (row < 0 || row >= static_cast<int>(m_configs.size()))
            return;
        auto copy = m_configs[static_cast<size_t>(row)];
        copy.id = dv::newSourceId();
        copy.name += copy.name.empty() ? "" : " (copy)";
        copy.enabled = false; // a copy would usually clash (same port, same DBWIN objects)
        m_configs.push_back(copy);
        rebuild();
    });
    connect(remove, &QPushButton::clicked, this, [this](bool) {
        auto row = m_table->currentRow();
        if (row < 0 || row >= static_cast<int>(m_configs.size()))
            return;
        m_configs.erase(m_configs.begin() + row);
        rebuild();
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() != 0 || item->row() >= static_cast<int>(m_configs.size()))
            return;
        m_configs[static_cast<size_t>(item->row())].enabled = item->checkState() == Qt::Checked;
    });

    updateStatus(status);
}

void SourcesDialog::edit(int row)
{
    if (row < 0 || row >= static_cast<int>(m_configs.size()))
        return;
    auto& config = m_configs[static_cast<size_t>(row)];
    if (!dv::SourceRegistry::instance().isAvailable(config.type))
    {
        QMessageBox::information(this, "Sources", "Source type '" + toQString(config.type) + "' is not available on this platform; it is kept in the settings.");
        return;
    }
    SourceEditDialog dialog(config, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    config = dialog.config();
    rebuild();
}

void SourcesDialog::updateStatus(const std::vector<dv::SourceInfo>& status)
{
    m_status.clear();
    for (auto& s : status)
        m_status[s.config.id] = s;
    rebuild();
}

void SourcesDialog::rebuild()
{
    auto current = m_table->currentRow();
    m_table->blockSignals(true);
    m_table->setRowCount(static_cast<int>(m_configs.size()));
    for (size_t i = 0; i < m_configs.size(); ++i)
    {
        auto& c = m_configs[i];
        auto row = static_cast<int>(i);
        bool available = dv::SourceRegistry::instance().isAvailable(c.type);
        auto enabled = new QTableWidgetItem;
        enabled->setFlags(available ? Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::ItemIsSelectable);
        enabled->setCheckState(c.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(row, 0, enabled);
        m_table->setItem(row, 1, new QTableWidgetItem(toQString(dv::describeSource(c))));
        auto type = dv::SourceRegistry::instance().find(c.type);
        m_table->setItem(row, 2, new QTableWidgetItem(toQString(type ? type->displayName : c.type)));
        QString status;
        if (!available)
            status = "not available on this platform";
        else if (auto it = m_status.find(c.id); it != m_status.end())
            status = toQString(it->second.status.text);
        else
            status = c.enabled ? QString("(apply to start)") : QString("disabled");
        m_table->setItem(row, 3, new QTableWidgetItem(status));
        if (!available)
            for (int col = 1; col < 4; ++col)
                m_table->item(row, col)->setForeground(QBrush(Qt::gray));
    }
    m_table->blockSignals(false);
    if (current >= 0 && current < m_table->rowCount())
        m_table->selectRow(current);
}
