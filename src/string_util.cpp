/*
 * s3fs - FUSE-based file system backed by Amazon S3
 *
 * Copyright(C) 2007 Randy Rizun <rrizun@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include <sstream>
#include <string>
#include <map>

#include "common.h"
#include "string_util.h"

using namespace std;

template <class T> std::string str(T value) {
  std::ostringstream s;
  s << value;
  return s.str();
}

template std::string str(short value);
template std::string str(unsigned short value);
template std::string str(int value);
template std::string str(unsigned int value);
template std::string str(long value);
template std::string str(unsigned long value);
template std::string str(long long value);
template std::string str(unsigned long long value);

static const char hexAlphabet[] = "0123456789ABCDEF";

off_t s3fs_strtoofft(const char* str, bool is_base_16)
{
  if(!str || '\0' == *str){
    return 0;
  }
  off_t  result;
  bool   chk_space;
  bool   chk_base16_prefix;
  for(result = 0, chk_space = false, chk_base16_prefix = false; '\0' != *str; str++){
    // check head space
    if(!chk_space && isspace(*str)){
      continue;
    }else if(!chk_space){
      chk_space = true;
    }
    // check prefix for base 16
    if(!chk_base16_prefix){
      chk_base16_prefix = true;
      if('0' == *str && ('x' == str[1] || 'X' == str[1])){
        is_base_16 = true;
        str++;
        continue;
      }
    }
    // check like isalnum and set data
    result *= (is_base_16 ? 16 : 10);
    if('0' <= *str && '9' >= *str){
      result += static_cast<off_t>(*str - '0');
    }else if(is_base_16){
      if('A' <= *str && *str <= 'F'){
        result += static_cast<off_t>(*str - 'A' + 0x0a);
      }else if('a' <= *str && *str <= 'f'){
        result += static_cast<off_t>(*str - 'a' + 0x0a);
      }else{
        return 0;
      }
    }else{
      return 0;
    }
  }
  return result;
}

string lower(string s)
{
  // change each character of the string to lower case
  for(size_t i = 0; i < s.length(); i++){
    s[i] = tolower(s[i]);
  }
  return s;
}

string trim_left(const string &s, const string &t /* = SPACES */)
{
  string d(s);
  return d.erase(0, s.find_first_not_of(t));
}

string trim_right(const string &s, const string &t /* = SPACES */)
{
  string d(s);
  string::size_type i(d.find_last_not_of(t));
  if(i == string::npos){
    return "";
  }else{
    return d.erase(d.find_last_not_of(t) + 1);
  }
}

string trim(const string &s, const string &t /* = SPACES */)
{
  return trim_left(trim_right(s, t), t);
}

/**
 * urlEncode a fuse path,
 * taking into special consideration "/",
 * otherwise regular urlEncode.
 */
string urlEncode(const string &s)
{
  string result;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '/' // Note- special case for fuse paths...
      || c == '.'
      || c == '-'
      || c == '_'
      || c == '~'
      || (c >= 'a' && c <= 'z')
      || (c >= 'A' && c <= 'Z')
      || (c >= '0' && c <= '9')) {
      result += c;
    } else {
      result += "%";
      result += hexAlphabet[static_cast<unsigned char>(c) / 16];
      result += hexAlphabet[static_cast<unsigned char>(c) % 16];
    }
  }
  return result;
}

/**
 * urlEncode a fuse path,
 * taking into special consideration "/",
 * otherwise regular urlEncode.
 */
string urlEncode2(const string &s)
{
  string result;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '=' // Note- special case for fuse paths...
      || c == '&' // Note- special case for s3...
      || c == '%'
      || c == '.'
      || c == '-'
      || c == '_'
      || c == '~'
      || (c >= 'a' && c <= 'z')
      || (c >= 'A' && c <= 'Z')
      || (c >= '0' && c <= '9')) {
      result += c;
    } else {
      result += "%";
      result += hexAlphabet[static_cast<unsigned char>(c) / 16];
      result += hexAlphabet[static_cast<unsigned char>(c) % 16];
    }
  }
  return result;
}

