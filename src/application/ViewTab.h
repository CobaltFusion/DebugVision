#pragma once

#include "LogViewWidget.h"

#include <QSplitter>

// The contents of a view tab: the log list, with the timeline below it
class ViewTab : public QSplitter
{
    CS_OBJECT(ViewTab)

public:
    explicit ViewTab(LogViewWidget* logView);

    LogViewWidget* logView() const { return m_logView; }

    // the tab that holds a log view
    static ViewTab* of(LogViewWidget* logView) { return static_cast<ViewTab*>(logView->parentWidget()); }

private:
    LogViewWidget* m_logView;
};
