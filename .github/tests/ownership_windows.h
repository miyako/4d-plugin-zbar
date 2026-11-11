// Exercise actual conditional consumers with synthetic UTF-16/platform types.
// This is not a Windows SDK, native ABI validation, WIC test or decoder test.
#include "4DPlugin-ZBAR.h"
#include "ARRAY_TEXT.h"
#include "C_BLOB.h"
#include "C_LONGINT.h"
#include <limits>
#undef VERSIONMAC
#undef VERSIONWIN
#define VERSIONMAC 0
#define VERSIONWIN 1
#define _WIN32 1
#define ZBAR_WINDOWS_SCANNER_H
using LPCWSTR = const PA_Unichar*;
using LPWSTR = PA_Unichar*;
using LPCSTR = const char*;
using LPSTR = char*;
enum { CP_UTF8 = 65001 };
int MultiByteToWideChar(unsigned, unsigned, LPCSTR, int, LPWSTR, int);
int WideCharToMultiByte(unsigned, unsigned, LPCWSTR, int, LPSTR, int, LPCSTR, BOOL*);
inline int sprintf_s(char* dst, size_t size, const char* format, unsigned value)
{ return std::snprintf(dst, size, format, value); }
inline void OutputDebugStringA(const char*) {}
namespace Gdiplus {
enum Status { Ok };
class Color { public: unsigned char GetR() const { return 255; } };
class Bitmap {
public:
    Status GetLastStatus() const { return Ok; }
    unsigned GetWidth() const { return 2; }
    unsigned GetHeight() const { return 2; }
    Status GetPixel(size_t, size_t, Color*) const { return Ok; }
    ~Bitmap();
};
}
namespace windows_zbar {
inline int scanImage(zbar::zbar_image_scanner_t* scanner, zbar::zbar_image_t* image)
{ return zbar::zbar_scan_image(scanner, image); }
}
