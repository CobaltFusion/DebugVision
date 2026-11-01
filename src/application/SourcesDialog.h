#pragma once

#include "dvcore/Ingest.h"
#include "dvcore/SourceConfig.h"

#include <QDialog>

#include <functional>
#include <map>
#include <vector>

class QTableWidget;
class QFormLayout;
class QWidget;

// Edits one source: the common settings plus a form generated from the type's parameters
class SourceEditDialog : public QDialog
{
    CS_OBJECT(SourceEditDialog)

public:
    SourceEditDialog(const dv::SourceConfig& config, QWidget* parent = nullptr);
    dv::SourceConfig config() const;
    void accept() override;

private:
    dv::SourceConfig m_config;
    std::function<void(dv::SourceConfig&)> m_read; // copies the form into a config
};

// Lists the configured sources with their live status; add, edit, remove, duplicate, enable/disable
class SourcesDialog : public QDialog
{
    CS_OBJECT(SourcesDialog)

public:
    SourcesDialog(std::vector<dv::SourceConfig> configs, std::vector<dv::SourceInfo> status, QWidget* parent = nullptr);

    std::vector<dv::SourceConfig> configs() const { return m_configs; }
    void updateStatus(const std::vector<dv::SourceInfo>& status);

private:
    void rebuild();
    void edit(int row);

    std::vector<dv::SourceConfig> m_configs;
    std::map<std::string, dv::SourceInfo> m_status;
    QTableWidget* m_table = nullptr;
};
