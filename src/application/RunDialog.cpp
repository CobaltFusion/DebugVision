#include "RunDialog.h"

#include "Text.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

std::vector<std::string> splitArguments(const std::string& text)
{
    std::vector<std::string> args;
    std::string current;
    bool quoted = false;
    bool any = false;
    for (char c : text)
    {
        if (c == '"')
        {
            quoted = !quoted;
            any = true;
        }
        else if ((c == ' ' || c == '\t') && !quoted)
        {
            if (any)
                args.push_back(current);
            current.clear();
            any = false;
        }
        else
        {
            current.push_back(c);
            any = true;
        }
    }
    if (any)
        args.push_back(current);
    return args;
}

RunDialog::RunDialog(const QString& program, QWidget* parent) :
    QDialog(parent)
{
    setWindowTitle("Run");
    resize(560, 160);
    auto layout = new QVBoxLayout(this);
    auto form = new QFormLayout;
    layout->addLayout(form);

    auto programRow = new QHBoxLayout;
    m_program = new QLineEdit(program);
    auto browse = new QPushButton("Browse...");
    programRow->addWidget(m_program);
    programRow->addWidget(browse);
    form->addRow("Program", programRow);
    m_arguments = new QLineEdit;
    form->addRow("Arguments", m_arguments);
    m_directory = new QLineEdit;
    m_directory->setPlaceholderText("current directory");
    form->addRow("Working directory", m_directory);

    auto box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &RunDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &RunDialog::reject);
    connect(browse, &QPushButton::clicked, this, [this](bool) {
        auto file = QFileDialog::getOpenFileName(this, "Run", m_program->text());
        if (!file.isEmpty())
            m_program->setText(file);
    });
}

std::string RunDialog::program() const
{
    return toStd(m_program->text().trimmed());
}

std::vector<std::string> RunDialog::arguments() const
{
    return splitArguments(toStd(m_arguments->text()));
}

std::string RunDialog::workingDirectory() const
{
    return toStd(m_directory->text().trimmed());
}