string urlDecode(const string& s)
{
  string result;
  for(size_t i = 0; i < s.length(); ++i){
    if(s[i] != '%'){
      result += s[i];
    }else{
      int ch = 0;
      if(s.length() <= ++i){
        break;       // wrong format.
      }
      ch += ('0' <= s[i] && s[i] <= '9') ? (s[i] - '0') : ('A' <= s[i] && s[i] <= 'F') ? (s[i] - 'A' + 0x0a) : ('a' <= s[i] && s[i] <= 'f') ? (s[i] - 'a' + 0x0a) : 0x00;
      if(s.length() <= ++i){
        break;       // wrong format.
      }
      ch *= 16;
      ch += ('0' <= s[i] && s[i] <= '9') ? (s[i] - '0') : ('A' <= s[i] && s[i] <= 'F') ? (s[i] - 'A' + 0x0a) : ('a' <= s[i] && s[i] <= 'f') ? (s[i] - 'a' + 0x0a) : 0x00;
      result += static_cast<char>(ch);
    }
  }
  return result;
}

bool takeout_str_dquart(string& str)
{
  size_t pos;

  // '"' for start
  if(string::npos != (pos = str.find_first_of('\"'))){
    str = str.substr(pos + 1);

    // '"' for end
    if(string::npos == (pos = str.find_last_of('\"'))){
      return false;
    }
    str = str.substr(0, pos);
    if(string::npos != str.find_first_of('\"')){
      return false;
    }
  }
  return true;
}

//
// ex. target="http://......?keyword=value&..."
//
bool get_keyword_value(string& target, const char* keyword, string& value)
{
  if(!keyword){
    return false;
  }
  size_t spos;
  size_t epos;
  if(string::npos == (spos = target.find(keyword))){
    return false;
  }
  spos += strlen(keyword);
  if('=' != target.at(spos)){
    return false;
  }
  spos++;
  if(string::npos == (epos = target.find('&', spos))){
    value = target.substr(spos);
  }else{
    value = target.substr(spos, (epos - spos));
  }
  return true;
}

/**
 * Returns the current date
 * in a format suitable for a HTTP request header.
 */
string get_date_rfc850()
{
  char buf[100];
  time_t t = time(NULL);
  strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&t));
  return buf;
}

void get_date_sigv3(string& date, string& date8601)
{
  time_t tm = time(NULL);
  date     = get_date_string(tm);
  date8601 = get_date_iso8601(tm);
}

string get_date_string(time_t tm)
{
  char buf[100];
  strftime(buf, sizeof(buf), "%Y%m%d", gmtime(&tm));
  return buf;
}

string get_date_iso8601(time_t tm)
{
  char buf[100];
  strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", gmtime(&tm));
  return buf;
}

bool get_unixtime_from_iso8601(const char* pdate, time_t& unixtime)
{
  if(!pdate){
    return false;
  }

  struct tm tm;
  char*     prest = strptime(pdate, "%Y-%m-%dT%T", &tm);
  if(prest == pdate){
    // wrong format
    return false;
  }
  unixtime = mktime(&tm);
  return true;
}

//
// Convert to unixtime from string which formatted by following:
//   "12Y12M12D12h12m12s", "86400s", "9h30m", etc
//
bool convert_unixtime_from_option_arg(const char* argv, time_t& unixtime)
{
  if(!argv){
    return false;
  }
  unixtime = 0;
  const char* ptmp;
  int         last_unit_type = 0;       // unit flag.
  bool        is_last_number;
  time_t      tmptime;
  for(ptmp = argv, is_last_number = true, tmptime = 0; ptmp && *ptmp; ++ptmp){
    if('0' <= *ptmp && *ptmp <= '9'){
      tmptime        *= 10;
      tmptime        += static_cast<time_t>(*ptmp - '0');
      is_last_number  = true;
    }else if(is_last_number){
      if('Y' == *ptmp && 1 > last_unit_type){
        unixtime      += (tmptime * (60 * 60 * 24 * 365));   // average 365 day / year
        last_unit_type = 1;
      }else if('M' == *ptmp && 2 > last_unit_type){
        unixtime      += (tmptime * (60 * 60 * 24 * 30));    // average 30 day / month
        last_unit_type = 2;
      }else if('D' == *ptmp && 3 > last_unit_type){
        unixtime      += (tmptime * (60 * 60 * 24));
        last_unit_type = 3;
      }else if('h' == *ptmp && 4 > last_unit_type){
        unixtime      += (tmptime * (60 * 60));
        last_unit_type = 4;
      }else if('m' == *ptmp && 5 > last_unit_type){
        unixtime      += (tmptime * 60);
        last_unit_type = 5;
      }else if('s' == *ptmp && 6 > last_unit_type){
        unixtime      += tmptime;
        last_unit_type = 6;
      }else{
        return false;
      }
      tmptime        = 0;
      is_last_number = false;
    }else{
      return false;
    }
  }
  if(is_last_number){
    return false;
  }
  return true;
}

