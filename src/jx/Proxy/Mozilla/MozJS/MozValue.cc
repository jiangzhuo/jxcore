// Copyright & License details are available under JXCORE_LICENSE file

#include "MozValue.h"
#include <math.h>  // isnan
#include "../SpiderHelper.h"
#include "../EngineHelper.h"
#include "utf_man.h"

#include "../../EngineLogger.h"

namespace MozJS {

JSString *StringTools::FromUINT16(JSContext *ctx, const uint16_t *str,
                                  const int len) {
  char16_t *temp = (char16_t *)JS_malloc(ctx, sizeof(uint16_t) * (1 + len));
  memcpy(temp, str, sizeof(uint16_t) * len);
  temp[len] = jschar(0);
  return JS_NewUCString(ctx, temp, len);
}

JSString *StringTools::JS_ConvertToJSString(JSContext *cx, const char *source,
                                            size_t sz_source) {
  char16_t *cc = (char16_t *)JS_malloc(cx, (sz_source + 1) * sizeof(char16_t));
  int leno = utf8_to_utf16(cc, source, sz_source);
  if (leno != UTF8_ERROR) {  // UTF8 Check
    cc[leno] = jschar(0);
    return JS_NewUCString(cx, cc, leno);
  } else {
    JS_free(cx, cc);
    return JS_NewStringCopyN(cx, source, sz_source);
  }
}

void StringTools::JS_ConvertToJSChar(JSContext *cx, const char *source,
                                     size_t sz_source, auto_jschar *out) {
  out->ctx_ = cx;
  out->str_ = (char16_t *)JS_malloc(cx, (sz_source + 1) * sizeof(char16_t));
  out->length_ = utf8_to_utf16(out->str_, source, sz_source);
  if (out->length_ != UTF8_ERROR) {
    out->str_[out->length_] = jschar(0);
  } else {
    // force ascii to char16_t
    for (int i = 0; i < sz_source; i++) {
      char buffer[2] = {source[i], 0};
      char16_t *ref = (char16_t *)buffer;
      out->str_[i] = *ref;
    }
    out->str_[sz_source] = jschar(0);
    out->length_ = sz_source;
  }
}

void StringTools::JS_ConvertToJSChar(JSContext *cx, JSString *source,
                                     auto_jschar *out) {
  out->ctx_ = cx;
  out->str_ = JS_GetTwoByteString(cx, source, out->length_);

  // not two bytes
  if (out->length_ == -1 && out->str_ == NULL) {
    auto_str achar;
    EngineHelper::FromJSString(source, cx, &achar, true);
    JS_ConvertToJSChar(cx, achar.str_, achar.length_, out);
  }
}

void StringTools::JS_ConvertToJSChar(const String &source, auto_jschar *out) {
  JS_ConvertToJSChar(source.ctx_, source.value_.toString(), out);
}

Script &Script::operator=(const Script &value) {
  value_ = value.value_;
  rooted_ = value.fake_rooting_;
  empty_ = value.empty_;
  ctx_ = value.ctx_;
  if (rooted_) {
    assert(!empty_);
    JS::AddScriptRootOLD(value.ctx_, &value_);
  }
  fake_rooting_ = false;
  return *this;
}

void Script::AddRoot() {
  if (!rooted_ && !empty_) {
    rooted_ = true;
    JS::AddScriptRootOLD(ctx_, &value_);
  }
}

void Script::RemoveRoot() {
  if (rooted_ && !empty_) {
    rooted_ = false;
    JS::Heap<JSScript *> rt_value_(value_);
    JS::RemoveScriptRoot(ctx_, &rt_value_);
  }
}

Script Script::Compile(JSContext *cx, const Value &hst,
                       const auto_jschar &source, const char *filename,
                       bool for_asm_js) {
  Script script;
  script.ctx_ = cx;

  if (!hst.value_.isObjectOrNull()) return script;

  if (!hst.value_.isObject()) {
    error_console("Warning: Script host wasn't an object, skip compiling.\n");
    return script;
  }

  JS::RootedObject host(cx, hst.value_.toObjectOrNull());
  JS::RootedScript rs(cx);

  if (!JS_CompileUCScriptJX(cx, host, source.str_, source.length_, filename, 1,
                            for_asm_js, &rs)) {
    return script;
  }

  script.value_ = rs;
  script.empty_ = false;

  return script;
}

Script Script::Compile(JSContext *cx, const Value &hst, const auto_str &source,
                       const char *filename) {
  Script script;
  script.ctx_ = cx;

  if (!hst.value_.isObjectOrNull()) return script;

  JS::RootedObject host(cx, hst.value_.toObjectOrNull());

  JSAutoCompartment ac(cx, host);

  JS::RootedScript rs(cx);

  {
    JS::CompileOptions compile_options(cx);
    compile_options.setFileAndLine(filename, 1);
    compile_options.setCompileAndGo(false);

    JS_CompileScript(cx, host, source.str_, source.length_, compile_options,
                     &rs);
  }

  script.value_ = rs;
  if (!script.value_) {
    return script;
  }
  script.empty_ = false;

  return script;
}

Script Script::Compile(JSContext *ctx, const Value &hst, const String &source,
                       const String &filename, bool for_asm_js) {
  auto_jschar _source;
  StringTools::JS_ConvertToJSChar(ctx, source.value_.toString(), &_source);

  auto_str _filename;
  filename.ToSTDString(&_filename);

  return Compile(ctx, hst, _source, _filename.str_, for_asm_js);
}

Script Script::Compile(JSContext *ctx, const String &source,
                       const String &filename) {
  Value global = jxcore::getGlobal(JS_GetThreadId(ctx));
  auto_jschar _source;

  StringTools::JS_ConvertToJSChar(ctx, source.value_.toString(), &_source);

  auto_str _filename;
  filename.ToSTDString(&_filename);

  return Compile(ctx, global, _source, _filename.str_);
}

Value Script::Run(const Value &host_object_) {
  JSObject *hst = host_object_.GetRawObjectPointer();
  JSCompartment *jsco = JS_EnterCompartment(ctx_, hst);
  JS::RootedObject host(ctx_, hst);

  Value result(Value::NewEmptyObject(ctx_), ctx_);

  JS::RootedScript rs_scr(ctx_, value_);
  JS::RootedValue rv_res(ctx_, result.value_);
  if (!JS_ExecuteScript(ctx_, host, rs_scr, &rv_res)) {
    result.value_ = rv_res;
    result.empty_ = true;
    goto ret_urn;
  }

  result.value_ = rv_res;

ret_urn:
  JS_LeaveCompartment(ctx_, jsco);
  return result;
}

Value Script::Run() {
  Value global = jxcore::getGlobal(JS_GetThreadId(ctx_));
  return Run(global);
}

Value::Value() {
  ctx_ = NULL;
  rooted_ = false;
  empty_ = true;
  fake_rooting_ = false;
  is_exception_ = false;
}

Value::Value(const JS::Value &value, JSContext *ctx, bool rooted) {
  value_ = value;
  rooted_ = rooted;
  empty_ = false;
  ctx_ = ctx;
  if (rooted) {
    JS::AddValueRootOLD(ctx, &value_);
  }
  fake_rooting_ = false;
  is_exception_ = false;
}

#define DEFINE_OPERATOR(left, right)                    \
  left &left::operator=(const right &value) {           \
    value_ = value.value_;                              \
    rooted_ = value.fake_rooting_;                      \
    empty_ = value.empty_;                              \
    ctx_ = value.ctx_;                                  \
    is_exception_ = value.is_exception_;                \
    if (empty_) {                                       \
      rooted_ = false;                                  \
    } else if (rooted_) {                               \
      assert(JS::AddValueRootOLD(value.ctx_, &value_)); \
    }                                                   \
    fake_rooting_ = false;                              \
    return *this;                                       \
  }

DEFINE_OPERATOR(Value, Value)
DEFINE_OPERATOR(String, String)

Value::Value(const Value &value) : value_(value.value_) {
  rooted_ = false;
  empty_ = value.empty_;
  ctx_ = value.ctx_;
  fake_rooting_ = value.fake_rooting_;
  is_exception_ = false;
}

Value::Value(const Value &value, bool rooted) {
  value_ = value.value_;
  rooted_ = rooted;
  empty_ = value.empty_;
  ctx_ = value.ctx_;
  if (rooted) {
    assert(!empty_);
    JS::AddValueRootOLD(value.ctx_, &value_);
  }
  fake_rooting_ = false;
  is_exception_ = false;
}

bool Value::IsEmpty() const { return empty_; }

#define EMPTY_RETURN() \
  if (empty_) return false

bool Value::IsArray() const {
  EMPTY_RETURN();
  if (value_.isNullOrUndefined() || !value_.isObject()) return false;

  JS::RootedObject obj(ctx_);
  JS::RootedValue rt_value_(ctx_, value_);
  if (!JS_ValueToObject(ctx_, rt_value_, &obj)) return false;

  return JS_IsArrayObject(ctx_, obj);
}

bool Value::IsBoolean() const {
  EMPTY_RETURN();
  return value_.isBoolean();
}

bool Value::IsBooleanObject() const {
  EMPTY_RETURN();

  // TODO(obastemur) fix this!
  return value_.isBoolean() && value_.isObject();
}

bool Value::IsDate() const {
  EMPTY_RETURN();
  if (value_.isNullOrUndefined() || !value_.isObject()) return false;

  JS::RootedObject obj(ctx_);
  JS::RootedValue rt_value_(ctx_, value_);
  if (!JS_ValueToObject(ctx_, rt_value_, &obj)) return false;
  return JS_ObjectIsDate(ctx_, obj);
}

bool Value::IsExternal() const {
  EMPTY_RETURN();

  if (!value_.isObject()) return false;
  return JS_GetHasExternalArrayData(value_.toObjectOrNull());
}

bool Value::IsFalse() const {
  EMPTY_RETURN();
  return value_.isFalse();
}

bool Value::IsTrue() const {
  EMPTY_RETURN();
  return value_.isTrue();
}

bool Value::IsFunction() const {
  EMPTY_RETURN();

  if (value_.isNullOrUndefined() || !value_.isObject()) return false;

  return JS_ObjectIsFunction(ctx_, value_.toObjectOrNull());
}

bool Value::IsInt32() const {
  EMPTY_RETURN();
  return value_.isInt32();
}

bool Value::IsUint32() const {
  EMPTY_RETURN();
  return value_.isInt32() && value_.toInt32() >= 0;
}

bool Value::IsNativeError() const {
  EMPTY_RETURN();

  return is_exception_;
}

bool Value::IsNull() const {
  EMPTY_RETURN();
  return value_.isNull();
}

bool Value::IsUndefined() const {
  EMPTY_RETURN();
  return value_.isUndefined();
}

bool Value::IsNumber() const {
  EMPTY_RETURN();
  return value_.isNumber();
}

bool Value::IsNumberObject() const {
  EMPTY_RETURN();
  return value_.isNumber() && value_.isObject();
}

bool Value::IsObject() const {
  EMPTY_RETURN();
  return value_.isObject();
}

bool Value::IsRegExp() const {
  EMPTY_RETURN();

  if (!value_.isObject()) return false;

  JSObject *obj = value_.toObjectOrNull();
  if (obj == nullptr) return false;

  JS::RootedObject rt_obj(ctx_, obj);
  return JS_ObjectIsRegExp(ctx_, rt_obj);
}

bool Value::IsString() const {
  EMPTY_RETURN();
  return value_.isString();
}

bool Value::IsStringObject() const {
  EMPTY_RETURN();
  return value_.isString();
}

bool Value::Equals(Value *that) const {
  EMPTY_RETURN();
  if (that->empty_) return false;

  bool result = false;
  JS::RootedValue r1(ctx_, value_);
  JS::RootedValue r2(that->ctx_, that->value_);

  if (JS_LooselyEqual(ctx_, r1, r2, &result)) return result;
  return false;
}

int32_t Value::Int32Value() {
  if (value_.isInt32()) {
    return value_.toInt32();
  } else if (value_.isNumber()) {
    return (int32_t)value_.toNumber();
  } else if (value_.isBoolean()) {
    return value_.toBoolean() ? 1 : 0;
  } else if (value_.isString()) {
    auto_str str;
    ToSTDString(&str);
    return atol(*str);
  } else if (value_.isNullOrUndefined()) {
    return 0;
  }

  assert(0 && "Can not convert object to integer");
  return 0;
}

uint32_t Value::Uint32Value() {
  if (value_.isInt32())
    return value_.toPrivateUint32();
  else
    return (uint32_t)Int32Value();
}

int64_t Value::IntegerValue() { return Int32Value(); }

bool Value::BooleanValue() {
  if (value_.isBoolean())
    return value_.toBoolean();
  else if (value_.isNumber())
    return value_.toNumber() != 0;

  return !value_.isNullOrUndefined();
}

double Value::NumberValue() {
  if (value_.isNumber())
    return value_.toNumber();
  else if (value_.isBoolean())
    return value_.toBoolean() ? 1 : 0;

  if (value_.isString()) {
    auto_str str;
    ToSTDString(&str);
    return atof(*str);
  }

  assert(0 && "Can not convert object to number");
  return 0;
}

String Value::ToString() {
  Value &_this = *this;
  return String(_this);
}

void Value::AddRoot() {
  if (!rooted_ && !empty_) {
    rooted_ = true;
    JS::AddValueRootOLD(ctx_, &value_);
  }
}

void Value::RemoveRoot() {
  if (rooted_ && !empty_) {
    rooted_ = false;
    if (!EngineHelper::IsInstanceAlive(ctx_)) return;
    JS::RemoveValueRootOLD(ctx_, &value_);
  }
}

#define INNER_VALUE_TO_OBJECT(name, force_unroot) \
  JS::RootedObject name##rt(ctx_);                \
  JS::RootedValue value__(ctx_, value_);          \
  JS_ValueToObject(ctx_, value__, &name##rt);     \
  JSObject *name = name##rt

void Value::SetPrivate(void *data) {
  INNER_VALUE_TO_OBJECT(object_, false);

  assert(object_ != nullptr &&
         "Can not set private data into none object JS variable");

  if (!JS_HasReservedSlot(object_, GC_SLOT_GC_CALL)) {
    // perhaps this object was created on JS land
    // add a property that can trigger the event when the base
    // object is GC'ed
    object_ = Natify(object_rt);
  }
  JS_SetPrivate(object_, data);
}

void *Value::GetPointerFromInternalField(const int index) {
  INNER_VALUE_TO_OBJECT(object_, false);

  if (object_ != nullptr) {
    if (!JS_HasReservedSlot(object_, GC_SLOT_GC_CALL)) {
      // perhaps this object was created on JS land
      // add a property that can trigger the event when the base
      // object is GC'ed
      object_ = Natify(object_rt);
    }
  } else {
    return nullptr;
  }
  return JS_GetPrivate(object_);
}

int Value::InternalFieldCount() const {
  if (!value_.isObject()) return 0;

  JSObject *object_ = value_.toObjectOrNull();
  if (object_ != nullptr) {
    if (JS_HasReservedSlot(object_, 1)) {
      return 1;
    }
  }

  return 0;
}

void Value::SetInternalFieldCount(int count) {
  if (!value_.isObject()) return;

  JSObject *object_ = value_.toObjectOrNull();

  if (object_ != nullptr) {
    if (!JS_HasReservedSlot(object_, 1)) {
      // perhaps this object was created on JS land
      // add a property that can trigger the event when the base
      // object is GC'ed
      JS::RootedObject object_rt(ctx_, object_);
      object_ = Natify(object_rt);
    }
  }
}

Value Value::FromInteger(JSContext *ctx, const int32_t n) {
  jsval val = JS::Int32Value(n);
  return Value(val, ctx);
}

Value Value::FromBoolean(JSContext *ctx, const bool n) {
  jsval val = JS::BooleanValue(n);
  return Value(val, ctx);
}

Value Value::FromUnsigned(JSContext *ctx, const unsigned n) {
  jsval val = JS::PrivateUint32Value(n);
  return Value(val, ctx);
}

Value Value::FromDouble(JSContext *ctx, const double n) {
  jsval val;
  if (std::isnan(n))
    val = JS::DoubleNaNValue();
  else
    val = JS::DoubleValue(n);

  return Value(val, ctx);
}

Value Value::Undefined(JSContext *ctx) {
  jsval val = JSVAL_VOID;
  return Value(val, ctx);
}

Value Value::Null(JSContext *ctx) {
  jsval val = JSVAL_NULL;
  return Value(val, ctx);
}

void Value::SetIndexedPropertiesToExternalArrayData(void *data,
                                                    const int data_type,
                                                    const int32_t length) {
  INNER_VALUE_TO_OBJECT(object_, false);

  assert(object_ != nullptr);
  JS_SetExternalArrayData(object_, data, length, data_type);
}

void *Value::GetIndexedPropertiesExternalArrayData() {
  INNER_VALUE_TO_OBJECT(object_, false);

  if (object_ == nullptr) return 0;
  return JS_GetExternalArrayData(object_);
}

int32_t Value::GetIndexedPropertiesExternalArrayDataLength() {
  INNER_VALUE_TO_OBJECT(object_, false);

  if (object_ == nullptr) return 0;
  return JS_GetExternalArrayDataSize(object_);
}

int Value::GetIndexedPropertiesExternalArrayDataType() {
  INNER_VALUE_TO_OBJECT(object_, false);

  if (object_ == nullptr) return 0;
  return JS_GetExternalArrayDataType(object_);
}

bool Value::HasInstance(const Value &val) {
  if (!val.IsObject()) return false;
  INNER_VALUE_TO_OBJECT(object_, false);

  JS::RootedValue rv_val(ctx_, val.value_);
  JS::RootedObject rv_obj(ctx_);
  if (!JS_ValueToObject(ctx_, rv_val, &rv_obj)) return false;

  const JSClass *csp = JS_GetClass(object_);
  if (csp == NULL) return false;

  bool res = JS_InstanceOf(ctx_, rv_obj, csp, NULL);
  return res;
}

bool Value::StrictEquals(const Value &val) {
  bool res = false;

  if (JS_StrictlyEqual(ctx_, value_, val.value_, &res)) return res;

  return false;
}

void Value::ToSTDString(auto_str *out) const {
  if (!value_.isString()) {
    return;
  }
  String str;
  str.ctx_ = ctx_;
  str.value_ = value_;
  str.empty_ = false;
  EngineHelper::FromJSString(str, out);
}

Value Value::RootCopy() {
  fake_rooting_ = true;
  return *this;
}

void Value::MakeWeak(void *_, JS_FINALIZER_METHOD method) {
  RemoveRoot();
  if (_ != NULL) {
    this->SetFinalizer(method);
    this->SetPrivate(_);
  }
}

void Value::ClearWeak() { AddRoot(); }

void Value::Clear() {
  if (empty_) return;
  value_ = JSVAL_VOID;
  empty_ = true;
}

void Value::Dispose() { RemoveRoot(); }

// STRING
String::String(JSString *obj, JSContext *ctx, bool rooted) {
  value_ = JS::StringValue(obj);
  rooted_ = rooted;
  ctx_ = ctx;
  empty_ = false;
  if (rooted) {
    JS::AddValueRootOLD(ctx_, &value_);
  }
  fake_rooting_ = false;
  is_exception_ = false;
}

String::String(const JS::Value &val, JSContext *ctx, bool rooted) {
  value_ = val;
  rooted_ = rooted;
  ctx_ = ctx;
  empty_ = false;
  if (rooted) {
    JS::AddValueRootOLD(ctx_, &value_);
  }
  fake_rooting_ = false;
  is_exception_ = false;
}

String::String(JSContext *ctx, const char *str, const int len, bool rooted) {
  ctx_ = ctx;
  String _str = String::FromSTD(ctx, str, len);
  value_ = _str.value_;
  rooted_ = rooted;
  empty_ = false;
  if (rooted) {
    JS::AddValueRootOLD(ctx_, &value_);
  }
  fake_rooting_ = false;
  is_exception_ = false;
}

String::String(const String &value) {
  value_ = value.value_;
  rooted_ = false;
  empty_ = value.empty_;
  ctx_ = value.ctx_;
  fake_rooting_ = value.fake_rooting_;
  is_exception_ = false;
}

String::String(const Value &value) {
  value_ = value.value_;
  rooted_ = false;
  empty_ = value.empty_;
  ctx_ = value.ctx_;
  fake_rooting_ = value.fake_rooting_;
  is_exception_ = false;
}

int String::StringLength() const {
  if (empty_) return 0;
  if (!value_.isString()) return 0;

  return JS_GetStringEncodingLength(ctx_, value_.toString());
}

int String::Utf8Length() const {
  if (empty_) return 0;
  if (!value_.isString()) return 0;
  return JS_GetStringUTF8Length(ctx_, value_.toString());
}

String String::FromSTD(JSContext *ctx, const char *str, const int len) {
  return String(JS_NewStringCopyN(ctx, str, len != 0 ? len : strlen(str)), ctx);
}

String String::FromUTF8(JSContext *ctx, const char *str, const int len) {
  const int slen = len != 0 ? len : strlen(str);

  bool wide = false;
  for (int i = 0; i < slen; i++) {
    const char ch = *(str + i);
    if ((ch & 0x80) != 0) {
      wide = true;
      break;
    }
  }

  if (wide) {
    JSString *js_str = StringTools::JS_ConvertToJSString(ctx, str, slen);
    assert(js_str != NULL);
    return String(js_str, ctx);
  }

  return String(JS_NewStringCopyN(ctx, str, slen), ctx);
}

String String::FromSTD(JSContext *ctx, const uint16_t *str, const int len) {
  JSString *js_str = StringTools::FromUINT16(ctx, str, len);

  assert(js_str != NULL);

  return String(js_str, ctx);
}

// OBJECT

Value::Value(JSObject *obj, JSContext *ctx, bool rooted) {
  assert(obj != NULL);

  value_ = JS::ObjectOrNullValue(obj);
  ctx_ = ctx;
  empty_ = false;
  rooted_ = rooted;
  if (rooted) {
    JS::AddValueRootOLD(ctx_, &value_);
  }
  fake_rooting_ = false;
  is_exception_ = false;
}

JSFinalizeOp object_finalizer = NULL;

struct ObjectFinalizer {
  JS_FINALIZER_METHOD target;
};

void Value::empty_finalize(JSFreeOp *fop, JSObject *obj) {
  if (JS_HasReservedSlot(obj, GC_SLOT_JS_CLASS)) {  // jsclass info
    jsval __ = JS_GetReservedSlot(obj, GC_SLOT_JS_CLASS);
    if (__.isObjectOrNull()) {
      JSObject *robj = __.toObjectOrNull();
      if (robj != nullptr) {
        void *ff = (void *)JS_GetPrivate(robj);
        free(ff);
        int *tid = (int *)JS_GetRuntimePrivate(fop->runtime());
        JSContext *ctx = Isolate::GetByThreadId(*tid)->GetRaw();
        jsval val = JS::ObjectOrNullValue(robj);
        JS::RemoveValueRootOLD(ctx, &val);
      }
    }
  }

  if (JS_HasReservedSlot(obj, GC_SLOT_GC_CALL)) {  // callback info
    jsval __ = JS_GetReservedSlot(obj, GC_SLOT_GC_CALL);
    if (__.isObjectOrNull()) {
      JSObject *robj = __.toObjectOrNull();
      if (robj != nullptr) {
        ObjectFinalizer *ff = (ObjectFinalizer *)JS_GetPrivate(robj);
        int *tid = (int *)JS_GetRuntimePrivate(fop->runtime());
        JSContext *ctx = Isolate::GetByThreadId(*tid)->GetRaw();
        Value val(obj, ctx);
        ff->target(val, JS_GetPrivate(obj));
        free(ff);
        val.rooted_ = true;
        val.RemoveRoot();
        return;
      }
    }
  }

  if (!JS_HasReservedSlot(obj,
                          JS_NATIFIED_OBJECT_SLOT_INDEX)) {  // check if it is a
                                                             // Natified Object!
    if (object_finalizer != NULL) {
      object_finalizer(fop, obj);
    }
  }
}

static JSClass empty_class_definition = {
    "JXObject",
    JSCLASS_HAS_PRIVATE | JSCLASS_HAS_RESERVED_SLOTS(JS_OBJECT_SLOT_COUNT) |
        JSCLASS_NEW_RESOLVE,
    JS_PropertyStub, JS_DeletePropertyStub, JS_PropertyStub,
    JS_StrictPropertyStub, JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub,
    Value::empty_finalize, 0, 0, 0, 0};

static JSClass empty_natified_definition = {
    "JXNatify", JSCLASS_HAS_PRIVATE |
                    JSCLASS_HAS_RESERVED_SLOTS(JS_NATIFIED_OBJECT_SLOT_COUNT) |
                    JSCLASS_NEW_RESOLVE,
    JS_PropertyStub, JS_DeletePropertyStub, JS_PropertyStub,
    JS_StrictPropertyStub, JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub,
    Value::empty_finalize, 0, 0, 0, 0};

static JSClass empty_reserved_definition = {
    "JXReserved",          JSCLASS_HAS_PRIVATE, JS_PropertyStub,
    JS_DeletePropertyStub, JS_PropertyStub,     JS_StrictPropertyStub,
    JS_EnumerateStub,      JS_ResolveStub,      JS_ConvertStub,
    0,                     0,                   0,
    0,                     0};

const char *natifier_name_ =
    "                                                                ";
JSObject *Value::Natify(JS::HandleObject object_rt) {
  JS::RootedValue ret_val(ctx_);

  bool op = JS_GetProperty(ctx_, object_rt, natifier_name_, &ret_val);
  if (!op || ret_val.isUndefined()) {
    JSObject *obj = JS_NewObject(ctx_, &empty_natified_definition,
                                 JS::NullPtr(), JS::NullPtr());
    jsval val = JS::ObjectOrNullValue(obj);
    JS::RootedValue rt_val(ctx_, val);
    JS_SetProperty(ctx_, object_rt, natifier_name_, rt_val);
    return obj;
  }

  return ret_val.toObjectOrNull();
}

void Value::SetFinalizer(JS_FINALIZER_METHOD method) {
  if (!value_.isObject()) return;
  INNER_VALUE_TO_OBJECT(object_, false);

  assert(object_ != nullptr);
  if (!JS_HasReservedSlot(object_, GC_SLOT_GC_CALL)) {
    // perhaps this object was created on JS land
    // add a property that can trigger the event when the base
    // object is GC'ed
    object_ = Natify(object_rt);
  }

  jsval slot_val = JS_GetReservedSlot(object_, GC_SLOT_GC_CALL);
  if (slot_val.isObjectOrNull()) {
    JSObject *rval_obj = slot_val.toObjectOrNull();
    if (rval_obj != nullptr) {
      ObjectFinalizer *ff = (ObjectFinalizer *)JS_GetPrivate(rval_obj);
      ff->target = method;
      return;
    }
  }

  JSObject *reserved_obj = JS_NewObject(ctx_, &empty_reserved_definition,
                                        JS::NullPtr(), JS::NullPtr());

  ObjectFinalizer *objectFinalizer = new ObjectFinalizer;
  objectFinalizer->target = method;
  JS_SetPrivate(reserved_obj, objectFinalizer);

  jsval rval = JS::ObjectOrNullValue(reserved_obj);
  JS::AddValueRootOLD(ctx_, &rval);
  JS_SetReservedSlot(object_, GC_SLOT_GC_CALL, rval);
}

void Value::SetGlobalFinalizer(JSFinalizeOp method) {
  object_finalizer = method;
}

JSObject *Value::NewEmptyObject(JSContext *ctx) {
  return JS_NewObject(ctx, &empty_class_definition, JS::NullPtr(),
                      JS::NullPtr());
}

static JSClass empty_prop_definition = {
    "JXPropObject",
    JSCLASS_HAS_PRIVATE | JSCLASS_HAS_RESERVED_SLOTS(JS_OBJECT_SLOT_COUNT) |
        JSCLASS_NEW_RESOLVE,
    JS_PropertyStub, JS_DeletePropertyStub, JS_PropertyStub,
    JS_StrictPropertyStub, JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub,
    Value::empty_finalize, 0, 0, 0, 0};

static bool resolver_(JSContext *__contextORisolate, JS::HandleObject ___obj,
                      JS::HandleId ___id) {
  return true;
}

static bool enumer_(JSContext *cx, JS::HandleObject obj) { return true; }

JSObject *Value::NewEmptyPropertyObject(JSContext *ctx, JSPropertyOp add_get,
                                        JSStrictPropertyOp set,
                                        JSResolveOp resolve,
                                        JSEnumerateOp enumerate,
                                        JSDeletePropertyOp del) {
  JSClass *emp = (JSClass *)malloc(sizeof(JSClass));
  *emp = empty_prop_definition;
  emp->finalize = Value::empty_finalize;

  if (add_get != NULL) {
    emp->addProperty = add_get;
    emp->getProperty = add_get;
  }

  if (del != NULL) {
    emp->delProperty = del;
  }

  if (set != NULL) {
    emp->setProperty = set;
  }

  if (enumerate != NULL) {
    emp->enumerate = enumerate;
  } else {
    emp->enumerate = enumer_;
  }

  if (resolve != NULL) {
    emp->resolve = resolve;
  } else {
    emp->resolve = resolver_;
  }

  JSObject *obj = JS_NewObject(ctx, emp, JS::NullPtr(), JS::NullPtr());

  JSObject *reserved_obj = JS_NewObject(ctx, &empty_reserved_definition,
                                        JS::NullPtr(), JS::NullPtr());

  JS_SetPrivate(reserved_obj, emp);

  jsval rval = JS::ObjectOrNullValue(reserved_obj);
  JS::AddValueRootOLD(ctx, &rval);
  JS_SetReservedSlot(obj, GC_SLOT_JS_CLASS, rval);

  return obj;
}

void Value::SetReserved(const int index, const Value &value) {
  assert(index <= JS_OBJECT_SLOT_MAX_INDEX);

  INNER_VALUE_TO_OBJECT(object_, false);

  if (object_ != nullptr) {
    if (!JS_HasReservedSlot(object_, GC_SLOT_GC_CALL)) {
      // perhaps this object was created on JS land
      // add a property that can trigger the event when the base
      // object is GC'ed
      object_ = Natify(object_rt);
    }
  }

  assert(object_ != nullptr);
  JS_SetReservedSlot(object_, index, value.value_);
}

Value Value::GetReserved(const int index) {
  assert(index <= JS_OBJECT_SLOT_MAX_INDEX);

  INNER_VALUE_TO_OBJECT(object_, false);

  if (object_ == nullptr) {
    return Value();
  } else {
    if (!JS_HasReservedSlot(object_, GC_SLOT_GC_CALL)) {
      // perhaps this object was created on JS land
      // add a property that can trigger the event when the base
      // object is GC'ed
      object_ = Natify(object_rt);
    }
  }

  if (!JS_HasReservedSlot(object_, index)) return Value();
  jsval rval = JS_GetReservedSlot(object_, index);
  return Value(rval, ctx_);
}

bool Value::Has(const String &_name) const {
  auto_str name;
  EngineHelper::FromJSString(_name, &name);
  bool foundp = Has(name.str_);

  return foundp;
}

bool Value::Has(const char *name) const {
  if (empty_) return false;
  bool foundp;
  JS::RootedObject object_rt(ctx_);
  JS::RootedValue value__(ctx_, value_);
  JS_ValueToObject(ctx_, value__, &object_rt);

  if (object_rt == nullptr) return false;
  if (!JS_HasProperty(ctx_, object_rt, name, &foundp)) {
    foundp = false;
  }

  return foundp;
}

Value Value::GetPropertyNames() {
  Value val;
  EngineHelper::GetPropertyNames(ctx_, this, &val);

  return val;
}

Value Value::Get(const String &_name) const {
  auto_str name;
  _name.ToSTDString(&name);

  Value val = Get(name.str_);
  return val;
}

Value Value::Get(const char *name) const {
  Value val;
  val.ctx_ = ctx_;
  val.empty_ = false;

  JS::RootedValue ret_val(ctx_);
  JS::RootedObject object_rt(ctx_);
  JS::RootedValue value__(ctx_, value_);
  JS_ValueToObject(ctx_, value__, &object_rt);

  if (object_rt == nullptr) return Value();
  if (JS_GetProperty(ctx_, object_rt, name, &ret_val))
    val.value_ = ret_val;
  else
    val.value_ = JSVAL_NULL;

  return val;
}

Value Value::GetIndex(const int index) {
  Value val;
  val.ctx_ = ctx_;
  val.empty_ = false;

  JS::RootedValue ret_val(ctx_);
  INNER_VALUE_TO_OBJECT(object_, false);

  if (object_ == nullptr) return Value();
  if (JS_GetElement(ctx_, object_rt, index, &ret_val))
    val.value_ = ret_val;
  else
    val.value_ = JSVAL_NULL;
  return val;
}

bool Value::SetProperty(const String &_name, const Value &val_) {
  auto_str name;
  EngineHelper::FromJSString(_name, &name);

  JS::RootedValue val(ctx_, val_.value_);

  return SetProperty(name.str_, val);
}

bool Value::SetProperty(const String &_name, JS::HandleValue val) {
  auto_str name;
  EngineHelper::FromJSString(_name, &name);

  return SetProperty(name.str_, val);
}

bool Value::SetProperty(const char *name, JS::HandleValue val) {
  INNER_VALUE_TO_OBJECT(object_, false);

  return JS_SetProperty(ctx_, object_rt, name, val);
}

bool Value::SetProperty(const String &_name, JSNative method,
                        const uint16_t parameter_count) {
  auto_str name;
  _name.ToSTDString(&name);
  return SetProperty(name.str_, method, parameter_count);
}

bool Value::SetProperty(const char *name, JSNative method,
                        const uint16_t parameter_count) {
  INNER_VALUE_TO_OBJECT(object_, false);

  JSFunction *fun = JS_DefineFunction(ctx_, object_rt, name, method,
                                      parameter_count, JSPROP_ENUMERATE);

  return fun != NULL;
}

bool Value::DeleteProperty(const String &_name) {
  auto_str name;
  _name.ToSTDString(&name);

  return DeleteProperty(name.str_);
}

bool Value::DeleteProperty(const char *name) {
  INNER_VALUE_TO_OBJECT(object_, false);

  bool result = JS_DeleteProperty(ctx_, object_rt, name);

  return result;
}

bool Value::SetStaticFunction(const String &_name, JSNative method,
                              const uint16_t parameter_count) {
  auto_str name;
  _name.ToSTDString(&name);
  return SetStaticFunction(name.str_, method, parameter_count);
}

bool Value::SetStaticFunction(const char *name, JSNative method,
                              const uint16_t parameter_count) {
  INNER_VALUE_TO_OBJECT(object_, false);

  JSFunctionSpec methods[2] = {
      JS_FS(name, method, parameter_count, JSPROP_ENUMERATE), JS_FS_END};

  return JS_DefineFunctions(ctx_, object_rt, methods);
}

bool Value::SetIndex(const int index, const Value &val) {
  JS::RootedValue set_val(ctx_, val.value_);

  return SetIndex(index, set_val);
}

bool Value::SetIndex(const int index, JS::HandleValue val) {
  INNER_VALUE_TO_OBJECT(object_, false);

  bool result = JS_SetElement(ctx_, object_rt, index, val);

  return result;
}

bool Value::DefineGetterSetter(const String &_name, JSPropertyOp getter,
                               JSStrictPropertyOp setter,
                               const Value &initial_value) {
  INNER_VALUE_TO_OBJECT(object_, false);
  JS::RootedId ri_id(ctx_);
  JS::RootedValue rt_nmval(ctx_, _name.value_);
  JS_ValueToId(ctx_, rt_nmval, &ri_id);

  JS::RootedValue rt_inval(ctx_, initial_value.value_);
  bool success = JS_DefinePropertyById(ctx_, object_rt, ri_id, rt_inval, 0,
                                       getter, setter);

  return success;
}

Value Value::CompileAndRun(JSContext *ctx_, String script, String filename,
                           MozJS::Value *_global) {
  Value obj;
  obj.ctx_ = ctx_;
  obj.empty_ = false;
  MozJS::Value global;
  if (_global == NULL)
    global = jxcore::getGlobal(JS_GetThreadId(ctx_));
  else
    global = *_global;

  auto_jschar str_script;

  JS::RootedValue rt_scval(ctx_, script.value_);
  StringTools::JS_ConvertToJSChar(ctx_, JS::ToString(ctx_, rt_scval),
                                  &str_script);

  auto_str str_filename;
  filename.ToSTDString(&str_filename);

  Script scr =
      Script::Compile(ctx_, global, str_script, str_filename.str_, false);
  if (scr.IsEmpty())
    obj.value_ = JSVAL_NULL;
  else
    return scr.Run(global);

  return obj;
}

Value Value::GetConstructor() {
  String str_cons = String::FromSTD(ctx_, "constructor", 11);
  if (Has(str_cons)) {
    return Get(str_cons);
  }
  return Value();
}

JSObject *Value::NewInstance(int argc, jsval *args) {
  // ENGINE_LOG_THIS("Value", "Call8");
  INNER_VALUE_TO_OBJECT(object_, false);

  JSObject *rv;

  JS::RootedValue ret_val(ctx_);
  if (!JS_GetProperty(ctx_, object_rt, "constructor", &ret_val)) {
    rv = JS_New(ctx_, object_rt,
                JS::HandleValueArray::fromMarkedLocation(argc, args));
  } else {
    JS::RootedObject from(ctx_);
    JS_ValueToObject(ctx_, ret_val, &from);
    rv = JS_New(ctx_, from,
                JS::HandleValueArray::fromMarkedLocation(argc, args));
  }

  return rv;
}

Value Value::NewInstance(int argc, Value *_args) {
  // ENGINE_LOG_THIS("Value", "Call7");
  jsval *args;
  if (argc > 0) {
    args = new jsval[argc];
    for (int i = 0; i < argc; i++) args[i] = _args[i].value_;
  } else {
    argc = 0;
    args = NULL;
  }

  Value rval;
  rval.empty_ = false;
  rval.ctx_ = ctx_;
  rval.value_ = JS::ObjectOrNullValue(NewInstance(argc, args));

  if (args != NULL) delete args;
  return rval;
}

Value Value::Call(const String &_name, int argc, Value *_args) const {
  // ENGINE_LOG_THIS("Value", "Call6");
  auto_str name;
  EngineHelper::FromJSString(_name, &name);

  return Call(name.str_, argc, _args);
}

Value Value::Call(const char *name, int argc, Value *_args) const {
  // ENGINE_LOG_THIS("Value", "Call5");
  Value rval;
  jsval *args = NULL;

  if (argc > 0) {
    args = new jsval[argc];
    for (int i = 0; i < argc; i++) args[i] = _args[i].value_;
  }

  rval.ctx_ = ctx_;
  JS::RootedValue rov(ctx_);
  if (!Call(name, argc, args, &rov)) {
    if (args != NULL) delete args;
    return Value();
  }

  rval.empty_ = false;
  rval.value_ = rov;

  if (args != NULL) delete args;

  return rval;
}

Value Value::Call(const char *name, int argc, jsval *args) const {
  // ENGINE_LOG_THIS("Value", "Call3");
  Value rval;

  rval.ctx_ = ctx_;
  JS::RootedValue rov(ctx_);
  if (!Call(name, argc, args, &rov)) {
    return Value();
  }

  rval.empty_ = false;
  rval.value_ = rov;

  return rval;
}

bool Value::Call(const char *name, int argc, JS::Value *args,
                 JS::MutableHandleValue rov) const {
  JS::RootedValue prop(ctx_);
  JS::RootedObject object_rt(ctx_);
  JS::RootedValue value__(ctx_, value_);
  JS_ValueToObject(ctx_, value__, &object_rt);
  if (JS_GetProperty(ctx_, object_rt, name, &prop)) {
    JS_CallFunctionValue(ctx_, object_rt, prop,
                         JS::HandleValueArray::fromMarkedLocation(argc, args),
                         rov);
    return true;
  }
  return false;
}

Value Value::Call(const Value &host, int argc, jsval *args) const {
  // ENGINE_LOG_THIS("Value", "Call2");
  if (host.IsString()) {
    auto_str host_str;
    host.ToSTDString(&host_str);
    return Call(host_str.str_, argc, args);
  }

  Value rval;
  rval.ctx_ = ctx_;
  JS::RootedValue prop(ctx_, value_);

  JS::RootedValue rt_hval(ctx_, host.value_);
  JS::RootedObject rob(ctx_);
  if (!JS_ValueToObject(ctx_, rt_hval, &rob)) {
    return rval;
  }

  rval.empty_ = false;
  JS::RootedValue rov(ctx_);
  JS_CallFunctionValue(ctx_, rob, prop,
                       JS::HandleValueArray::fromMarkedLocation(argc, args),
                       &rov);
  rval.value_ = rov;

  return rval;
}

Value Value::Call(const Value &host, int argc, Value *_args) const {
  // ENGINE_LOG_THIS("Value", "Call1");
  if (host.IsString()) {
    auto_str host_str;
    host.ToSTDString(&host_str);
    return Call(host_str.str_, argc, _args);
  }

  Value rval;
  rval.ctx_ = ctx_;
  JS::RootedValue prop(ctx_, value_);

  jsval *args = NULL;
  if (argc > 0) {
    args = new jsval[argc];
    for (int i = 0; i < argc; i++) args[i] = _args[i].value_;
  }

  JS::RootedValue rt_hval(ctx_, host.value_);
  JS::RootedObject rob(ctx_);
  if (!JS_ValueToObject(ctx_, rt_hval, &rob)) {
    if (args != NULL) delete args;
    return rval;
  }

  rval.empty_ = false;
  JS::RootedValue rov(ctx_);
  JS_CallFunctionValue(ctx_, rob, prop,
                       JS::HandleValueArray::fromMarkedLocation(argc, args),
                       &rov);
  rval.value_ = rov;

  if (args != NULL) delete args;

  return rval;
}

unsigned Value::ArrayLength() const {
  unsigned ln = 0;

  JS::RootedObject object_rt(ctx_);
  JS::RootedValue value__(ctx_, value_);
  JS_ValueToObject(ctx_, value__, &object_rt);

  if (JS_GetArrayLength(ctx_, object_rt, &ln))
    return ln;
  else
    return 0;
}

Value Value::NewEmptyFunction(JSContext *cx) {
  JS::CompileOptions options(cx);
  options.setFileAndLine("Value_NewEmptyFunction", 1);

  JS::RootedObject temph(cx, Value::NewEmptyObject(cx));

  JS::RootedFunction fun(cx);
  JS_CompileFunction(cx, temph, "func", 0, NULL, "", 0, options, &fun);
  JSObject *funobj = JS_GetFunctionObject(fun);

  return Value(funobj, cx);

  // Below is a backup implementation for JS_CompileFunction
  //  String scr = String::FromSTD(cx, "(function(){ var _ = function(){};
  // return _; })", 0);
  //  String name = String::FromSTD(cx, "Value_NewEmptyFunction", 0);
  //  return Value::CompileAndRun(cx, scr, name);
}

static JSClass constructor_class_definition = {
    "NativeFunction",      JSCLASS_HAS_PRIVATE, JS_PropertyStub,
    JS_DeletePropertyStub, JS_PropertyStub,     JS_StrictPropertyStub,
    JS_EnumerateStub,      JS_ResolveStub,      JS_ConvertStub,
    NULL,                  0,                   0,
    0,                     0};

Value::Value(JSNative native, bool instance, JSContext *cx) {
  ctx_ = cx;
  JSObject *local_new = Value::NewEmptyObject(cx);
  JS::RootedObject lnob(ctx_, local_new);

  if (!instance) {
    JSFunction *fun =
        JS_DefineFunction(cx, lnob, "nativefunc", native, 0, JSPROP_ENUMERATE);
    JSObject *fun_ob = JS_GetFunctionObject(fun);

    value_ = JS::ObjectOrNullValue(fun_ob);
  } else {
    JSObject *inited =
        JS_InitClass(cx, lnob, JS::NullPtr(), &constructor_class_definition,
                     native, 0, NULL, NULL, NULL, NULL);

    value_ = JS::ObjectOrNullValue(inited);
  }

  empty_ = false;
  rooted_ = false;
}

Value::~Value() {
  if (rooted_) {
    if (EngineHelper::IsInstanceAlive(ctx_)) {
      assert(!rooted_ && "Don't let rooted/persistent object die");
    }
  }
}
}  // namespace MozJS
