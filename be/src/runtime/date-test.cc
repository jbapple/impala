// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "common/status.h"
#include "runtime/date-value.h"
#include "runtime/datetime-parse-util.h"
#include "runtime/raw-value.inline.h"
#include "runtime/timestamp-value.h"
#include "testutil/gtest-util.h"

#include "common/names.h"

using boost::gregorian::date;
using boost::posix_time::time_duration;

namespace impala {

using datetime_parse_util::DateTimeFormatContext;
using datetime_parse_util::ParseFormatTokens;

inline void ValidateDate(const DateValue& dv, int exp_year, int exp_month, int exp_day,
    const string& desc) {
  int year, month, day;
  EXPECT_TRUE(dv.ToYearMonthDay(&year, &month, &day)) << desc;
  EXPECT_EQ(exp_year, year);
  EXPECT_EQ(exp_month, month);
  EXPECT_EQ(exp_day, day);
}

inline DateValue ParseValidateDate(const char* s, bool accept_time_toks, int exp_year,
    int exp_month, int exp_day) {
  DCHECK(s != nullptr);
  DateValue v = DateValue::Parse(s, strlen(s), accept_time_toks);
  ValidateDate(v, exp_year, exp_month, exp_day, s);
  return v;
}

TEST(DateTest, ParseDefault) {
  // Parse with time tokens rejected.
  const DateValue v1 = ParseValidateDate("2012-01-20", false, 2012, 1, 20);
  const DateValue v2 = ParseValidateDate("1990-10-20", false, 1990, 10, 20);
  const DateValue v3 = ParseValidateDate("1990-10-20", false, 1990, 10, 20);
  // Parse with time tokens accepted.
  const DateValue v4 = ParseValidateDate("1990-10-20 23:59:59.999999999", true, 1990, 10,
      20);
  const DateValue v5 = ParseValidateDate("1990-10-20 00:01:02.9", true, 1990, 10, 20);

  // Test comparison operators.
  EXPECT_NE(v1, v2);
  EXPECT_EQ(v2, v3);
  EXPECT_LT(v2, v1);
  EXPECT_LE(v2, v1);
  EXPECT_GT(v1, v2);
  EXPECT_GE(v2, v3);

  // Time components are not part of the date value
  EXPECT_EQ(v3, v4);
  EXPECT_EQ(v3, v5);

  EXPECT_NE(RawValue::GetHashValue(&v1, TYPE_DATE, 0),
      RawValue::GetHashValue(&v2, TYPE_DATE, 0));
  EXPECT_EQ(RawValue::GetHashValue(&v3, TYPE_DATE, 0),
      RawValue::GetHashValue(&v2, TYPE_DATE, 0));

  // 1-digit months and days are ok in date string.
  ParseValidateDate("2012-1-20", false, 2012, 1, 20);
  ParseValidateDate("2012-9-8", false, 2012, 9, 8);
  // 1-digit hours/minutes/seconds are ok if time components are accepted.
  ParseValidateDate("2012-09-8 01:1:2.9", true, 2012, 9, 8);
  ParseValidateDate("2012-9-8 1:01:02", true, 2012, 9, 8);
  // Different fractional seconds are accepted
  ParseValidateDate("2012-09-8 01:01:2", true, 2012, 9, 8);
  ParseValidateDate("2012-09-8 01:01:2.9", true, 2012, 9, 8);
  ParseValidateDate("2012-09-8 01:01:02.9", true, 2012, 9, 8);
  ParseValidateDate("2012-09-8 01:01:2.999", true, 2012, 9, 8);
  ParseValidateDate("2012-09-8 01:01:02.999", true, 2012, 9, 8);
  ParseValidateDate("2012-09-8 01:01:2.999999999", true, 2012, 9, 8);
  ParseValidateDate("2012-09-8 01:01:02.999999999", true, 2012, 9, 8);

  // Bad formats: invalid date component.
  for (const char* s: {"1990-10", "1991-10-32", "1990-10-", "10:11:12 1991-10-10",
      "02011-01-01", "999-01-01", "2012-01-200", "2011-001-01"}) {
    EXPECT_FALSE(DateValue::Parse(s, strlen(s), false).IsValid()) << s;
  }
  // Bad formats: valid date and time components but time component is rejected.
  for (const char* s: {"2012-01-20 10:11:12", "2012-1-2 10:11:12"}) {
    EXPECT_FALSE(DateValue::Parse(s, strlen(s), false).IsValid()) << s;
  }
  // Bad formats: valid date component, invalid time component.
  for (const char* s: {"2012-01-20 10:11:", "2012-1-2 10::12", "2012-01-20 :11:12",
      "2012-01-20 24:11:12", "2012-01-20 23:60:12"}) {
    EXPECT_FALSE(DateValue::Parse(s, strlen(s), true).IsValid()) << s;
  }
  // Bad formats: missing date component, valid time component.
  for (const char* s: {"10:11:12", "1:11:12", "10:1:12", "10:1:2.999"}) {
    EXPECT_FALSE(DateValue::Parse(s, strlen(s), true).IsValid()) << s;
  }
}

// Used to represent a parsed date token. For example, it may represent a year.
struct DateToken {
  const char* fmt;
  int val;
  const char* month_name;

