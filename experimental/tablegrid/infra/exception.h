#pragma once

#include "stringbuilder.h"
#include <thread>

namespace infra
{

class api_assuption_error : public std::logic_error
{
	using std::logic_error::logic_error;
};

inline void ThrowException(const std::string& str)
{
	throw std::runtime_error(str);
}

inline void ThrowApiAssuptionError(const std::string& str)
{
	throw api_assuption_error(str);
}

#define THROW_EXCEPTION(x) ThrowException(infra::stringbuilder() << x << " (" << __FILE__ << ":" << __LINE__ << ") tid: " << std::this_thread::get_id())
#define THROW_API_ASSUMPTION_ERROR(x) ThrowApiAssuptionError(infra::stringbuilder() << x << " (" << __FILE__ << ":" << __LINE__ << ") tid: " << std::this_thread::get_id())

} // namespace infra
