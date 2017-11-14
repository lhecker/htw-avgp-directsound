#include "stdafx.h"

std::string string_wide_to_utf8(const std::wstring_view wstr) {
	if (wstr.empty()) {
		return std::string();
	}
	if (wstr.size() > size_t(std::numeric_limits<int>::max())) {
		throw std::length_error("input string too long");
	}

	const int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	std::string str(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, NULL, NULL);
	return str;
}

std::wstring string_utf8_to_wide(const std::string_view str) {
	if (str.empty()) {
		return std::wstring();
	}
	if (str.size() > size_t(std::numeric_limits<int>::max())) {
		throw std::length_error("input string too long");
	}

	const int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	std::wstring wstr(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
	return wstr;
}

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