  DateToken(const char* fmt, int val)
    : fmt(fmt),
      val(val),
      month_name(nullptr) {
  }

  DateToken(const char* month_fmt, int month_val, const char* month_name)
    : fmt(month_fmt),
      val(month_val),
      month_name(month_name) {
  }

  friend bool operator<(const DateToken& lhs, const DateToken& rhs) {
    return strcmp(lhs.fmt, rhs.fmt) < 0;
  }
};

void TestDateTokens(const vector<DateToken>& toks, int year, int month, int day,
    const char* separator) {
  string fmt, val;
  for (int i = 0; i < toks.size(); ++i) {
    fmt.append(toks[i].fmt);
    if (separator != nullptr && i + 1 < toks.size()) fmt.push_back(*separator);

    if (toks[i].month_name != nullptr) {
      val.append(string(toks[i].month_name));
    } else {
      val.append(lexical_cast<string>(toks[i].val));
    }
    if (separator != nullptr && i + 1 < toks.size()) val.push_back(*separator);
  }

  string fmt_val = "Format: " + fmt + ", Val: " + val;
  DateTimeFormatContext dt_ctx(fmt.c_str());
  ASSERT_TRUE(ParseFormatTokens(&dt_ctx, false)) << fmt_val;
  DateValue dv = DateValue::Parse(val.c_str(), val.length(), dt_ctx);
  ValidateDate(dv, year, month, day, fmt_val);

  vector<char> buff(dt_ctx.fmt_out_len + 1);
  int actual_len = dv.Format(dt_ctx, buff.size(), buff.data());
  EXPECT_GT(actual_len, 0) << fmt_val;
  EXPECT_LE(actual_len, dt_ctx.fmt_len) << fmt_val;

  string buff_str(buff.data());
  EXPECT_EQ(buff_str, val) << fmt_val <<  " " << buff_str;
}

// This function will generate all permutations of tokens to test that the parsing and
// formatting is correct (position of tokens should be irrelevant). Note that separators
// are also combined with EACH token permutation to get the widest coverage on formats.
// This forces out the parsing and format logic edge cases.
void TestDateTokenPermutations(vector<DateToken>* toks, int year, int month, int day) {
  sort(toks->begin(), toks->end());

  const char* SEPARATORS = " ~!@%^&*_+-:;|\\,./";
  do {
    // Validate we can parse date raw tokens (no separators)
    TestDateTokens(*toks, year, month, day, nullptr);

    // Validate we can parse date with separators
    for (const char* separator = SEPARATORS; *separator != 0; ++separator) {
      TestDateTokens(*toks, year, month, day, separator);
    }
  } while (next_permutation(toks->begin(), toks->end()));
}

TEST(DateTest, ParseFormatCustomFormats) {
  // Test custom formats by generating all permutations of tokens to check parsing and
  // formatting is behaving correctly (position of tokens should be irrelevant). Note
  // that separators are also combined with EACH token permutation to get the widest
  // coverage on formats.
  const int YEAR = 2013;
  const int MONTH = 10;
  const int DAY = 14;
  // Test parsing/formatting with numeric date tokens
  vector<DateToken> dt_toks{
      DateToken("dd", DAY),
      DateToken("MM", MONTH),
      DateToken("yyyy", YEAR)};
  TestDateTokenPermutations(&dt_toks, YEAR, MONTH, DAY);
}

TEST(DateTest, ParseFormatLiteralMonths) {
  // Test literal months
  const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
      "Oct", "Nov", "Dec"
  };

