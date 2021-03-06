/* Copyright (c) 2014, Oguz Bastemur (oguz@bastemur.com)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SRC_JX_PROXY_MOZILLA_JXSTRING_H_
#define SRC_JX_PROXY_MOZILLA_JXSTRING_H_
#include "MozJS/MozJS.h"
#include "PMacro.h"

namespace jxcore {

class JXString {
  char *str_;
  size_t length_;
  size_t utf8_length_;
  bool autogc_;
  JSString *value_;
  bool ascii_char_set_;

  void set_handle();

 public:
  JSContext *ctx_;

  void set_std(const char *other, JSContext *ctx = NULL);
  void set_handle(const JS_HANDLE_VALUE &str, bool get_ascii = false);
  void set_handle(JSString *str, JSContext *ctx);
  void GetASCII();

  static JXString CreateString(JSString *str, JSContext *ctx);
  static JXString CreateString(const char *str, JSContext *ctx);
  static MozJS::String CreateNativeString(const char *str, JSContext *ctx);
  static JXString CreateString(JS_HANDLE_VALUE str);

  JXString();
  JXString(const char *str, JSContext *ctx);
  JXString(JSString *str, JSContext *ctx, bool autogc = true,
           bool get_ascii = false);
  explicit JXString(const JS_HANDLE_VALUE &str, void *ctx = NULL);
  ~JXString();

  void Dispose();

  char *operator*();
  const char *operator*() const;

  size_t length() const;
  size_t Utf8Length() const;
  void GetUTF8LetterAt(const size_t index, MozJS::auto_str *chars);
  size_t WriteUtf8(char *buf, const size_t buflen, int *chars_written);

  JSString *ToJSString();

  void DisableAutoGC() { autogc_ = false; }
};

}  // namespace jxcore

#endif  // SRC_JX_PROXY_MOZILLA_JXSTRING_H_
