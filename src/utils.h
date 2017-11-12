#pragma once

std::wstring_view cstring_view(const CSimpleString& str);
std::string string_format(_Printf_format_string_ const char* const format, ...);
void debug_print(_In_z_ _Printf_format_string_ const wchar_t* const format, ...);