std::string s3fs_hex(const unsigned char* input, size_t length)
{
  std::string hex;
  for(size_t pos = 0; pos < length; ++pos){
    char hexbuf[3];
    snprintf(hexbuf, 3, "%02x", input[pos]);
    hex += hexbuf;
  }
  return hex;
}

char* s3fs_base64(const unsigned char* input, size_t length)
{
  static const char* base = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
  char* result;

  if(!input || 0 == length){
    return NULL;
  }
  result = new char[((length / 3) + 1) * 4 + 1];

  unsigned char parts[4];
  size_t rpos;
  size_t wpos;
  for(rpos = 0, wpos = 0; rpos < length; rpos += 3){
    parts[0] = (input[rpos] & 0xfc) >> 2;
    parts[1] = ((input[rpos] & 0x03) << 4) | ((((rpos + 1) < length ? input[rpos + 1] : 0x00) & 0xf0) >> 4);
    parts[2] = (rpos + 1) < length ? (((input[rpos + 1] & 0x0f) << 2) | ((((rpos + 2) < length ? input[rpos + 2] : 0x00) & 0xc0) >> 6)) : 0x40;
    parts[3] = (rpos + 2) < length ? (input[rpos + 2] & 0x3f) : 0x40;

    result[wpos++] = base[parts[0]];
    result[wpos++] = base[parts[1]];
    result[wpos++] = base[parts[2]];
    result[wpos++] = base[parts[3]];
  }
  result[wpos] = '\0';

  return result;
}

inline unsigned char char_decode64(const char ch)
{
  unsigned char by;
  if('A' <= ch && ch <= 'Z'){                   // A - Z
    by = static_cast<unsigned char>(ch - 'A');
  }else if('a' <= ch && ch <= 'z'){             // a - z
    by = static_cast<unsigned char>(ch - 'a' + 26);
  }else if('0' <= ch && ch <= '9'){             // 0 - 9
    by = static_cast<unsigned char>(ch - '0' + 52);
  }else if('+' == ch){                         // +
    by = 62;
  }else if('/' == ch){                         // /
    by = 63;
  }else if('=' == ch){                         // =
    by = 64;
  }else{                                       // something wrong
    by = UCHAR_MAX;
  }
  return by;
}

unsigned char* s3fs_decode64(const char* input, size_t* plength)
{
  unsigned char* result;
  if(!input || 0 == strlen(input) || !plength){
    return NULL;
  }
  result = new unsigned char[strlen(input) + 1];

  unsigned char parts[4];
  size_t input_len = strlen(input);
  size_t rpos;
  size_t wpos;
  for(rpos = 0, wpos = 0; rpos < input_len; rpos += 4){
    parts[0] = char_decode64(input[rpos]);
    parts[1] = (rpos + 1) < input_len ? char_decode64(input[rpos + 1]) : 64;
    parts[2] = (rpos + 2) < input_len ? char_decode64(input[rpos + 2]) : 64;
    parts[3] = (rpos + 3) < input_len ? char_decode64(input[rpos + 3]) : 64;

    result[wpos++] = ((parts[0] << 2) & 0xfc) | ((parts[1] >> 4) & 0x03);
    if(64 == parts[2]){
      break;
    }
    result[wpos++] = ((parts[1] << 4) & 0xf0) | ((parts[2] >> 2) & 0x0f);
    if(64 == parts[3]){
      break;
    }
    result[wpos++] = ((parts[2] << 6) & 0xc0) | (parts[3] & 0x3f);
  }
  result[wpos] = '\0';
  *plength = wpos;
  return result;
}

