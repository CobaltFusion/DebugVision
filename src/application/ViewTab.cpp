#include "ViewTab.h"

ViewTab::ViewTab(LogViewWidget* logView) :
    QSplitter(Qt::Vertical),
    m_logView(logView)
{
    setChildrenCollapsible(false);
    addWidget(logView);
}
