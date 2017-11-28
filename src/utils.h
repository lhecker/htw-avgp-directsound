#pragma once

#include "stdafx.h"

std::string string_wide_to_utf8(const std::wstring_view wstr);
std::wstring string_utf8_to_wide(const std::string_view str);
std::wstring_view cstring_view(const CSimpleString& str);
std::string string_format(_Printf_format_string_ const char* const format, ...);
void debug_print(_In_z_ _Printf_format_string_ const wchar_t* const format, ...);
gsl::span<const byte> load_resource(LPWSTR type, int name);
