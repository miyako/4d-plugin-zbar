/* --------------------------------------------------------------------------------
 #
 #  4DPlugin-ZBAR.cpp
 #	source generated by 4D Plugin Wizard
 #	Project : ZBAR
 #	author : miyako
 #	2020/06/03
 #
 # --------------------------------------------------------------------------------*/

#include "4DPlugin-ZBAR.h"

#pragma mark -

void PluginMain(PA_long32 selector, PA_PluginParameters params) {
    
    try
    {
        switch(selector)
        {
                // --- ZBAR
                
            case 1 :
                ZBAR(params);
                break;
                
        }
        
    }
    catch(...)
    {
        
    }
}

#pragma mark -

void ZBAR(PA_PluginParameters params) {
    
    PA_ObjectRef returnValue = PA_CreateObject();
    
    ob_set_b(returnValue, L"success", false);
    
    PA_Picture src = PA_GetPictureParameter(params, 1);
    PA_CollectionRef types = PA_GetCollectionParameter(params, 2);
	PA_CollectionRef values = NULL;

    if(src) {
        
#if VERSIONMAC
        CGImageRef image = (CGImageRef)PA_CreateNativePictureForScreen(src);
#else
        Gdiplus::Bitmap *image = (Gdiplus::Bitmap *)PA_CreateNativePictureForScreen(src);
#endif
        
        if(image){
            
#if VERSIONMAC
            
            size_t w = CGImageGetWidth(image);
            size_t h = CGImageGetHeight(image);
            
            size_t size = w * h;
            std::vector<uint8_t> buf(size);
            
            CGContextRef ctx = NULL;
            CGColorSpaceRef colorSpace = NULL;
            
            size_t bitmapBytesPerRow   = (w * 4);
            size_t bitmapByteCount     = (bitmapBytesPerRow * h);
            
            std::vector<uint8_t> bitmapData(bitmapByteCount);
            
            colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
            
            if (colorSpace){
                
                ctx = CGBitmapContextCreate ((void *)&bitmapData[0],
                                             w,
                                             h,
                                             8,      // bits per component
                                             bitmapBytesPerRow,
                                             colorSpace,
                                             kCGImageAlphaPremultipliedFirst);
                
                CGColorSpaceRelease(colorSpace);
                
            }
            
            if (ctx){
                
                CGRect rect = {{0,0},{static_cast<CGFloat>(w),static_cast<CGFloat>(h)}};
                
                CGContextDrawImage(ctx, rect, image);
                
                size_t *pixels = (size_t *)CGBitmapContextGetData (ctx);
                
                size_t pixel, y8;
                size_t i = 0;
#if __LP64__
                BOOL alt = false;
                size_t j = 0;
#endif
                for(size_t y = 0; y < h; y++) {
                    for(size_t x = 0; x < w; x++) {
#if __LP64__
                        if(!alt)
                        {
                            pixel = pixels[j];
                            y8 = (pixel >> 24) & 0xFF;
                        }
                        else
                        {
                            pixel = pixels[j];
                            y8 = (pixel >> 56) & 0xFF;
                            j++;
                        }
                        buf[i] = y8;
                        i++;
                        alt = !alt;
#else
                        pixel = pixels[i];
                        y8 = (pixel >> 24) & 0xFF;
                        buf[i] = y8;
                        i++;
#endif
                    }
                }
                CGContextRelease(ctx);
            }
            
#else
            size_t w = image->GetWidth();
            size_t h = image->GetHeight();
            
            size_t size = w * h;
            std::vector<uint8_t> buf(size);
            
            uint32_t y8;
            size_t i = 0;
            
            for(size_t y = 0; y < h; y++) {
                PA_YieldAbsolute();
                for(size_t x = 0; x < w; x++) {
                    
                    Gdiplus::Color c;
                    image->GetPixel(x,y,&c);
                    
                    y8 = c.GetR();
                    buf[i] = y8;
                    i++;
                }
            }
#endif
            
//            void (*_PA_YieldAbsolute)(void) = PA_YieldAbsolute;
            
            using namespace zbar;
            
            /* create a reader */
            zbar_image_scanner_t *zScanner = zbar_image_scanner_create();
            
            if(types) {
                
                PA_long32 countTypes = PA_GetCollectionLength(types);
                if(countTypes > 0) {
                    zbar_image_scanner_set_config(zScanner, ZBAR_NONE, ZBAR_CFG_ENABLE, 0);
                    for(PA_long32 i = 0; i < countTypes; ++i) {
                        PA_Variable v = PA_GetCollectionElement(types, i);
                        if(PA_GetVariableKind(v) == eVK_Real) {
							zbar_symbol_type_t type = (zbar_symbol_type_t)(int)PA_GetRealVariable(v);
                            zbar_image_scanner_set_config(zScanner, type, ZBAR_CFG_ENABLE, 1);
                        }
                    }
                }
            }

            /* wrap image data */
            zbar_image_t *zImage = zbar_image_create();
            
            zbar_image_set_format(zImage, zbar_fourcc('Y','8','0','0'));
            zbar_image_set_size(zImage, w, h);
            zbar_image_set_data(zImage, &buf[0], w * h, NULL);
            
            /* scan the image for barcodes */
//            unsigned int n = zbar_scan_image(zScanner, zImage, _PA_YieldAbsolute, 0x2000);
            unsigned int n = zbar_scan_image(zScanner, zImage);
            unsigned int count = 0;
            
            if(n) {

                /* extract results */
                const zbar_symbol_t *symbol = zbar_image_first_symbol(zImage);
                
                bool found = false;
                
                for(; symbol; symbol = zbar_symbol_next(symbol)) {
                                                            
                    const char *zData = zbar_symbol_get_data(symbol);
                    zbar_symbol_type_t symType = zbar_symbol_get_type(symbol);
                    
                    int confidence = zbar_symbol_get_quality(symbol);
                    
                    const char *zName = zbar_get_symbol_name(symType);
                    const char *zAddon = zbar_get_addon_name(symType);
                    
                    CUTF8String uData = CUTF8String((const uint8_t *)zData);
                    CUTF8String uName = CUTF8String((const uint8_t *)zName);
                    CUTF8String uAddon = CUTF8String((const uint8_t *)zAddon);
                    
                    PA_ObjectRef barObj = PA_CreateObject();
                    
                    ob_set_s(barObj, "data", (const char *)uData.c_str());
                    ob_set_s(barObj, "name", (const char *)uName.c_str());
                    ob_set_s(barObj, "addOn", (const char *)uAddon.c_str());
                    ob_set_n(barObj, L"confidence", confidence);
                    
                    if(!found) {
                        ob_set_b(returnValue, L"success", true);
						values = PA_CreateCollection();
						found = true;
                    }

					PA_Variable v = PA_CreateVariable(eVK_ArrayObject);
					PA_SetObjectVariable(&v, barObj);
					PA_SetCollectionElement(values, PA_GetCollectionLength(values), v);
					PA_ClearVariable(&v);
                    
                }
                
            }
            
            zbar_image_scanner_destroy(zScanner); zScanner = NULL;
            zbar_image_destroy(zImage); zImage = NULL;

#if VERSIONMAC
            CGImageRelease(image); image = NULL;
#else
            delete image; image = NULL;
#endif
        }
        
    }

	if (values) {
		ob_set_c(returnValue, L"values", values);
	}
    
    PA_ReturnObject(params, returnValue);
}

