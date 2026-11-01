#pragma once

#include "dvcore/Filter.h"

#include <QDialog>

class QLineEdit;
class QTabWidget;
class QTableWidget;

// Edits the name and the message/process filters of a view
class FilterDialog : public QDialog
{
    CS_OBJECT(FilterDialog)

public:
    FilterDialog(const QString& name, const dv::FilterSet& filters, QWidget* parent = nullptr);

    QString name() const;
    dv::FilterSet filters() const;

    void accept() override;

private:
    QTableWidget* createTable(bool process);
    QTableWidget* currentTable() const;
    void addRow(QTableWidget* table, const dv::Filter& filter, bool process);
    std::vector<dv::Filter> readTable(QTableWidget* table) const;
    void setFilters(const dv::FilterSet& filters, bool append);
    void moveRow(int delta);
    void load();
    void save();
    void showRegexHelp();

    QLineEdit* m_name = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTableWidget* m_messages = nullptr;
    QTableWidget* m_processes = nullptr;
};
