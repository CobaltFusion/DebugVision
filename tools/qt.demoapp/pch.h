#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <regex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

#include <QtCore>

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
namespace std {
template <>
struct hash<QString>
{
    std::size_t operator()(const QString& str) const noexcept
    {
        return qHash(str);
    }
};
} // namespace std
#endif
