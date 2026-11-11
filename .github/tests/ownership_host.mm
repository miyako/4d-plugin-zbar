#include "4DPlugin-ZBAR.h"
#include "ARRAY_TEXT.h"
#include "C_BLOB.h"
#include "C_LONGINT.h"
#include "PrivateTypes.h"
#include "EntryPoints.h"
#include <type_traits>

static void check(bool valid, const char* message)
{
    if (!valid) { std::cerr << message << "\n"; std::exit(1); }
}
static CUTF16String utf16(const char* text)
{
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> codec;
    auto u = codec.from_bytes(text);
    return CUTF16String(reinterpret_cast<const PA_Unichar*>(u.data()), u.size());
}
static std::string utf8(const PA_Unichar* text, size_t units)
{
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> codec;
    if (!units) return {};
    return codec.to_bytes(reinterpret_cast<const char16_t*>(text),
                          reinterpret_cast<const char16_t*>(text) + units);
}
static std::map<PA_Unichar*, size_t> strings;
static std::map<PA_Handle, size_t> handles;
static std::map<PA_Picture, int> pictures;
struct Reference {
    std::map<CUTF16String, PA_Variable> properties;
    std::vector<PA_Variable> elements;
    int owners = 1;
    bool collection = false;
};
static std::map<void*, Reference*> refs;
static std::string kind;
static CUTF16String expected;
static bool nullEmpty, failHandle, failResize;
static int propertyReads, collectionReads, scans, enabledTypes;
static bool decodeEmpty, decodeThrows, nativeNull;
static size_t nativeOwners;
static Reference* makeRef(bool collection = false)
{
    auto r = new Reference; r->collection = collection; refs[r] = r; return r;
}
static PA_Unistring makeString(const PA_Unichar* text)
{
    size_t units = 0;
    while (text && text[units]) ++units;
    PA_Unistring result = {};
    if (units || !nullEmpty) {
        result.fString = new PA_Unichar[units + 1];
        if (units) std::memcpy(result.fString, text, units * sizeof(PA_Unichar));
        result.fString[units] = 0; result.fLength = units;
        strings[result.fString] = units;
    }
    return result;
}
static void disposeString(PA_Unistring value)
{
    if (!value.fString) { check(!value.fLength, "null nonempty string"); return; }
    check(strings.count(value.fString), "unowned/double string disposal");
    check(strings.at(value.fString) == size_t(value.fLength), "string allocation length");
    strings.erase(value.fString); delete[] value.fString;
}
static void textEquals(PA_Unistring value, const CUTF16String& wanted)
{
    check(value.fLength == PA_long32(wanted.size()), "exact UTF16 code-unit count");
    check(value.fString || wanted.empty(), "missing nonempty string");
    if (value.fString) {
        check(strings.count(value.fString), "read released string");
        check(strings.at(value.fString) == wanted.size(), "capacity excludes terminator");
        check(value.fString[value.fLength] == 0, "terminator boundary");
        check(CUTF16String(value.fString, value.fLength) == wanted, "exact text contents");
    }
}
static void clear(PA_Variable*);
static void releaseRef(void* p)
{
    if (!p) return;
    check(refs.count(p), "release dead reference");
    auto r = refs.at(p); check(r->owners > 0, "positive owner balance");
    if (--r->owners) return;
    for (auto& pair : r->properties) clear(&pair.second);
    for (auto& v : r->elements) clear(&v);
    refs.erase(p); delete r;
}
static PA_Picture makePicture()
{ auto p = new unsigned char(42); pictures[p] = 1; return p; }
static PA_Variable clone(PA_Variable value)
{
    if (value.fType == eVK_Unistring) value.uValue.fString = makeString(value.uValue.fString.fString);
    else if (value.fType == eVK_Object || value.fType == eVK_Collection) {
        auto p = value.fType == eVK_Object ? value.uValue.fObject : value.uValue.fCollection;
        if (p) { check(refs.count(p), "copy live reference"); ++refs.at(p)->owners; }
    } else if (value.fType == eVK_Picture && value.uValue.fPicture) {
        check(pictures.count(value.uValue.fPicture), "copy live picture");
        ++pictures[value.uValue.fPicture];
    }
    return value;
}
static void clear(PA_Variable* value)
{
    switch (value->fType) {
    case eVK_Unistring: disposeString(value->uValue.fString); break;
    case eVK_Picture:
        if (auto p = value->uValue.fPicture) {
            check(pictures.count(p), "release live picture");
            if (--pictures[p] == 0) { pictures.erase(p); delete static_cast<unsigned char*>(p); }
        }
        break;
    case eVK_Object: releaseRef(value->uValue.fObject); break;
    case eVK_Collection: releaseRef(value->uValue.fCollection); break;
    case eVK_ArrayUnicode: case eVK_ArrayObject: case eVK_Blob: {
        auto h = value->fType == eVK_Blob ? value->uValue.fBlob.fHandle : value->uValue.fArray.fData;
        if (!h) break;
        check(handles.count(h), "clear live handle");
        if (value->fType != eVK_Blob) {
            auto count = value->uValue.fArray.fNbElements + 1;
            auto size = value->fType == eVK_ArrayUnicode ? sizeof(PA_Unistring) : sizeof(PA_ObjectRef);
            check(handles.at(h) >= count * size, "array metadata within capacity");
            for (PA_long32 i = 0; i < count; ++i) {
                if (value->fType == eVK_ArrayUnicode) disposeString(reinterpret_cast<PA_Unistring*>(*h)[i]);
                else releaseRef(reinterpret_cast<PA_ObjectRef*>(*h)[i]);
            }
        }
        std::free(*h); delete h; handles.erase(h); break;
    }
    case eVK_Undefined: case static_cast<char>(eVK_Null):
    case eVK_Boolean: case eVK_Real: case eVK_Longint: break;
    default: check(false, "cleanup of unspecified failed/raw-kind0 return");
    }
    value->fType = eVK_Undefined;
}
static PA_Variable resultValue()
{
    auto v = PA_CreateVariable(eVK_Undefined);
    if (kind == "string" || kind == "set-error") {
        auto s = makeString(expected.c_str()); PA_SetStringVariable(&v, &s);
    } else if (kind == "object") PA_SetObjectVariable(&v, makeRef());
    else if (kind == "collection") PA_SetCollectionVariable(&v, makeRef(true));
    else if (kind == "picture") PA_SetPictureVariable(&v, makePicture());
    else if (kind == "boolean") PA_SetBooleanVariable(&v, true);
    else if (kind == "real") PA_SetRealVariable(&v, 3.25);
    else check(false, "unknown synthetic result");
    return v;
}
static void failedResult(PA_Variable* v)
{ std::memset(v, kind == "error-poison" ? 0xa5 : 0, sizeof(*v)); }
static void host(short selector, EngineBlock* eb)
{
    switch (selector) {
    case EX_CREATE_UNISTRING: eb->fUniString1 = makeString(reinterpret_cast<PA_Unichar*>(eb->fHandle)); break;
    case EX_DISPOSE_UNISTRING: disposeString(eb->fUniString1); break;
    case EX_SET_UNISTRING: {
        auto s = makeString(reinterpret_cast<PA_Unichar*>(eb->fHandle));
        disposeString(eb->fUniString1); eb->fUniString1 = s; break;
    }
    case EX_VARIABLE_TO_STRING: {
        auto v = reinterpret_cast<PA_Variable*>(eb->fHandle);
        check(v->fType == eVK_Unistring, "reading failed/non-string variable");
        textEquals(v->uValue.fString, expected); eb->fUniString1 = v->uValue.fString; break;
    }
    case EX_CLEAR_VARIABLE: clear(static_cast<PA_Variable*>(eb->fPtr1)); break;
    case EX_CREATE_PICTURE: eb->fPicture = makePicture(); break;
    case EX_DISPOSE_PICTURE: {
        auto v = PA_CreateVariable(eVK_Undefined); PA_SetPictureVariable(&v, eb->fPicture); clear(&v); break;
    }
    case EX_DUPLICATE_PICTURE:
        check(pictures.count(eb->fPicture) && eb->fParam1 == 0, "retain live picture");
        ++pictures[eb->fPicture]; break;
    case EX_CREATE_NATIVE_PICTURE_FOR_SCREEN:
        check(pictures.count(eb->fPicture), "input picture remains alive");
        if (nativeNull) { eb->fHandle = nullptr; break; }
#ifdef OWNERSHIP_WINDOWS
        eb->fHandle = reinterpret_cast<PA_Handle>(new Gdiplus::Bitmap);
#else
        eb->fHandle = reinterpret_cast<PA_Handle>(new unsigned char(9));
#endif
        ++nativeOwners; break;
    case EX_GET_INFORMATION: eb->fParam2 = 0x1630; break;
    case EX_SET_OBJ_VALUE: {
        check(refs.count(eb->fPtr1), "set live reference");
        if (kind == "set-error") { eb->fError = eER_IncompatibleTypes; break; }
        auto r = refs.at(eb->fPtr1);
        auto value = clone(*static_cast<PA_Variable*>(eb->fPtr2));
        if (eb->fShort == eVK_Object) {
            auto key = CUTF16String(eb->fUniString1.fString, eb->fUniString1.fLength);
            auto it = r->properties.find(key);
            if (it != r->properties.end()) clear(&it->second);
            r->properties[key] = value;
        } else {
            check(eb->fShort == eVK_Collection && size_t(eb->fLongint) == r->elements.size(), "append collection element");
            r->elements.push_back(value);
        }
        break;
    }
    case EX_GET_OBJ_VALUE: {
        check(refs.count(eb->fPtr1), "get live reference");
        auto r = refs.at(eb->fPtr1); auto v = static_cast<PA_Variable*>(eb->fPtr2);
        if (eb->fShort == eVK_Collection && eb->fLongint == -1) {
            PA_SetLongintVariable(v, r->elements.size()); break;
        }
        if (eb->fShort == eVK_Object) ++propertyReads; else ++collectionReads;
        if (kind.find("error-") == 0) { failedResult(v); eb->fError = eER_IncompatibleTypes; }
        else if (eb->fShort == eVK_Collection) {
            check(size_t(eb->fLongint) < r->elements.size(), "valid collection index");
            *v = clone(r->elements[eb->fLongint]);
        } else *v = resultValue();
        break;
    }
    case EX_HANDLE_MANAGER: {
        auto h = eb->fHandle;
        switch (eb->fParam1) {
        case 1:
            if (failHandle) { eb->fHandle = nullptr; eb->fError = eER_IncompatibleTypes; break; }
            h = new char*; *h = static_cast<char*>(std::calloc(1, std::max<sLONG_PTR>(1, eb->fParam2)));
            check(*h, "test handle allocation"); handles[h] = eb->fParam2; eb->fHandle = h; break;
        case 2: check(handles.count(h), "dispose live handle"); std::free(*h); delete h; handles.erase(h); break;
        case 3:
            check(handles.count(h), "resize live handle");
            if (failResize) { eb->fError = eER_IncompatibleTypes; break; }
            *h = static_cast<char*>(std::realloc(*h, std::max<sLONG_PTR>(1, eb->fParam2)));
            check(*h, "test handle resize"); handles[h] = eb->fParam2; break;
        case 4: check(handles.count(h), "size live handle"); eb->fParam2 = handles[h]; break;
        case 5: check(handles.count(h), "lock live handle"); eb->fParam3 = reinterpret_cast<sLONG_PTR>(*h); break;
        case 6: check(handles.count(h), "unlock live handle"); break;
        default: check(false, "unexpected handle operation");
        }
        break;
    }
    case EX_EXECUTE_COMMAND_BY_ID: {
        auto args = static_cast<PA_Variable**>(eb->fPtr1);
        auto v = static_cast<PA_Variable*>(eb->fPtr2);
        check(v->fType == eVK_Undefined, "command result initially Undefined");
        if (eb->fParam1 == 1471) PA_SetObjectVariable(v, makeRef());
        else if (eb->fParam1 == 1472) PA_SetCollectionVariable(v, makeRef(true));
        else if (eb->fParam1 == 1231) {
            check(refs.count(args[0]->uValue.fObject), "existence on live object");
            PA_SetBooleanVariable(v, kind != "absent");
        } else if (eb->fParam1 == 1225) {
            check(refs.count(args[0]->uValue.fObject), "copy live argument");
            if (kind == "error-copy") { failedResult(v); eb->fError = eER_IncompatibleTypes; }
            else if (kind.find("copy-") == 0) {
                auto saved = kind; kind = kind.substr(5); *v = resultValue(); kind = saved;
            } else {
                PA_SetObjectVariable(v, makeRef());
                if (kind == "error-handle") failHandle = true;
            }
        } else if (eb->fParam1 == 1217) {
            check(refs.count(args[0]->uValue.fObject), "stringify live copied argument");
            if (kind.find("error-") == 0) { failedResult(v); eb->fError = eER_IncompatibleTypes; }
            else *v = resultValue();
        } else check(false, "unexpected engine command");
        break;
    }
    default: std::cerr << "unexpected engine selector " << selector << "\n"; std::exit(1);
    }
}

