#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#include <codecvt>
#include <cstring>
#include <locale>
#include <map>
#include <set>
#include <string>
#include <vector>

// Text conversion and every graphics/path boundary are synthetic, in memory.
CFStringRef OwnershipStringBytes(CFAllocatorRef, const UInt8*, CFIndex, CFStringEncoding, Boolean);
CFStringRef OwnershipStringCharacters(CFAllocatorRef, const UniChar*, CFIndex);
CFIndex OwnershipStringLength(CFStringRef);
void OwnershipStringCharactersAt(CFStringRef, CFRange, UniChar*);
CFIndex OwnershipStringCapacity(CFIndex, CFStringEncoding);
CFIndex OwnershipStringGetBytes(CFStringRef, CFRange, CFStringEncoding, UInt8, Boolean,
                              UInt8*, CFIndex, CFIndex*);
void OwnershipRelease(CFTypeRef);
CFURLRef OwnershipURLCreate(CFAllocatorRef, CFStringRef, CFURLPathStyle, Boolean);
CFStringRef OwnershipURLPath(CFURLRef, CFURLPathStyle);
size_t OwnershipImageWidth(CGImageRef);
size_t OwnershipImageHeight(CGImageRef);
CGColorSpaceRef OwnershipColorCreate(CFStringRef);
void OwnershipColorRelease(CGColorSpaceRef);
CGContextRef OwnershipContextCreate(void*, size_t, size_t, size_t, size_t, CGColorSpaceRef, uint32_t);
void OwnershipDraw(CGContextRef, CGRect, CGImageRef);
void* OwnershipContextData(CGContextRef);
void OwnershipContextRelease(CGContextRef);
void OwnershipImageRelease(CGImageRef);
void OwnershipTransform(CGContextRef, CGFloat, CGFloat);
@interface OwnershipURL : NSObject
- (id)initWithString:(id)value;
- (id)absoluteString;
@end
#define CFStringCreateWithBytes OwnershipStringBytes
#define CFStringCreateWithCharacters OwnershipStringCharacters
#define CFStringGetLength OwnershipStringLength
#define CFStringGetCharacters OwnershipStringCharactersAt
#define CFStringGetMaximumSizeForEncoding OwnershipStringCapacity
#define CFStringGetBytes OwnershipStringGetBytes
#define CFRelease OwnershipRelease
#define CFURLCreateWithFileSystemPath OwnershipURLCreate
#define CFURLCopyFileSystemPath OwnershipURLPath
#define NSURL OwnershipURL
#define CGImageGetWidth OwnershipImageWidth
#define CGImageGetHeight OwnershipImageHeight
#define CGColorSpaceCreateWithName OwnershipColorCreate
#define CGColorSpaceRelease OwnershipColorRelease
#define CGBitmapContextCreate OwnershipContextCreate
#define CGContextDrawImage OwnershipDraw
#define CGBitmapContextGetData OwnershipContextData
#define CGContextRelease OwnershipContextRelease
#define CGImageRelease OwnershipImageRelease
#define CGContextScaleCTM OwnershipTransform
#define CGContextTranslateCTM OwnershipTransform