  // Test parsing/formatting of literal months (short)
  const int MONTH_CNT = (sizeof(months) / sizeof(char**));

  const int YEAR = 2013;
  const int DAY = 14;
  for (int i = 0; i < MONTH_CNT; ++i) {
    // Test parsing/formatting with short literal months
    vector<DateToken> dt_lm_toks{
        DateToken("dd", DAY),
        DateToken("MMM", i + 1, months[i]),
        DateToken("yyyy", YEAR)};
    TestDateTokenPermutations(&dt_lm_toks, YEAR, i + 1, DAY);
  }
}

// Used for defining a custom date format test. The structure can be used to indicate
// whether the format or value is expected to fail. In a happy path test, the values for
// year, month, day will be validated against the parsed result.
// Further validation will also be performed if the should_format flag is enabled,
// whereby the parsed date will be translated back to a string and checked against the
// expected value.
struct DateTC {
  const char* fmt;
  const char* str;
  bool fmt_should_fail;
  bool str_should_fail;
  bool should_format;
  int expected_year;
  int expected_month;
  int expected_day;

  DateTC(const char* fmt, const char* str, bool fmt_should_fail = true,
      bool str_should_fail = true)
    : fmt(fmt),
      str(str),
      fmt_should_fail(fmt_should_fail),
      str_should_fail(str_should_fail),
      should_format(true),
      expected_year(0),
      expected_month(0),
      expected_day(0) {
  }

  DateTC(const char* fmt, const char* str, bool should_format,
      int expected_year, int expected_month, int expected_day)
    : fmt(fmt),
      str(str),
      fmt_should_fail(false),
      str_should_fail(false),
      should_format(should_format),
      expected_year(expected_year),
      expected_month(expected_month),
      expected_day(expected_day) {
  }

  void Run(int id, const TimestampValue& now) const {
    DateTimeFormatContext dt_ctx(fmt);
    dt_ctx.SetCenturyBreak(now);

    stringstream desc;
    desc << "DateTC [" << id << "]: " << " fmt:" << fmt << " str:" << str
      << " expected date:" << expected_year << "/" << expected_month << "/"
      << expected_day;

    bool parse_result = ParseFormatTokens(&dt_ctx, false);
    if (fmt_should_fail) {
      EXPECT_FALSE(parse_result) << desc.str();
      return;
    } else {
      ASSERT_TRUE(parse_result) << desc.str();
    }

    DateValue cust_dv = DateValue::Parse(str, strlen(str), dt_ctx);
    if (str_should_fail) {
      EXPECT_FALSE(cust_dv.IsValid()) << desc.str();
      return;
    }

    // Check the date (based on any date format tokens being present)
    ValidateDate(cust_dv, expected_year, expected_month, expected_day, desc.str());

    // Check formatted date
    if (!should_format) return;

    vector<char> buff(dt_ctx.fmt_out_len + 1);
    int actual_len = cust_dv.Format(dt_ctx, buff.size(), buff.data());
    EXPECT_GT(actual_len, 0) << desc.str();
    EXPECT_LE(actual_len, dt_ctx.fmt_len) << desc.str();
    EXPECT_EQ(string(str, strlen(str)), string(buff.data(), actual_len)) << desc.str();
  }
};

