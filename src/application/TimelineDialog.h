#pragma once

#include "dvcore/Timeline.h"

#include <QDialog>

#include <vector>

class QTableWidget;

// Edits the timeline rules of a view: spans (a begin and an end pattern) and events (one pattern, with a marker shape)
class TimelineDialog : public QDialog
{
    CS_OBJECT(TimelineDialog)

public:
    TimelineDialog(const std::vector<dv::TimelineRule>& rules, QWidget* parent = nullptr);

    std::vector<dv::TimelineRule> rules() const;

    void accept() override;

private:
    void addRow(const dv::TimelineRule& rule);
    void addNew(dv::TimelineKind kind);
    void moveRow(int delta);
    void updateRow(int row);

    QTableWidget* m_table = nullptr;
};
