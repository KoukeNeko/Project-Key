// McBopomofo's input macros: data.txt rows such as "ㄐㄧㄣ-ㄊㄧㄢ MACRO@DATE_TODAY_SHORT -8" stand for
// a date, the time or a year, written as McBopomofo's InputMacro.swift writes them.
#pragma once

#include <ctime>
#include <string>
#include <string_view>

namespace ProjectKey {

inline constexpr std::string_view kMacroPrefix = "MACRO@";

// The text `macro` stands for at the local time `now`, or `macro` itself when it is no macro
// known here (McBopomofoLM then drops it).
std::string ConvertMacro(const std::string& macro, const std::tm& now);
// The same at the current local time.
std::string ConvertMacro(const std::string& macro);

}  // namespace ProjectKey