TEST(DateTest, ParseFormatEdgeCases) {
  const TimestampValue now(date(1980, 2, 28), time_duration(16, 14, 24));

  vector<DateTC> test_cases{
      // Test year upper/lower bound
      DateTC("yyyy-MM-dd", "0000-01-01", true, 0, 1, 1),
      DateTC("yyyy-MM-dd", "-001-01-01", false, true),
      DateTC("yyyy-MM-dd", "9999-12-31", true, 9999, 12, 31),
      DateTC("yyyyy-MM-dd", "10000-12-31", false, true),
      // Test Feb 29 in leap years
      DateTC("yyyy-MM-dd", "0000-02-29", true, 0, 2, 29),
      DateTC("yyyy-MM-dd", "1904-02-29", true, 1904, 2, 29),
      DateTC("yyyy-MM-dd", "2000-02-29", true, 2000, 2, 29),
      // Test Feb 29 in non-leap years
      DateTC("yyyy-MM-dd", "0001-02-29", false, true),
      DateTC("yyyy-MM-dd", "1900-02-29", false, true),
      DateTC("yyyy-MM-dd", "1999-02-29", false, true)};

  for (int i = 0; i < test_cases.size(); ++i) {
    test_cases[i].Run(i, now);
  }
}

TEST(DateTest, ParseFormatSmallYear) {
  // Fix current time to determine the behavior parsing 2-digit year format.
  const TimestampValue now(date(1980, 2, 28), time_duration(16, 14, 24));

  // Test year < 1000
  vector<DateTC> test_cases{
      DateTC("yyyy-MM-dd", "0999-10-31", true, 999, 10, 31),
      DateTC("yyyy-MM-dd", "0099-10-31", true, 99, 10, 31),
      DateTC("yyyy-MM-dd", "0009-10-31", true, 9, 10, 31),
      // Format token yyy works when parsing years < 1000.
      // On the other hand when yyy is used for formatting years, modulo 100 will be
      // applied
      DateTC("yyy-MM-dd", "999-10-31", false, 999, 10, 31),
      DateTC("yyy-MM-dd", "099-10-31", true, 99, 10, 31),
      DateTC("yyy-MM-dd", "009-10-31", true, 9, 10, 31),
      // Year is aligned when yy format token is used and we have a 2-difgit year. 3-digit
      // years are not parsed correctly.
      DateTC("yy-MM-dd", "999-10-31", false, true),
      DateTC("yy-MM-dd", "99-10-31", true, 1999, 10, 31),
      DateTC("yy-MM-dd", "09-10-31", true, 1909, 10, 31),
      // Year is aligned when y format token is used and we have a 2-digit year.
      DateTC("y-MM-dd", "999-10-31", false, 999, 10, 31),
      DateTC("y-MM-dd", "99-10-31", false, 1999, 10, 31),
      DateTC("y-MM-dd", "09-10-31", false, 1909, 10, 31),
      DateTC("y-MM-dd", "9-10-31", false, 1909, 10, 31)};

  for (int i = 0; i < test_cases.size(); ++i) {
    test_cases[i].Run(i, now);
  }
}

