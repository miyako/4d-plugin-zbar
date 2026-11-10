#define NOMINMAX
#include <windows.h>
#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>
#include <zbar.h>
#ifndef LEGACY_ZBAR
#include <iconv.h>
#include "..\..\windows\scanner.h"
#endif
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace zbar;
#ifndef LEGACY_ZBAR
static_assert(std::is_same<decltype(&zbar_scan_image),
    int (*)(zbar_image_scanner_t*, zbar_image_t*)>::value,
    "The scanner must use the standard two-argument ABI");
#endif

struct Picture {
    unsigned width, height;
    std::vector<unsigned char> pixels;
};

static void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

static Picture load(const std::wstring& path)
{
    Gdiplus::Bitmap bitmap(path.c_str());
    require(bitmap.GetLastStatus() == Gdiplus::Ok, "Cannot load fixture");
    Picture picture{bitmap.GetWidth(), bitmap.GetHeight(), {}};
    picture.pixels.resize(static_cast<size_t>(picture.width) * picture.height);
    for (unsigned y = 0; y < picture.height; ++y) {
        for (unsigned x = 0; x < picture.width; ++x) {
            Gdiplus::Color color;
            require(bitmap.GetPixel(x, y, &color) == Gdiplus::Ok, "GetPixel failed");
            picture.pixels[static_cast<size_t>(y) * picture.width + x] = color.GetR();
        }
    }
    return picture;
}

struct Result {
    zbar_symbol_type_t type;
    std::string data;
    bool operator==(const Result& other) const
    {
        return type == other.type && data == other.data;
    }
};

static std::vector<Result> scan(const Picture& picture,
    const std::vector<zbar_symbol_type_t>& filters, bool* retried = nullptr,
    bool retry = true)
{
    std::unique_ptr<zbar_image_scanner_t, decltype(&zbar_image_scanner_destroy)>
        scanner(zbar_image_scanner_create(), zbar_image_scanner_destroy);
    std::unique_ptr<zbar_image_t, decltype(&zbar_image_destroy)>
        image(zbar_image_create(), zbar_image_destroy);
    require(scanner && image, "Scanner allocation failed");
    if (!filters.empty()) {
        require(!zbar_image_scanner_set_config(scanner.get(), ZBAR_NONE, ZBAR_CFG_ENABLE, 0),
            "Cannot disable symbologies");
        for (auto type : filters) {
            const int status = zbar_image_scanner_set_config(scanner.get(), type,
                ZBAR_CFG_ENABLE, 1);
#ifndef LEGACY_ZBAR
            if (type == ZBAR_PDF417)
                require(status != 0, "Incomplete PDF417 decoder must be disabled at build time");
            else
#endif
                require(status == 0, "Cannot enable requested symbology");
        }
    }
    zbar_image_set_format(image.get(), zbar_fourcc('Y', '8', '0', '0'));
    zbar_image_set_size(image.get(), picture.width, picture.height);
    zbar_image_set_data(image.get(), picture.pixels.data(),
        static_cast<unsigned long>(picture.pixels.size()), nullptr);
#ifdef LEGACY_ZBAR
    const int count = zbar_scan_image(scanner.get(), image.get(), []() {}, 0x2000);
    if (retried) *retried = false;
#else
    const int count = retry ? windows_zbar::scanImage(scanner.get(), image.get()) :
        zbar_scan_image(scanner.get(), image.get());
    if (retried) *retried = zbar_image_get_data(image.get()) != picture.pixels.data();
#endif
    require(count >= 0, "Scanner returned an error");
    std::vector<Result> results;
    for (const auto* symbol = zbar_image_first_symbol(image.get()); symbol;
         symbol = zbar_symbol_next(symbol)) {
        Result result{zbar_symbol_get_type(symbol),
            std::string(zbar_symbol_get_data(symbol), zbar_symbol_get_data_length(symbol))};
        require(!result.data.empty(), "Decoded empty payload");
        require(zbar_get_symbol_name(result.type)[0] != '\0', "Missing symbol name");
        require(zbar_get_addon_name(result.type) != nullptr, "Missing add-on name");
        require(zbar_symbol_get_quality(symbol) >= 0, "Invalid quality");
        results.push_back(result);
    }
    require(results.size() == static_cast<size_t>(count), "Result count mismatch");
    return results;
}