struct TextResource { CUTF16String text; };
static std::map<CFTypeRef, TextResource*> resources;
static CFStringRef storeText(CUTF16String text)
{
    auto r = new TextResource{std::move(text)};
    auto p = reinterpret_cast<CFStringRef>(r); resources[p] = r; return p;
}
static const CUTF16String& resourceText(CFStringRef p)
{ check(resources.count(p), "synthetic text resource"); return resources.at(p)->text; }
CFStringRef OwnershipStringBytes(CFAllocatorRef, const UInt8* bytes, CFIndex count, CFStringEncoding encoding, Boolean)
{
    if (encoding == kCFStringEncodingUTF8) {
        std::string value(reinterpret_cast<const char*>(bytes), count); return storeText(utf16(value.c_str()));
    }
    check(encoding == kCFStringEncodingUTF32LE && count % sizeof(wchar_t) == 0, "UTF32 byte count");
    auto w = reinterpret_cast<const wchar_t*>(bytes); CUTF16String value;
    for (size_t i = 0; i < size_t(count) / sizeof(wchar_t); ++i) {
        uint32_t c = w[i];
        if (c <= 0xffff) value.push_back(c);
        else { c -= 0x10000; value.push_back(0xd800 + (c >> 10)); value.push_back(0xdc00 + (c & 1023)); }
    }
    return storeText(value);
}
CFStringRef OwnershipStringCharacters(CFAllocatorRef, const UniChar* text, CFIndex count)
{ return storeText(count ? CUTF16String(text, count) : CUTF16String()); }
CFIndex OwnershipStringLength(CFStringRef p) { return resourceText(p).size(); }
void OwnershipStringCharactersAt(CFStringRef p, CFRange range, UniChar* out)
{
    const auto& text = resourceText(p);
    check(range.location >= 0 && range.length >= 0 && size_t(range.location + range.length) <= text.size(), "UTF16 source bounds");
    std::memcpy(out, text.data() + range.location, range.length * sizeof(UniChar));
}
CFIndex OwnershipStringCapacity(CFIndex units, CFStringEncoding encoding)
{ check(encoding == kCFStringEncodingUTF8, "UTF8 capacity"); return units * 3; }
CFIndex OwnershipStringGetBytes(CFStringRef p, CFRange range, CFStringEncoding encoding, UInt8, Boolean,
                              UInt8* out, CFIndex capacity, CFIndex* used)
{
    const auto& text = resourceText(p);
    check(encoding == kCFStringEncodingUTF8 && range.location == 0 && size_t(range.length) == text.size(), "UTF8 conversion range");
    auto bytes = utf8(text.data(), text.size());
    check(capacity >= CFIndex(bytes.size()), "UTF8 byte capacity");
    if (!bytes.empty()) std::memcpy(out, bytes.data(), bytes.size());
    *used = bytes.size(); return text.size();
}
void OwnershipRelease(CFTypeRef p)
{ check(resources.count(p), "release synthetic string"); delete resources.at(p); resources.erase(p); }
CFURLRef OwnershipURLCreate(CFAllocatorRef, CFStringRef, CFURLPathStyle, Boolean)
{ check(false, "forbidden path boundary"); return nullptr; }
CFStringRef OwnershipURLPath(CFURLRef, CFURLPathStyle)
{ check(false, "forbidden path boundary"); return nullptr; }
@implementation OwnershipURL
- (id)initWithString:(id)value { check(false, "forbidden URL boundary"); return nil; }
- (id)absoluteString { check(false, "forbidden URL boundary"); return nil; }
@end
size_t OwnershipImageWidth(CGImageRef) { return 2; }
size_t OwnershipImageHeight(CGImageRef) { return 2; }
CGColorSpaceRef OwnershipColorCreate(CFStringRef) { ++nativeOwners; return reinterpret_cast<CGColorSpaceRef>(new unsigned char(1)); }
void OwnershipColorRelease(CGColorSpaceRef p) { --nativeOwners; delete reinterpret_cast<unsigned char*>(p); }
CGContextRef OwnershipContextCreate(void* data, size_t w, size_t h, size_t bits, size_t row, CGColorSpaceRef, uint32_t)
{
    check(w == 2 && h == 2 && bits == 8 && row == 8, "synthetic bitmap geometry");
    ++nativeOwners; return reinterpret_cast<CGContextRef>(new void*(data));
}
void OwnershipDraw(CGContextRef ctx, CGRect, CGImageRef)
{ auto data = *reinterpret_cast<void**>(ctx); std::memset(data, 255, 16); }
void* OwnershipContextData(CGContextRef ctx) { return *reinterpret_cast<void**>(ctx); }
void OwnershipContextRelease(CGContextRef p) { --nativeOwners; delete reinterpret_cast<void**>(p); }
void OwnershipImageRelease(CGImageRef p) { --nativeOwners; delete reinterpret_cast<unsigned char*>(p); }
void OwnershipTransform(CGContextRef, CGFloat, CGFloat) { check(false, "unexpected graphics transform"); }
#ifdef OWNERSHIP_WINDOWS
Gdiplus::Bitmap::~Bitmap() { --nativeOwners; }
int MultiByteToWideChar(unsigned cp, unsigned, LPCSTR in, int bytes, LPWSTR out, int capacity)
{
    check(cp == CP_UTF8 && bytes >= 0, "UTF8 input bytes");
    std::string s(in, bytes); auto value = utf16(s.c_str());
    if (!out) return value.size();
    check(capacity == int(value.size()), "UTF16 capacity in code units");
    std::memcpy(out, value.data(), value.size() * sizeof(PA_Unichar)); return value.size();
}
int WideCharToMultiByte(unsigned cp, unsigned, LPCWSTR in, int units, LPSTR out, int capacity, LPCSTR, BOOL*)
{
    check(cp == CP_UTF8 && units >= 0, "UTF16 input units"); auto value = utf8(in, units);
    if (!out) return value.size();
    check(capacity == int(value.size()), "UTF8 byte capacity");
    std::memcpy(out, value.data(), value.size()); return value.size();
}
#endif
namespace zbar {
struct zbar_image_scanner_s {};
struct zbar_image_s { const void* data = nullptr; unsigned long size = 0; };
struct zbar_symbol_s { int index; };
static zbar_symbol_s symbols[] = {{0}, {1}};
static std::set<zbar_image_scanner_t*> scanners;
static std::set<zbar_image_t*> images;
zbar_image_scanner_t* zbar_image_scanner_create()
{ auto p = new zbar_image_scanner_t; scanners.insert(p); return p; }
void zbar_image_scanner_destroy(zbar_image_scanner_t* p)
{ check(scanners.erase(p), "release synthetic scanner"); delete p; }
int zbar_image_scanner_set_config(zbar_image_scanner_t* p, zbar_symbol_type_t type, zbar_config_t config, int value)
{
    check(scanners.count(p) && config == ZBAR_CFG_ENABLE, "synthetic scanner config");
    if (type != ZBAR_NONE && value) ++enabledTypes;
    return 0;
}
zbar_image_t* zbar_image_create() { auto p = new zbar_image_t; images.insert(p); return p; }
void zbar_image_destroy(zbar_image_t* p)
{ check(images.erase(p), "release synthetic scan image"); delete p; }
void zbar_image_set_format(zbar_image_t* p, unsigned long format)
{ check(images.count(p) && format == zbar_fourcc('Y','8','0','0'), "Y800 format"); }
void zbar_image_set_size(zbar_image_t* p, unsigned w, unsigned h)
{ check(images.count(p) && w == 2 && h == 2, "scan dimensions"); }
void zbar_image_set_data(zbar_image_t* p, const void* data, unsigned long size, zbar_image_cleanup_handler_t* cleanup)
{ check(images.count(p) && size == 4 && !cleanup, "borrowed in-memory pixels"); p->data = data; p->size = size; }
int zbar_scan_image(zbar_image_scanner_t* scanner, zbar_image_t* image)
{
    check(scanners.count(scanner) && images.count(image), "synthetic decode boundary");
    auto bytes = static_cast<const uint8_t*>(image->data);
    check(image->size == 4 && bytes[0] == 255 && bytes[3] == 255, "synthetic in-memory pixel contents");
    ++scans; if (decodeThrows) throw std::runtime_error("synthetic decoder failure");
    return decodeEmpty ? 0 : 2;
}
const zbar_symbol_t* zbar_image_first_symbol(const zbar_image_t*) { return &symbols[0]; }
const zbar_symbol_t* zbar_symbol_next(const zbar_symbol_t* p) { return p->index ? nullptr : &symbols[1]; }
const char* zbar_symbol_get_data(const zbar_symbol_t*) { static std::string s; s = utf8(expected.data(), expected.size()); return s.c_str(); }
zbar_symbol_type_t zbar_symbol_get_type(const zbar_symbol_t*) { return ZBAR_QRCODE; }
int zbar_symbol_get_quality(const zbar_symbol_t*) { return 7; }
const char* zbar_get_symbol_name(zbar_symbol_type_t) { return "QR-Code"; }
const char* zbar_get_addon_name(zbar_symbol_type_t) { return ""; }
}

