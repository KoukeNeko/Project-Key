#include "InputMacro.h"

#include <chrono>
#include <cstdint>
#include <iterator>
#include <utility>

namespace ProjectKey {

namespace {

using std::chrono::day;
using std::chrono::days;
using std::chrono::month;
using std::chrono::sys_days;
using std::chrono::weekday;
using std::chrono::year;
using std::chrono::year_month_day;

// The forms are those of Foundation's DateFormatter (ICU) in zh_Hant_TW, and ja_JP for the
// Japanese calendar, as McBopomofo asks for them on macOS.

constexpr const char* kWeekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
constexpr const char* kJapaneseWeekdays[] = {"日曜日", "月曜日", "火曜日", "水曜日", "木曜日", "金曜日", "土曜日"};

struct Era {
  int year;
  unsigned month, day;
  const char* name;
};
// ICU's Japanese eras from 明治 on.
constexpr Era kJapaneseEras[] = {
    {2019, 5, 1, "令和"}, {1989, 1, 8, "平成"}, {1926, 12, 25, "昭和"}, {1912, 7, 30, "大正"}, {1868, 9, 8, "明治"}};

// The Chinese calendar's years, from the one starting 1900-01-31 through 2056, as ICU reckons
// them (it gets 2057's months wrong): bit k is set when the year's k-th month, a leap month
// included, has 30 days rather than 29, and bits 13 on number the month a leap month follows.
constexpr uint32_t kChineseYears[] = {
    0x116d2, 0x00752, 0x00ea5, 0x0b64a, 0x0064b, 0x00a9b, 0x09556, 0x0056a, 0x00b59, 0x05752,
    0x00752, 0x0db25, 0x00b25, 0x00a4b, 0x0b2ab, 0x00aad, 0x0056a, 0x04b69, 0x00da9, 0x0fd92,
    0x00d92, 0x00d25, 0x0ba4d, 0x00a56, 0x002b6, 0x095b5, 0x006d4, 0x00ea9, 0x05e92, 0x00e92,
    0x0cd26, 0x0052b, 0x00a57, 0x0b2b6, 0x00b5a, 0x006d4, 0x06ec9, 0x00749, 0x0f693, 0x00a93,
    0x0052b, 0x0ca5b, 0x00aad, 0x0056a, 0x09b55, 0x00ba4, 0x00b49, 0x05a93, 0x00a95, 0x0f52d,
    0x00536, 0x00aad, 0x0b5aa, 0x005b2, 0x00da5, 0x07d4a, 0x00d4a, 0x10a95, 0x00a97, 0x00556,
    0x0cab5, 0x00ad5, 0x006d2, 0x08ea5, 0x00ea5, 0x0064a, 0x06c97, 0x00a9b, 0x0f55a, 0x0056a,
    0x00b69, 0x0b752, 0x00b52, 0x00b25, 0x0964b, 0x00a4b, 0x114ab, 0x002ad, 0x0056d, 0x0cb69,
    0x00da9, 0x00d92, 0x09d25, 0x00d25, 0x15a4d, 0x00a56, 0x002b6, 0x0c5b5, 0x006d5, 0x00ea9,
    0x0be92, 0x00e92, 0x00d26, 0x06a56, 0x00a57, 0x114d6, 0x0035a, 0x006d5, 0x0b6c9, 0x00749,
    0x00693, 0x0952b, 0x0052b, 0x00a5b, 0x0555a, 0x0056a, 0x0fb55, 0x00ba4, 0x00b49, 0x0ba93,
    0x00a95, 0x0052d, 0x08aad, 0x00ab5, 0x135aa, 0x005d2, 0x00da5, 0x0dd4a, 0x00d4a, 0x00c95,
    0x0952e, 0x00556, 0x00ab5, 0x055b2, 0x006d2, 0x0cea5, 0x00725, 0x0064b, 0x0ac97, 0x00cab,
    0x0055a, 0x06ad6, 0x00b69, 0x17752, 0x00b52, 0x00b25, 0x0da4b, 0x00a4b, 0x004ab, 0x0a55b,
    0x005ad, 0x00b6a, 0x05b52, 0x00d92, 0x0fd25, 0x00d25, 0x00a55, 0x0b4ad, 0x004b6, 0x005b5,
    0x06daa, 0x00ec9, 0x11e92, 0x00e92, 0x00d26, 0x0ca56, 0x00a57,
};

int YearOf(const year_month_day& date) { return static_cast<int>(date.year()); }

std::string MonthDay(const year_month_day& date) {
  return std::to_string(static_cast<unsigned>(date.month())) + "月" + std::to_string(static_cast<unsigned>(date.day())) + "日";
}

// 民國 115, or, before 1912, 民國前 1.
std::string Roc(int y, const char* space) {
  return (y > 1911 ? "民國" : "民國前") + std::string(space) + std::to_string(y > 1911 ? y - 1911 : 1912 - y);
}

// 令和8年, with 元年 for an era's first year when gannen; empty before 明治.
std::string JapaneseYear(const year_month_day& date, bool gannen) {
  for (const Era& era : kJapaneseEras) {
    if (date < year_month_day{year{era.year}, month{era.month}, day{era.day}}) continue;
    int n = YearOf(date) - era.year + 1;
    return era.name + (gannen && n == 1 ? std::string("元") : std::to_string(n)) + "年";
  }
  return "";
}

// 2026年十月初三: the year is the Gregorian one the Chinese year starts in. Empty outside the table.
std::string ChineseDate(const year_month_day& date) {
  constexpr const char* kMonths[] = {"正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "臘"};
  constexpr const char* kDigits[] = {"", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十"};
  long long elapsed = (sys_days{date} - sys_days{year{1900} / 1 / 31}).count();
  for (size_t i = 0; i < std::size(kChineseYears) && elapsed >= 0; i++) {
    uint32_t leap = kChineseYears[i] >> 13;
    for (uint32_t k = 0; k < (leap ? 13u : 12u); k++) {
      int length = kChineseYears[i] >> k & 1 ? 30 : 29;
      if (elapsed >= length) {
        elapsed -= length;
        continue;
      }
      int d = static_cast<int>(elapsed) + 1;
      std::string dayName = d <= 10 ? std::string("初") + kDigits[d]
                            : d < 20 ? std::string("十") + kDigits[d - 10]
                            : d == 20 ? "二十"
                            : d < 30 ? std::string("廿") + kDigits[d - 20]
                                     : "三十";
      return std::to_string(1900 + i) + "年" + (leap && k == leap ? "閏" : "") + kMonths[leap && k >= leap ? k - 1 : k] + "月" + dayName;
    }
  }
  return "";
}

// 丙午年, or with zodiac 火馬年, by the Gregorian year as McBopomofo counts it.
std::string Sexagenary(int y, bool zodiac) {
  constexpr const char* kStems[] = {"癸", "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬"};
  constexpr const char* kElements[] = {"水", "木", "木", "火", "火", "土", "土", "金", "金", "水"};
  constexpr const char* kBranches[] = {"亥", "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌"};
  constexpr const char* kAnimals[] = {"豬", "鼠", "牛", "虎", "兔", "龍", "蛇", "馬", "羊", "猴", "雞", "狗"};
  if (y < 4) return "";
  int base = (y - 3) % 60;
  return std::string(zodiac ? kElements[base % 10] : kStems[base % 10]) + (zodiac ? kAnimals[base % 12] : kBranches[base % 12]) + "年";
}

std::string TwoDigits(int n) { return (n < 10 ? "0" : "") + std::to_string(n); }

// 晚上8:06, with seconds 晚上8:06:21: ICU's zh_Hant day periods.
std::string Time(const std::tm& now, bool seconds) {
  int h = now.tm_hour;
  const char* period = h < 5 ? "凌晨" : h < 8 ? "清晨" : h < 12 ? "上午" : h < 13 ? "中午" : h < 19 ? "下午" : "晚上";
  std::string text = period + std::to_string((h + 11) % 12 + 1) + ":" + TwoDigits(now.tm_min);
  return seconds ? text + ":" + TwoDigits(now.tm_sec) : text;
}

// The macro's text, empty when unknown. McBopomofo's TIMEZONE_* need ICU's localized zone names,
// which the platforms do not share, so they stay unknown.
std::string Convert(std::string_view name, const std::tm& now) {
  year_month_day today{year{now.tm_year + 1900}, month{static_cast<unsigned>(now.tm_mon + 1)}, day{static_cast<unsigned>(now.tm_mday)}};
  if (!today.ok()) return "";
  if (name == "TIME_NOW_SHORT" || name == "TIME_NOW_MEDIUM") return Time(now, name == "TIME_NOW_MEDIUM");

  constexpr std::pair<std::string_view, int> kDays[] = {{"DATE_YESTERDAY", -1}, {"DATE_TODAY", 0}, {"DATE_TOMORROW", 1}};
  for (const auto& [prefix, offset] : kDays) {
    if (!name.starts_with(prefix)) continue;
    year_month_day date{sys_days{today} + days{offset}};
    unsigned weekdayIndex = weekday{sys_days{date}}.c_encoding();
    std::string_view form = name.substr(prefix.size());
    if (form == "_SHORT") {
      return std::to_string(YearOf(date)) + "/" + std::to_string(static_cast<unsigned>(date.month())) + "/" +
             std::to_string(static_cast<unsigned>(date.day()));
    }
    if (form == "_MEDIUM") return std::to_string(YearOf(date)) + "年" + MonthDay(date);
    if (form == "_MEDIUM_ROC") return Roc(YearOf(date), " ") + "年" + MonthDay(date);
    if (form == "_MEDIUM_CHINESE") return ChineseDate(date);
    if (form == "_MEDIUM_JAPANESE") {
      std::string era = JapaneseYear(date, /*gannen=*/true);
      return era.empty() ? "" : era + MonthDay(date);
    }
    if (form == "_WEEKDAY_SHORT") return std::string("週") + kWeekdays[weekdayIndex];
    if (form == "_WEEKDAY") return std::string("星期") + kWeekdays[weekdayIndex];
    // McBopomofo's DATE_TOMORROW2_WEEKDAY lacks the 禮拜 of its TODAY2 and YESTERDAY2, which
    // makes it a repeat of DATE_TOMORROW_WEEKDAY that McBopomofoLM drops; here it has it.
    if (form == "2_WEEKDAY") return std::string("禮拜") + kWeekdays[weekdayIndex];
    if (form == "_WEEKDAY_JAPANESE") return kJapaneseWeekdays[weekdayIndex];
    return "";
  }

  constexpr std::pair<std::string_view, int> kYears[] = {{"LAST_YEAR_", -1}, {"THIS_YEAR_", 0}, {"NEXT_YEAR_", 1}};
  for (const auto& [prefix, offset] : kYears) {
    if (!name.starts_with(prefix)) continue;
    int y = YearOf(today) + offset;
    std::string_view form = name.substr(prefix.size());
    if (form == "PLAIN") return std::to_string(y) + "年";
    if (form == "PLAIN_WITH_ERA") return "西元" + std::to_string(y) + "年";
    if (form == "ROC") return Roc(y, "") + "年";
    // The era of the same day that year.
    if (form == "JAPANESE") return JapaneseYear(year_month_day{year{y}, today.month(), today.day()}, /*gannen=*/false);
    if (form == "GANZHI") return Sexagenary(y, /*zodiac=*/false);
    // McBopomofo's LAST_ and NEXT_YEAR_CHINESE_ZODIAC give this year's; these give their own year's.
    if (form == "CHINESE_ZODIAC") return Sexagenary(y, /*zodiac=*/true);
    return "";
  }
  return "";
}

}  // namespace

std::string ConvertMacro(const std::string& macro, const std::tm& now) {
  if (!macro.starts_with(kMacroPrefix)) return macro;
  std::string text = Convert(std::string_view(macro).substr(kMacroPrefix.size()), now);
  return text.empty() ? macro : text;
}

std::string ConvertMacro(const std::string& macro) {
  if (!macro.starts_with(kMacroPrefix)) return macro;  // every word of a lookup comes through here
  std::time_t time = std::time(nullptr);
  std::tm now{};
#ifdef _WIN32
  if (localtime_s(&now, &time) != 0) return macro;
#else
  if (!localtime_r(&time, &now)) return macro;
#endif
  return ConvertMacro(macro, now);
}

}  // namespace ProjectKey