static std::vector<Result> scan(const Picture& picture, zbar_symbol_type_t only = ZBAR_NONE)
{
    return scan(picture, only == ZBAR_NONE ? std::vector<zbar_symbol_type_t>{} :
        std::vector<zbar_symbol_type_t>{only});
}

#ifndef LEGACY_ZBAR
static void checkFallback(const Picture& qr)
{
    const auto map = windows_zbar::createRetryMap();
    require(map.front() <= 2 && map.back() >= 254 && map[128] < 128,
        "Retry must use a nonlinear, darkening intensity map");
    for (size_t i = 1; i < map.size(); ++i)
        require(map[i] >= map[i - 1], "Retry intensity map must be monotonic");
    {
        struct Apartment {
            HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
        } apartment;
        require(SUCCEEDED(apartment.result), "Cannot initialize MTA test");
        require(windows_zbar::createRetryMap() == map,
            "WIC retry map changed in an existing MTA apartment");
    }
    unsigned preserved = 0, convertedMisses = 0;
    for (unsigned background : {0u, 8u, 16u, 32u, 64u, 100u, 160u, 220u}) {
        for (unsigned range : {255u, 40u, 20u, 16u, 12u, 8u}) {
            if (background + range > 255) continue;
            Picture low = qr, converted = qr;
            for (size_t i = 0; i < qr.pixels.size(); ++i) {
                low.pixels[i] = static_cast<unsigned char>(
                    background + qr.pixels[i] * range / 255);
                converted.pixels[i] = map[low.pixels[i]];
            }
            for (const auto& filter : {std::vector<zbar_symbol_type_t>{},
                                      std::vector<zbar_symbol_type_t>{ZBAR_QRCODE}}) {
                const auto baseline = scan(low, filter, nullptr, false);
                require(baseline.size() == 1 && baseline[0].data == "abcd",
                    "Low-contrast baseline fixture must decode");
                bool retried = true;
                require(scan(low, filter, &retried) == baseline && !retried,
                    "Retry changed an existing low-contrast success");
                ++preserved;
                convertedMisses += scan(converted, filter, nullptr, false).empty();
            }
        }
    }
    require(preserved == 80 && convertedMisses > 0,
        "Low-contrast regression controls did not exercise conversion losses");

    Picture rescued{};
    bool found = false;
    for (unsigned background : {128u, 160u, 192u, 220u, 240u}) {
        for (unsigned range = 2; range <= 8 && !found; ++range) {
            Picture candidate = qr;
            for (auto& pixel : candidate.pixels)
                pixel = static_cast<unsigned char>(background + pixel * range / 255);
            if (!scan(candidate, std::vector<zbar_symbol_type_t>{ZBAR_QRCODE},
                      nullptr, false).empty()) continue;
            bool retried = false;
            const auto result = scan(candidate,
                std::vector<zbar_symbol_type_t>{ZBAR_QRCODE}, &retried);
            if (result.size() == 1 && result[0].data == "abcd" && retried) {
                rescued = std::move(candidate);
                found = true;
            }
        }
    }
    require(found, "Public synthetic QR did not exercise successful retry");
    const auto expected = scan(qr, ZBAR_QRCODE);
    for (int repeat = 0; repeat < 3; ++repeat) {
        bool retried = false;
        require(scan(rescued, std::vector<zbar_symbol_type_t>{}, &retried) == expected &&
            retried, "Default retry did not recover exact QR payload");
        require(scan(rescued, std::vector<zbar_symbol_type_t>{ZBAR_PDF417, ZBAR_QRCODE}) ==
            expected, "Mixed filter lost QR during retry");
    }
    require(scan(rescued, ZBAR_CODE128).empty(), "Retry ignored QR exclusion");
    require(scan(rescued, ZBAR_PDF417).empty(), "Retry enabled default decoders");

    std::unique_ptr<zbar_image_scanner_t, decltype(&zbar_image_scanner_destroy)>
        scanner(zbar_image_scanner_create(), zbar_image_scanner_destroy);
    std::unique_ptr<zbar_image_t, decltype(&zbar_image_destroy)>
        image(zbar_image_create(), zbar_image_destroy);
    require(scanner && image, "Negative-return test allocation failed");
    const unsigned char pixel = 0;
    zbar_image_set_format(image.get(), zbar_fourcc('B', 'A', 'D', '!'));
    zbar_image_set_size(image.get(), 1, 1);
    zbar_image_set_data(image.get(), &pixel, 1, nullptr);
    require(windows_zbar::scanImage(scanner.get(), image.get()) < 0 &&
        zbar_image_get_data(image.get()) == &pixel,
        "Negative scan return incorrectly triggered a retry");
    std::cout << "WIC retry, exclusion, error, and 80 low-contrast controls passed\n";
}

