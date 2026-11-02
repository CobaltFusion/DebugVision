#pragma once

#include "dvcore/Timeline.h"

#include <QWidget>

#include <vector>

class QTableWidget;

// Edits the timeline rules of a view: spans (a begin and an end pattern) and events (one pattern, with a marker shape).
// The Timeline Rules tab of the filter dialog, which provides the buttons.
class TimelineRulesEditor : public QWidget
{
    CS_OBJECT(TimelineRulesEditor)

public:
    explicit TimelineRulesEditor(const std::vector<dv::TimelineRule>& rules, QWidget* parent = nullptr);

    std::vector<dv::TimelineRule> rules() const;

    void addNew(dv::TimelineKind kind);
    void removeCurrent();
    void removeAll();
    void moveRow(int delta);

    // compiles the rules; on an error selects the rule's row and returns the error, else ""
    QString validate();

private:
    void addRow(const dv::TimelineRule& rule);
    void updateRow(int row);

    QTableWidget* m_table = nullptr;
};