TEST(DateTest, ParseFormatAlignedYear) {
  // Fix current time to determine the behavior parsing 2-digit year format.
  // Set it to 02/28 to test 02/29 edge cases.
  // The corresponding century break will be 1900-02-28.
  const TimestampValue now(date(1980, 2, 28), time_duration(16, 14, 24));

  // Test year alignment for 1- and 2-digit year format.
  vector<DateTC> test_cases{
      // Test 2-digit year format
      DateTC("yy-MM-dd", "17-08-31", true, 1917, 8, 31),
      DateTC("yy-MM-dd", "99-08-31", true, 1999, 8, 31),
      // Test 02/29 edge cases of 2-digit year format
      DateTC("yy-MM-dd", "00-02-28", true, 2000, 2, 28),
      // After the cutoff year is 1900, but 1900/02/29 is invalid
      DateTC("yy-MM-dd", "00-02-29", false, true),
      // After the cutoff year is 1900
      DateTC("yy-MM-dd", "00-03-01", true, 1900, 3, 1),
      DateTC("yy-MM-dd", "04-02-29", true, 1904, 2, 29),
      DateTC("yy-MM-dd", "99-02-29", false, true),
      // Test 1-digit year format with time to show the exact boundary
      // Before the cutoff, year should be 2000
      DateTC("y-MM-dd", "00-02-28", false, 2000, 2, 28),
      // After the cutoff year is 1900, but 1900/02/29 is invalid
      DateTC("y-MM-dd", "00-02-29", false, true),
      // After the cutoff year is 1900.
      DateTC("y-MM-dd", "00-03-01", false, 1900, 3, 1)};

  for (int i = 0; i < test_cases.size(); ++i) {
    test_cases[i].Run(i, now);
  }

  // Test year realignment with a different 'now' timestamp.
  // This time the corresponding century break will be 1938-09-25.
  const TimestampValue now2(date(2018, 9, 25), time_duration(16, 14, 24));

  vector<DateTC> test_cases2{
      // Before the cutoff, year is 2004.
      DateTC("yy-MM-dd", "04-02-29", true, 2004, 2, 29),
      // Still before the cutoff, year is 2038.
      DateTC("yy-MM-dd", "38-09-25", true, 2038, 9, 25),
      // After the cutoff, year is 1938.
      DateTC("yy-MM-dd", "38-09-26", true, 1938, 9, 26),
      // Test parsing again with 'y' format token.
      DateTC("y-MM-dd", "04-02-29", false, 2004, 2, 29),
      DateTC("y-MM-dd", "38-09-25", false, 2038, 9, 25),
      DateTC("y-MM-dd", "38-09-26", false, 1938, 9, 26)};

  for (int i = 0; i < test_cases2.size(); ++i) {
    test_cases2[i].Run(i + test_cases.size(), now2);
  }
}