static void checkEncoding()
{
    iconv_t converter = iconv_open("UTF-8", "SJIS");
    require(converter != reinterpret_cast<iconv_t>(-1), "SJIS converter unavailable");
    char bytes[] = "\x93\xfa\x96\x7b";
    char output[16] = {};
    char* input = bytes;
    char* destination = output;
    size_t inputSize = 4, outputSize = sizeof(output);
    const size_t status = iconv(converter, &input, &inputSize, &destination, &outputSize);
    iconv_close(converter);
    require(status != static_cast<size_t>(-1) && inputSize == 0 &&
        std::string(output) == "\xe6\x97\xa5\xe6\x9c\xac",
        "QR text conversion must preserve Japanese characters");
}
#endif

int wmain(int argc, wchar_t** argv)
{
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) {
        std::cerr << "GDI+ startup failed\n";
        return 1;
    }
    int exitCode = 0;
    try {
        require(argc == 3, "Usage: scanner-test <test/Resources> <tests/windows/resources>");
        unsigned major = 0, minor = 0, patch = 0;
#ifdef LEGACY_ZBAR
        zbar_version(&major, &minor);
        std::cout << "Legacy library reports version " << major << '.' << minor << '\n';
#else
        zbar_version(&major, &minor, &patch);
        require(major == 0 && minor == 23 && patch == 95, "Wrong linked zbar version");
        checkEncoding();
#endif
        std::unique_ptr<zbar_decoder_t, decltype(&zbar_decoder_destroy)>
            decoder(zbar_decoder_create(), zbar_decoder_destroy);
        require(decoder != nullptr, "Cannot create decoder");
        const zbar_symbol_type_t types[] = {ZBAR_EAN2, ZBAR_EAN5, ZBAR_EAN8,
            ZBAR_UPCE, ZBAR_ISBN10, ZBAR_UPCA, ZBAR_EAN13, ZBAR_ISBN13,
            ZBAR_I25, ZBAR_DATABAR, ZBAR_DATABAR_EXP, ZBAR_CODABAR,
            ZBAR_CODE39, ZBAR_PDF417, ZBAR_QRCODE, ZBAR_CODE93, ZBAR_CODE128};
        const unsigned defaults[] = {0, 0, 5, 4, 4, 4, 5, 4, 1, 5, 5, 1, 1, 0, 1, 1, 1};
        for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
            const auto type = types[i];
#ifdef LEGACY_ZBAR
            const unsigned expected = type == ZBAR_PDF417 ? 1 : defaults[i];
#else
            const unsigned expected = defaults[i];
#endif
            require(zbar_decoder_get_configs(decoder.get(), type) == expected,
                "Pinned decoder defaults changed");
            std::cout << "default " << type << ": "
                << zbar_decoder_get_configs(decoder.get(), type) << '\n';
        }
        const std::wstring root = std::wstring(argv[1]) + L"/";
        struct Fixture { const wchar_t* file; zbar_symbol_type_t type; const char* data; };
        const Fixture fixtures[] = {
            {L"qrcode.png", ZBAR_QRCODE, "abcd"},
            {L"code128.png", ZBAR_CODE128, "124222"},
            {L"code93.png", ZBAR_CODE93, "abcd"},
            {L"ean8.png", ZBAR_EAN8, "01242225"},
            {L"databar.png", ZBAR_DATABAR, "0100075678164125"},
            {L"codabar.png", ZBAR_CODABAR, "A31117013206375B"},
            {L"i25.png", ZBAR_I25, "124222"},
            {L"upca.png", ZBAR_EAN13, "0000000012348"},
            {L"upce.png", ZBAR_UPCE, "00012344"},
            {L"isbn10.png", ZBAR_EAN13, "9780000124227"}
        };
        for (const auto& fixture : fixtures) {
            const auto picture = load(root + fixture.file);
            bool retried = true;
            const auto results = scan(picture,
                std::vector<zbar_symbol_type_t>{fixture.type}, &retried);
            require(!retried, "Existing successful fixture must not be retried");
            require(results.size() == 1 && results[0].type == fixture.type &&
                results[0].data == fixture.data,
                "Fixture did not decode as the requested type");
            std::cout << "type " << fixture.type << ": " << results[0].data << '\n';
            for (int repeat = 0; repeat < 3; ++repeat)
                require(scan(picture, fixture.type) == results, "Repeat scan changed payload");
            require(!scan(picture).empty(), "Default symbologies failed to decode fixture");
        }
        require(scan(load(root + L"code128.png"), ZBAR_QRCODE).empty(),
            "Symbology exclusion failed");
        require(scan(Picture{1024, 1024, std::vector<unsigned char>(1024 * 1024, 255)}).empty(),
            "Blank image produced a symbol");
        require(scan(load(root + L"pdf417.png")).empty(), "Unexpected PDF417 support");
        require(scan(load(root + L"pdf417.jpg")).empty(), "Unexpected PDF417 support");

        const std::wstring extra = std::wstring(argv[2]) + L"/";
        const std::pair<const wchar_t*, const char*> textFixtures[] = {
            {L"qr-utf8.png", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e caf\xc3\xa9"},
            {L"qr-sjis.png", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"}
        };
        for (const auto& fixture : textFixtures) {
            const auto results = scan(load(extra + fixture.first), ZBAR_QRCODE);
            require(results.size() == 1 && results[0].data == fixture.second,
                "Non-ASCII QR payload did not decode to expected UTF-8 bytes");
        }
        std::cout << "UTF-8 and Shift-JIS QR golden payloads passed\n";

        const auto qr = load(root + L"qrcode.png");
#ifndef LEGACY_ZBAR
        checkFallback(qr);
#endif
        const auto expected = scan(qr, ZBAR_QRCODE);
        require(scan(qr, ZBAR_PDF417).empty(), "PDF417-only filter enabled QR implicitly");
        require(scan(qr, std::vector<zbar_symbol_type_t>{ZBAR_PDF417, ZBAR_QRCODE}) == expected,
            "Mixed unsupported filter lost QR");
        const auto upca = load(root + L"upca.png");
        const auto isbn = load(root + L"isbn10.png");
        require(scan(upca, ZBAR_UPCA).empty(), "UPC-A-only filter behavior changed");
        require(scan(isbn, ZBAR_ISBN10).empty(), "ISBN-10-only filter behavior changed");
        const auto upcaResult = scan(upca, std::vector<zbar_symbol_type_t>{ZBAR_EAN13, ZBAR_UPCA});
        const auto isbnResult = scan(isbn, std::vector<zbar_symbol_type_t>{ZBAR_EAN13, ZBAR_ISBN10});
        require(upcaResult.size() == 1 && upcaResult[0].type == ZBAR_UPCA &&
            upcaResult[0].data == "000000012348", "UPC-A derived decoding changed");
        require(isbnResult.size() == 1 && isbnResult[0].type == ZBAR_ISBN10 &&
            isbnResult[0].data == "0000124222", "ISBN-10 derived decoding changed");
        const unsigned side = 8192, scale = 4;
        require(qr.width * scale < side && qr.height * scale < side, "QR fixture too large");
        Picture large{side, side, std::vector<unsigned char>(static_cast<size_t>(side) * side, 255)};
        const unsigned left = (side - qr.width * scale) / 2;
        const unsigned top = (side - qr.height * scale) / 2;
        for (unsigned y = 0; y < qr.height * scale; ++y)
            for (unsigned x = 0; x < qr.width * scale; ++x)
                large.pixels[static_cast<size_t>(top + y) * side + left + x] =
                    qr.pixels[static_cast<size_t>(y / scale) * qr.width + x / scale];
        const auto start = std::chrono::steady_clock::now();
        require(scan(large) == expected, "8192x8192 decode changed QR payload");
        std::cout << "8192x8192 scan: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count() << " ms\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        exitCode = 1;
    }
    Gdiplus::GdiplusShutdown(token);
    return exitCode;
}
