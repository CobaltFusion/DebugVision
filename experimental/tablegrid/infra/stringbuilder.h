#pragma once

#include <string>
#include <sstream>

namespace infra {

class stringbuilder
{
public:

	template <typename T>
	stringbuilder& operator<<(const T& t)
	{
		m_ss << t;
		return *this;
	}

	operator std::string() const
	{
		return m_ss.str();
	}

	std::string str() const
	{
		return m_ss.str();
	}

private:
	std::stringstream m_ss;
};

class wstringbuilder
{
public:

	template <typename T>
	wstringbuilder& operator<<(const T& t)
	{
		m_ss << t;
		return *this;
	}

	operator std::wstring() const
	{
		return m_ss.str();
	}

private:
	std::wstringstream m_ss;
};

} // namespace infra
