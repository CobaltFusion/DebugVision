#pragma once

#include <QDialog>

#include <string>
#include <vector>

class QLineEdit;

// Run a program and capture its stdout/stderr
class RunDialog : public QDialog
{
    CS_OBJECT(RunDialog)

public:
    explicit RunDialog(const QString& program = QString(), QWidget* parent = nullptr);

    std::string program() const;
    std::vector<std::string> arguments() const;
    std::string workingDirectory() const;

private:
    QLineEdit* m_program = nullptr;
    QLineEdit* m_arguments = nullptr;
    QLineEdit* m_directory = nullptr;
};

// splits a command line into arguments: whitespace separated, "double quotes" group
std::vector<std::string> splitArguments(const std::string& text);