/*
 * detect and rewrite invalid utf8.  We take invalid bytes
 * and encode them into a private region of the unicode
 * space.  This is sometimes known as wtf8, wobbly transformation format.
 * it is necessary because S3 validates the utf8 used for identifiers for
 * correctness, while some clients may provide invalid utf, notably
 * windows using cp1252.
 */

// Base location for transform.  The range 0xE000 - 0xF8ff
// is a private range, se use the start of this range.
static unsigned int escape_base = 0xe000;

// encode bytes into wobbly utf8.  
// 'result' can be null. returns true if transform was needed.
bool s3fs_wtf8_encode(const char *s, string *result)
{
  bool invalid = false;

  // Pass valid utf8 code through
  for (; *s; s++) {
    const unsigned char c = *s;

    // single byte encoding
    if (c <= 0x7f) {
      if (result) {
        *result += c;
      }
      continue;
    }

    // otherwise, it must be one of the valid start bytes
    if ( c >= 0xc2 && c <= 0xf5 ) {

      // two byte encoding
      // don't need bounds check, string is zero terminated
      if ((c & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
        // all two byte encodings starting higher than c1 are valid
        if (result) {
          *result += c;
          *result += *(++s);
        }
        continue;
      } 
      // three byte encoding
      if ((c & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
        const unsigned code = ((c & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
        if (code >= 0x800 && ! (code >= 0xd800 && code <= 0xd8ff)) {
          // not overlong and not a surrogate pair 
          if (result) {
            *result += c;
            *result += *(++s);
            *result += *(++s);
          }
          continue;
        }
      }
      // four byte encoding
      if ((c & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80) {
        const unsigned code = ((c & 0x07) << 18) | ((s[1] & 0x3f) << 12) | ((s[2] & 0x3f) << 6) | (s[3] & 0x3f);
        if (code >= 0x10000 && code <= 0x10ffff) {
          // not overlong and in defined unicode space
          if (result) {
            *result += c;
            *result += *(++s);
            *result += *(++s);
            *result += *(++s);
          }
          continue;
        }
      }
    }
    // printf("invalid %02x at %d\n", c, i);
    // Invalid utf8 code.  Convert it to a private two byte area of unicode
    // e.g. the e000 - f8ff area.  This will be a three byte encoding
    invalid = true;
    if (result) {
      unsigned escape = escape_base + c;
      *result += static_cast<char>(0xe0 | ((escape >> 12) & 0x0f));
      *result += static_cast<char>(0x80 | ((escape >> 06) & 0x3f));
      *result += static_cast<char>(0x80 | ((escape >> 00) & 0x3f));
    }
  }
  return invalid;
}

string s3fs_wtf8_encode(const string &s)
{
  string result;
  s3fs_wtf8_encode(s.c_str(), &result);
  return result;
}

// The reverse operation, turn encoded bytes back into their original values
// The code assumes that we map to a three-byte code point.
bool s3fs_wtf8_decode(const char *s, string *result)
{
  bool encoded = false;
  for (; *s; s++) {
    unsigned char c = *s;
    // look for a three byte tuple matching our encoding code
    if ((c & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
      unsigned code = (c & 0x0f) << 12;
      code |= (s[1] & 0x3f) << 6;
      code |= (s[2] & 0x3f) << 0;
      if (code >= escape_base && code <= escape_base + 0xff) {
        // convert back
        encoded = true;
        if(result){
          *result += static_cast<char>(code - escape_base);
        }
        s+=2;
        continue;
      }
    }
    if (result) {
      *result += c;
    }
  }
  return encoded;
}
 
string s3fs_wtf8_decode(const string &s)
{
  string result;
  s3fs_wtf8_decode(s.c_str(), &result);
  return result;
}

/*
* Local variables:
* tab-width: 4
* c-basic-offset: 4
* End:
* vim600: noet sw=4 ts=4 fdm=marker
* vim<600: noet sw=4 ts=4
*/