TEST(DateTest, ParseFormatComplexFormats) {
  const TimestampValue now(date(1980, 2, 28), time_duration(16, 14, 24));

  // Test parsing/formatting of complex date formats
  vector<DateTC> test_cases{
      // Test case on literal short months
      DateTC("yyyy-MMM-dd", "2013-OCT-01", false, 2013, 10, 1),
      // Test case on literal short months
      DateTC("yyyy-MMM-dd", "2013-oct-01", false, 2013, 10, 1),
      // Test case on literal short months
      DateTC("yyyy-MMM-dd", "2013-oCt-01", false, 2013, 10, 1),
      // Test padding on numeric and literal tokens (short,
      DateTC("MMMyyyyyydd", "Apr00201309", true, 2013, 4, 9),
      // Test duplicate tokens
      DateTC("yyyy MM dd ddMMMyyyy", "2013 05 12 16Apr1952", false, 1952, 4, 16),
      // Test missing separator on short date format
      DateTC("Myyd", "4139", true, true),
      // Test bad year format
      DateTC("YYYYmmdd", "20131001"),
      // Test unknown formatting character
      DateTC("yyyyUUdd", "2013001001"),
      // Test that T|Z markers and time tokens are rejected
      DateTC("yyyy-MM-ddT", "2013-11-12T"),
      DateTC("yyyy-MM-ddZ", "2013-11-12Z"),
      DateTC("yyyy-MM-dd HH:mm:ss", "2013-11-12 12:23:36"),
      DateTC("HH:mm:ss", "12:23:36"),
      // Test numeric formatting character
      DateTC("yyyyMM1dd", "201301111"),
      // Test out of range year
      DateTC("yyyyyMMdd", "120130101", false, true),
      // Test out of range month
      DateTC("yyyyMMdd", "20131301", false, true),
      // Test out of range month
      DateTC("yyyyMMdd", "20130001", false, true),
      // Test out of range day
      DateTC("yyyyMMdd", "20130132", false, true),
      // Test out of range day
      DateTC("yyyyMMdd", "20130100", false, true),
      // Test characters where numbers should be
      DateTC("yyyyMMdd", "201301aa", false, true),
      // Test missing year
      DateTC("MMdd", "1201", false, true),
      // Test missing month
      DateTC("yyyydd", "201301", false, true),
      DateTC("yydd", "1301", false, true),
      // Test missing day
      DateTC("yyyyMM", "201301", false, true),
      DateTC("yyMM", "8512", false, true),
      // Test missing month and day
      DateTC("yyyy", "2013", false, true),
      DateTC("yy", "13", false, true),
      // Test short year token
      DateTC("y-MM-dd", "2013-11-13", false, 2013, 11, 13),
      DateTC("y-MM-dd", "13-11-13", false, 1913, 11, 13),
      // Test short month token
      DateTC("yyyy-M-dd", "2013-11-13", false, 2013, 11, 13),
      DateTC("yyyy-M-dd", "2013-1-13", false, 2013, 1, 13),
      // Test short day token
      DateTC("yyyy-MM-d", "2013-11-13", false, 2013, 11, 13),
      DateTC("yyyy-MM-d", "2013-11-3", false, 2013, 11, 3),
      // Test short all date tokens
      DateTC("y-M-d", "2013-11-13", false, 2013, 11, 13),
      DateTC("y-M-d", "13-1-3", false, 1913, 1, 3)};

  // Loop through custom parse/format test cases and execute each one. Each test case
  // will be explicitly set with a pass/fail expectation related to either the format
  // or literal value.
  for (int i = 0; i < test_cases.size(); ++i) {
    test_cases[i].Run(i, now);
  }
}

// Used to test custom date output test cases i.e. date value -> string.
struct DateFormatTC {
  const int32_t days_since_epoch;
  const char* fmt;
  const char* str;
  bool should_fail;

  DateFormatTC(int32_t days_since_epoch, const char* fmt, const char* str,
      bool should_fail = false)
    : days_since_epoch(days_since_epoch),
      fmt(fmt),
      str(str),
      should_fail(should_fail) {
  }

  void Run(int id, const TimestampValue& now) const {
    DateTimeFormatContext dt_ctx(fmt);
    dt_ctx.SetCenturyBreak(now);

    stringstream desc;
    desc << "DateFormatTC [" << id << "]: " << "days_since_epoch:" << days_since_epoch
         << " fmt:" << fmt << " str:" << str;

    ASSERT_TRUE(ParseFormatTokens(&dt_ctx, false)) << desc.str();

    const DateValue cust_dv(days_since_epoch);
    EXPECT_TRUE(cust_dv.IsValid()) << desc.str();
    EXPECT_GE(dt_ctx.fmt_out_len, dt_ctx.fmt_len) << desc.str();

    vector<char> buff(dt_ctx.fmt_out_len + 1);
    int actual_len = cust_dv.Format(dt_ctx, buff.size(), buff.data());
    EXPECT_GT(actual_len, 0) << desc.str();
    EXPECT_LE(actual_len, dt_ctx.fmt_out_len) << desc.str();
    EXPECT_EQ(string(buff.data(), actual_len), string(str, strlen(str))) << desc.str();
  }

};

TEST(DateTest, FormatComplexFormats) {
  const TimestampValue now(date(1980, 2, 28), time_duration(16, 14, 24));

  // Test complex formatting of dates
  vector<DateFormatTC> fmt_test_cases{
      // Test just formatting date tokens
      DateFormatTC(11178, "yyyy-MM-dd", "2000-08-09"),
      // Test short form date tokens
      DateFormatTC(11178, "yyyy-M-d", "2000-8-9"),
      // Test short form tokens on wide dates
      DateFormatTC(15999, "d", "21"),
      // Test month expansion
      DateFormatTC(11178, "MMM/MM/M", "Aug/08/8"),
      // Test padding on single digits
      DateFormatTC(11178, "dddddd/dd/d", "000009/09/9"),
      // Test padding on double digits
      DateFormatTC(15999, "dddddd/dd/dd", "000021/21/21")};

  // Loop through format test cases
  for (int i = 0; i < fmt_test_cases.size(); ++i) {
    fmt_test_cases[i].Run(i, now);
  }
}

