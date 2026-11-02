#pragma once

#include "dvcore/Filter.h"
#include "dvcore/Timeline.h"

#include <QDialog>

#include <vector>

class QLineEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;
class TimelineRulesEditor;

// Edits the name, the message/process filters and the timeline rules of a view
class FilterDialog : public QDialog
{
    CS_OBJECT(FilterDialog)

public:
    FilterDialog(const QString& name, const dv::FilterSet& filters, const std::vector<dv::TimelineRule>& rules, QWidget* parent = nullptr);

    QString name() const;
    dv::FilterSet filters() const;
    std::vector<dv::TimelineRule> timelineRules() const;

    void showTimelineRules();

    void accept() override;

private:
    QTableWidget* createTable(bool process);
    QTableWidget* currentTable() const; // nullptr on the timeline rules tab
    void addRow(QTableWidget* table, const dv::Filter& filter, bool process);
    std::vector<dv::Filter> readTable(QTableWidget* table) const;
    void setFilters(const dv::FilterSet& filters, bool append);
    void moveRow(int delta);
    void updateButtons();
    void load();
    void save();
    void showRegexHelp();

    QLineEdit* m_name = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTableWidget* m_messages = nullptr;
    QTableWidget* m_processes = nullptr;
    TimelineRulesEditor* m_timeline = nullptr;
    QPushButton* m_add = nullptr;
    QPushButton* m_addSpan = nullptr;
    QPushButton* m_addEvent = nullptr;
    QPushButton* m_load = nullptr;
    QPushButton* m_save = nullptr;
};
