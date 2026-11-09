#define NOMINMAX
#include <windows.h>
#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>
#include <zbar.h>
#include <iconv.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace zbar;
static_assert(std::is_same<decltype(&zbar_scan_image),
    int (*)(zbar_image_scanner_t*, zbar_image_t*)>::value,
    "The scanner must use the standard two-argument ABI");

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

static std::vector<Result> scan(const Picture& picture, zbar_symbol_type_t only = ZBAR_NONE)
{
    std::unique_ptr<zbar_image_scanner_t, decltype(&zbar_image_scanner_destroy)>
        scanner(zbar_image_scanner_create(), zbar_image_scanner_destroy);
    std::unique_ptr<zbar_image_t, decltype(&zbar_image_destroy)>
        image(zbar_image_create(), zbar_image_destroy);
    require(scanner && image, "Scanner allocation failed");
    if (only != ZBAR_NONE) {
        require(!zbar_image_scanner_set_config(scanner.get(), ZBAR_NONE, ZBAR_CFG_ENABLE, 0),
            "Cannot disable symbologies");
        require(!zbar_image_scanner_set_config(scanner.get(), only, ZBAR_CFG_ENABLE, 1),
            "Cannot enable requested symbology");
    }
    zbar_image_set_format(image.get(), zbar_fourcc('Y', '8', '0', '0'));
    zbar_image_set_size(image.get(), picture.width, picture.height);
    zbar_image_set_data(image.get(), picture.pixels.data(),
        static_cast<unsigned long>(picture.pixels.size()), nullptr);
    const int count = zbar_scan_image(scanner.get(), image.get());
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
        require(argc == 2, "Usage: scanner-test <test/Resources>");
        unsigned major = 0, minor = 0, patch = 0;
        zbar_version(&major, &minor, &patch);
        require(major == 0 && minor == 23 && patch == 95, "Wrong linked zbar version");
        checkEncoding();
        const std::wstring root = std::wstring(argv[1]) + L"/";
        struct Fixture { const wchar_t* file; zbar_symbol_type_t type; };
        const Fixture fixtures[] = {
            {L"qrcode.png", ZBAR_QRCODE}, {L"code128.png", ZBAR_CODE128},
            {L"code93.png", ZBAR_CODE93}, {L"ean8.png", ZBAR_EAN8},
            {L"databar.png", ZBAR_DATABAR}, {L"codabar.png", ZBAR_CODABAR},
            {L"i25.png", ZBAR_I25}, {L"upca.png", ZBAR_UPCA},
            {L"upce.png", ZBAR_UPCE}, {L"isbn10.png", ZBAR_ISBN10}
        };
        for (const auto& fixture : fixtures) {
            const auto picture = load(root + fixture.file);
            const auto results = scan(picture, fixture.type);
            require(results.size() == 1 && results[0].type == fixture.type,
                "Fixture did not decode as the requested type");
            std::wcout << fixture.file << L": ";
            std::cout << results[0].data << '\n';
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

        const auto qr = load(root + L"qrcode.png");
        const auto expected = scan(qr, ZBAR_QRCODE);
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
