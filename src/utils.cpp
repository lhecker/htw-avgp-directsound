#include "stdafx.h"

std::wstring_view cstring_view(const CSimpleString& str) {
	return std::wstring_view(str, str.GetLength());
}

std::string string_format(_Printf_format_string_ const char* const format, ...) {
	va_list args;
	va_start(args, format);
	DEFER{ va_end(args); };

	const auto expected_size = vsnprintf(nullptr, 0, format, args);
	if (expected_size < 0) {
		throw std::runtime_error("formatting failed");
	}
	if (expected_size > std::numeric_limits<int>::max() - 1) {
		throw std::runtime_error("formatted size too large");
	}

	std::string str(expected_size + 1, char());

	const auto resulting_size = vsnprintf(str.data(), str.size(), format, args);
	if (resulting_size != expected_size) {
		throw std::runtime_error("formatting failed");
	}

	return str;
}

void debug_print(_In_z_ _Printf_format_string_ const wchar_t* const format, ...) {
	CString str;

	{
		va_list args;
		va_start(args, format);
		DEFER{ va_end(args); };
		str.FormatV(format, args);
	}

	OutputDebugString(str);
}