TEST(DateTest, DateValueEdgeCases) {
  // Test min supported date.
  // MIN_DATE_DAYS_SINCE_EPOCH was calculated using the Proleptic Gregorian calendar. This
  // is expected to be different then how Hive written Parquet files represent 0000-01-01.
  const int32_t MIN_DATE_DAYS_SINCE_EPOCH = -719528;
  const DateValue min_date1 = ParseValidateDate("0000-01-01", true, 0, 1, 1);
  const DateValue min_date2 = ParseValidateDate("0000-01-01 00:00:00", true, 0, 1, 1);
  EXPECT_EQ(min_date1, min_date2);
  int32_t min_days;
  EXPECT_TRUE(min_date1.ToDaysSinceEpoch(&min_days));
  EXPECT_EQ(MIN_DATE_DAYS_SINCE_EPOCH, min_days);
  EXPECT_EQ("0000-01-01", min_date1.ToString());
  EXPECT_EQ("0000-01-01", min_date2.ToString());

  const DateValue min_date3(MIN_DATE_DAYS_SINCE_EPOCH);
  EXPECT_TRUE(min_date3.IsValid());
  EXPECT_EQ(min_date1, min_date3);

  const DateValue too_early(MIN_DATE_DAYS_SINCE_EPOCH - 1);
  EXPECT_FALSE(too_early.IsValid());

  // Test max supported date.
  const int32_t MAX_DATE_DAYS_SINCE_EPOCH = 2932896;
  const DateValue max_date1 = ParseValidateDate("9999-12-31", true, 9999, 12, 31);
  const DateValue max_date2 = ParseValidateDate("9999-12-31 23:59:59.999999999", true,
      9999, 12, 31);
  EXPECT_EQ(max_date1, max_date2);
  int32_t max_days;
  EXPECT_TRUE(max_date1.ToDaysSinceEpoch(&max_days));
  EXPECT_EQ(MAX_DATE_DAYS_SINCE_EPOCH, max_days);
  EXPECT_EQ("9999-12-31", max_date1.ToString());
  EXPECT_EQ("9999-12-31", max_date2.ToString());

  const DateValue max_date3(MAX_DATE_DAYS_SINCE_EPOCH);
  EXPECT_TRUE(max_date3.IsValid());
  EXPECT_EQ(max_date1, max_date3);

  const DateValue too_late(MAX_DATE_DAYS_SINCE_EPOCH + 1);
  EXPECT_FALSE(too_late.IsValid());

  // Test that Feb 29 is valid in leap years.
  for (int leap_year: {0, 1904, 1980, 1996, 2000, 2004, 2104, 9996}) {
    EXPECT_TRUE(DateValue(leap_year, 2, 29).IsValid()) << "year:" << leap_year;
  }

  // Test that Feb 29 is invalid in non-leap years.
  for (int non_leap_year: {1, 1900, 1981, 1999, 2001, 2100, 9999}) {
    EXPECT_TRUE(DateValue(non_leap_year, 2, 28).IsValid()) << "year:" << non_leap_year;
    EXPECT_FALSE(DateValue(non_leap_year, 2, 29).IsValid()) << "year:" << non_leap_year;
    EXPECT_TRUE(DateValue(non_leap_year, 3, 1).IsValid()) << "year:" << non_leap_year;
  }
}