static void arrayCase()
{
    auto array = PA_CreateVariable(eVK_ArrayUnicode);
    PA_ResizeArray(&array, 2);
    for (PA_long32 i = 0; i <= 2; ++i) {
        auto text = PA_CreateUnistring(const_cast<PA_Unichar*>(expected.c_str()));
        PA_SetStringInArray(array, i, &text);
    }
    BytePtr args[] = {reinterpret_cast<BytePtr>(&array)};
    {
        ARRAY_TEXT wrapper; wrapper.fromParamAtIndex(args, 1);
        check(wrapper.getSize() == 3, "element zero included");
        for (PA_long32 i = 0; i <= 2; ++i) {
            CUTF16String text; wrapper.copyUTF16StringAtIndex(&text, i);
            check(text == expected, "borrowed getter copied");
        }
        PA_ResizeArray(&array, 0);
        wrapper.toParamAtIndex(args, 1);
        wrapper.toParamAtIndex(args, 1);
        for (int i = 0; i < 128; ++i) {
            auto churn = PA_CreateUnistring(const_cast<PA_Unichar*>(expected.c_str()));
            PA_DisposeUnistring(&churn);
        }
        for (PA_long32 i = 0; i <= 2; ++i) textEquals(PA_GetStringInArray(array, i), expected);
    }
    PA_ClearVariable(&array);
}
static void rejectionCase()
{
    auto array = PA_CreateVariable(eVK_ArrayUnicode);
    for (int which = 0; which < 4; ++which) {
        auto text = PA_CreateUnistring(const_cast<PA_Unichar*>(expected.c_str()));
        auto bad = array;
        if (which == 0) bad.fType = eVK_Real;
        if (which == 1) bad.uValue.fArray.fData = nullptr;
        PA_SetStringInArray(bad, which == 2 ? -1 : which == 3 ? 1 : 0, &text);
        textEquals(text, expected); PA_DisposeUnistring(&text);
    }
    auto before = PA_GetStringInArray(array, 0);
    PA_SetStringInArray(array, 0, nullptr);
    check(PA_GetStringInArray(array, 0).fString == before.fString, "null descriptor rejection");
    PA_ClearVariable(&array);
}
static void wrapperCase()
{
    C_TEXT text; text.setUTF16String(expected.data(), expected.size());
    CUTF8String u8; text.copyUTF8String(&u8);
    check(std::string(reinterpret_cast<const char*>(u8.data()), u8.size()) == utf8(expected.data(), expected.size()), "UTF8 exact bytes");
    text.setUTF8String(&u8);
    check(text.getUTF16Length() == expected.size(), "roundtrip UTF16 units");
    auto out = PA_CreateUnistring(nullptr); BytePtr args[] = {reinterpret_cast<BytePtr>(&out)};
    text.toParamAtIndex(args, 1); textEquals(out, expected);
    text.fromParamAtIndex(args, 1); PA_DisposeUnistring(&out);
    C_BLOB blob; uint8_t bytes[] = {1,2,3}; blob.setBytes(bytes, 3);
    check(blob.getBytesLength() == 3 && blob.getBytesPtr()[2] == 3, "BLOB value control");
    C_LONGINT number; number.setIntValue(42); check(number.getIntValue() == 42, "scalar control");
}
static void dispatchCase(const std::string& mode)
{
    auto src = mode == "no-picture" ? nullptr : makePicture();
    auto types = makeRef(true);
    if (kind.find("error-") == 0) types->elements.push_back(PA_CreateVariable(eVK_Undefined));
    else types->elements.push_back(resultValue());
    void* args[] = {&src, &types}; PA_ObjectRef output = nullptr;
    PluginBlock params = {args, &output, nullptr};
    decodeEmpty = mode == "decode-empty"; decodeThrows = mode == "decode-throws"; nativeNull = mode == "native-null";
    PluginMain(1, &params);
    check(refs.count(output), "actual dispatcher returns owned object");
    auto result = refs.at(output);
    const bool success = mode == "dispatch";
    check(result->properties.at(utf16("success")).uValue.fBoolean == success, "preserved success flag");
    if (success) {
        auto values = result->properties.at(utf16("values")).uValue.fCollection;
        check(refs.count(values) && refs.at(values)->elements.size() == 2, "two synthetic barcode results");
        for (auto& v : refs.at(values)->elements) {
            auto object = refs.at(v.uValue.fObject);
            check(object->properties.size() == 4, "barcode object shape");
            textEquals(object->properties.at(utf16("data")).uValue.fString, expected);
            textEquals(object->properties.at(utf16("name")).uValue.fString, utf16("QR-Code"));
            textEquals(object->properties.at(utf16("addOn")).uValue.fString, utf16(""));
            check(object->properties.at(utf16("confidence")).uValue.fReal == 7, "barcode confidence");
        }
        check(scans == 1 && collectionReads == 1, "actual collection filter and decode reached");
        check(enabledTypes == (kind == "real" ? 1 : 0), "only numeric type filters enabled");
    }
    releaseRef(output); releaseRef(types);
    if (src) { auto v = PA_CreateVariable(eVK_Undefined); PA_SetPictureVariable(&v, src); clear(&v); }
}
int main(int argc, char** argv)
{
    check(argc == 5, "case/text/empty/kind arguments");
    auto name = std::string(argv[1]); expected = utf16(argv[2]);
    nullEmpty = std::string(argv[3]) == "null"; kind = argv[4]; gCall4D = host;
    if (name == "array") arrayCase();
    else if (name == "rejections") rejectionCase();
    else if (name == "wrappers") wrapperCase();
    else if (name == "null-controls") {
        CUTF8String output(reinterpret_cast<const uint8_t*>("unchanged"));
        ob_set_s(nullptr, "key", "value");
        ob_set_a(nullptr, L"key", &expected);
        ob_set_p(nullptr, L"key", nullptr);
        check(!ob_get_a(nullptr, L"key", &output) && !ob_get_b(nullptr, L"key") &&
              ob_get_n(nullptr, L"key") == 0 && !ob_get_c(nullptr, L"key"), "null getter defaults");
        ob_stringify(nullptr, &output);
        check(std::string(reinterpret_cast<const char*>(output.data()), output.size()) == "unchanged", "null output unchanged");
        check(propertyReads == 0 && collectionReads == 0 && scans == 0, "null inputs never dispatched");
    }
    else if (name == "values") {
        auto obj = makeRef();
        ob_set_i(obj, L"integer", 42);
        ob_set_b(obj, L"boolean", true);
        ob_set_n(obj, L"wide-real", 1.5);
        ob_set_n(obj, "narrow-real", 2.5);
        ob_set_0(obj, L"wide-null");
        ob_set_0(obj, "narrow-null");
        ob_set_o(obj, L"wide-object", makeRef());
        ob_set_o(obj, "narrow-object", makeRef());
        ob_set_c(obj, L"collection", makeRef(true));
        check(obj->properties.size() == 9, "non-string helper output shape");
        check(obj->properties.at(utf16("integer")).uValue.fLongint == 42 &&
              obj->properties.at(utf16("boolean")).uValue.fBoolean &&
              obj->properties.at(utf16("wide-real")).uValue.fReal == 1.5 &&
              obj->properties.at(utf16("narrow-real")).uValue.fReal == 2.5, "non-string values unchanged");
        check(obj->properties.at(utf16("wide-null")).fType == static_cast<char>(eVK_Null) &&
              obj->properties.at(utf16("narrow-null")).fType == static_cast<char>(eVK_Null), "null values unchanged");
        releaseRef(obj);
    }
    else if (name == "copy-traits") {
        check(!std::is_copy_constructible<C_TEXT>::value && !std::is_copy_assignable<C_TEXT>::value, "C_TEXT noncopy prior art");
        check(!std::is_copy_constructible<ARRAY_TEXT>::value && !std::is_copy_assignable<ARRAY_TEXT>::value, "ARRAY_TEXT must not shallow-copy owner");
        check(!std::is_copy_constructible<C_BLOB>::value && !std::is_copy_assignable<C_BLOB>::value, "C_BLOB must not shallow-copy owner");
    }
#ifdef OWNERSHIP_BASELINE_COPY
    else if (name == "copy-array") { ARRAY_TEXT a; { ARRAY_TEXT b(a); } a.setSize(1); }
    else if (name == "assign-array") { ARRAY_TEXT a; { ARRAY_TEXT b; b = a; } a.setSize(1); }
    else if (name == "copy-blob") { C_BLOB a; { C_BLOB b(a); } a.getBytesLength(); }
    else if (name == "assign-blob") { C_BLOB a; { C_BLOB b; b = a; } a.getBytesLength(); }
#endif
    else if (name == "sdk-failures") {
        failHandle = true; auto h = PA_NewHandle(1);
        check(!h && PA_GetLastError() != eER_NoErr, "SDK allocation failure propagated"); failHandle = false;
        auto array = PA_CreateVariable(eVK_ArrayUnicode); auto h2 = array.uValue.fArray.fData;
        failResize = true; PA_ResizeArray(&array, 2); failResize = false;
        check(array.uValue.fArray.fNbElements == 2 && handles.at(h2) == sizeof(PA_Unistring), "documented SDK failed-resize metadata gap");
        array.uValue.fArray.fNbElements = 0; PA_ClearVariable(&array);
    }
    else if (name == "dispatch" || name == "no-picture" || name == "decode-empty" || name == "decode-throws" || name == "native-null") dispatchCase(name);
    else {
        auto obj = makeRef(); auto text = utf8(expected.data(), expected.size());
        if (name.find("set-") == 0) {
            if (name == "set-narrow") ob_set_s(obj, "key", text.c_str());
            else if (name == "set-wide") ob_set_s(obj, L"key", text.c_str());
            else if (name == "set-picture") ob_set_p(obj, L"key", makePicture());
            else if (name == "set-wchars") {
                std::wstring_convert<std::codecvt_utf8<wchar_t>> codec;
                auto wide = codec.from_bytes(text); ob_set_a(obj, L"key", wide.c_str());
            } else if (name == "set-utf16") ob_set_a(obj, L"key", &expected);
            else check(false, "unknown setter");
            if (kind != "set-error" && name != "set-picture") textEquals(obj->properties.at(utf16("key")).uValue.fString, expected);
        } else if (name == "get-a") {
            CUTF8String output(reinterpret_cast<const uint8_t*>("unchanged"));
            bool defined = ob_get_a(obj, L"key", &output);
            check(defined == (kind != "absent"), "preserve existence result");
            auto wanted = kind == "string" ? text : "unchanged";
            check(std::string(reinterpret_cast<const char*>(output.data()), output.size()) == wanted, "preserve getter output");
        } else if (name == "get-b") check(ob_get_b(obj, L"key") == (kind == "boolean"), "boolean default/match");
        else if (name == "get-n") check(ob_get_n(obj, L"key") == (kind == "real" ? 3.25 : 0), "number default/match");
        else if (name == "get-c") {
            auto value = ob_get_c(obj, L"key");
            check(bool(value) == (kind == "collection"), "collection matching handoff");
            if (value) { check(refs.count(value) && refs.at(value)->owners == 1, "caller owns collection"); PA_DisposeCollection(value); }
        } else if (name == "stringify") {
            if (kind == "error-first-handle") failHandle = true;
            CUTF8String output(reinterpret_cast<const uint8_t*>("unchanged")); ob_stringify(obj, &output);
            auto wanted = kind == "string" ? text : "unchanged";
            check(std::string(reinterpret_cast<const char*>(output.data()), output.size()) == wanted, "stringify output/error preservation");
        } else check(false, "unknown case");
        releaseRef(obj);
    }
    check(strings.empty(), "string owners leaked");
    check(pictures.empty(), "picture owners leaked");
    check(handles.empty(), "handle owners leaked");
    check(refs.empty(), "object/collection owners leaked");
    check(resources.empty() && nativeOwners == 0 && zbar::scanners.empty() && zbar::images.empty(), "platform/decoder owners leaked");
    std::cout << "strings=0 pictures=0 handles=0 references=0 platform=0 decoder=0\n";
}
