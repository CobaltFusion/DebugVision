#pragma once

// conversions between UTF-8 std::string / std::string_view and CopperSpice QString (which is UTF-8 internally)

#include <QString>

#include <string>
#include <string_view>

inline QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<QString::size_type>(text.size()));
}

inline std::string toStd(const QString& text)
{
    return text.toStdString();
}