TEST(DateTest, AddDays) {
  // Adding days to an invalid DateValue instance returns an invalid DateValue.
  DateValue invalid_dv;
  EXPECT_FALSE(invalid_dv.IsValid());
  EXPECT_FALSE(invalid_dv.AddDays(1).IsValid());

  // AddDays works with 0, > 0 and < 0 number of days.
  DateValue dv(2019, 5, 16);
  EXPECT_EQ(DateValue(2019, 5, 17), dv.AddDays(1));
  EXPECT_EQ(DateValue(2019, 5, 15), dv.AddDays(-1));
  // May has 31 days, April has 30 days.
  EXPECT_EQ(DateValue(2019, 6, 16), dv.AddDays(31));
  EXPECT_EQ(DateValue(2019, 4, 16), dv.AddDays(-30));
  // 2019 is not a leap year, 2020 is a leap year.
  EXPECT_EQ(DateValue(2020, 5, 16), dv.AddDays(366));
  EXPECT_EQ(DateValue(2018, 5, 16), dv.AddDays(-365));

  // Test upper limit
  dv = DateValue(9999, 12, 20);
  EXPECT_EQ(DateValue(9999, 12, 31), dv.AddDays(11));
  EXPECT_FALSE(dv.AddDays(12).IsValid());
  EXPECT_FALSE(dv.AddDays(13).IsValid());

  // Test lower limit
  dv = DateValue(0, 1, 10);
  EXPECT_EQ(DateValue(0, 1, 1), dv.AddDays(-9));
  EXPECT_FALSE(dv.AddDays(-10).IsValid());
  EXPECT_FALSE(dv.AddDays(-11).IsValid());

  // Test leap year
  dv = DateValue(2000, 2, 20);
  EXPECT_EQ(DateValue(2000, 2, 28), dv.AddDays(8));
  EXPECT_EQ(DateValue(2000, 2, 29), dv.AddDays(9));
  EXPECT_EQ(DateValue(2000, 3, 1), dv.AddDays(10));

  // Test non-leap year
  dv = DateValue(2001, 2, 20);
  EXPECT_EQ(DateValue(2001, 2, 28), dv.AddDays(8));
  EXPECT_EQ(DateValue(2001, 3, 1), dv.AddDays(9));
}

TEST(DateTest, WeekDay) {
  // WeekDay() returns -1 for invalid dates.
  DateValue invalid_dv;
  EXPECT_FALSE(invalid_dv.IsValid());
  EXPECT_EQ(-1, invalid_dv.WeekDay());

  // 2019.05.01 is Wednesday.
  DateValue dv(2019, 5, 1);
  for (int i = 0; i <= 31; ++i) {
    // 0 = Monday, 2 = Wednesday and 6 = Sunday.
    EXPECT_EQ((i + 2) % 7, dv.AddDays(i).WeekDay());
  }

  // Test upper limit. 9999.12.31 is Friday.
  EXPECT_EQ(4, DateValue(9999, 12, 31).WeekDay());

  // Test lower limit.
  // 0000.01.01 is Monday.
  EXPECT_EQ(0, DateValue(1, 1, 1).WeekDay());
  // 0000.01.01 is Saturday.
  EXPECT_EQ(5, DateValue(0, 1, 1).WeekDay());
}

TEST(DateTest, ToYear) {
  int year;

  // Test that ToYear() returns false for invalid dates.
  DateValue invalid_dv;
  EXPECT_FALSE(invalid_dv.IsValid());
  EXPECT_FALSE(invalid_dv.ToYear(&year));

  // Test that ToYear() returns the same year as ToYearMonthDay().
  // The following loop iterates through all valid dates:
  DateValue dv(0, 1, 1);
  EXPECT_TRUE(dv.IsValid());
  do {
    int y, m, d;
    EXPECT_TRUE(dv.ToYearMonthDay(&y, &m, &d));

    EXPECT_TRUE(dv.ToYear(&year));
    EXPECT_EQ(y, year);

    dv = dv.AddDays(1);
  } while (dv.IsValid());
}

}

IMPALA_TEST_MAIN();
