/* --------------------------------------------------------------------------------
 #
 #	4DPlugin.cpp
 #	source generated by 4D Plugin Wizard
 #	Project : ZBAR
 #	author : miyako
 #	2016/04/21
 #
 # --------------------------------------------------------------------------------*/


#include "4DPluginAPI.h"
#include "4DPlugin.h"

void PluginMain(PA_long32 selector, PA_PluginParameters params)
{
	try
	{
		PA_long32 pProcNum = selector;
		sLONG_PTR *pResult = (sLONG_PTR *)params->fResult;
		PackagePtr pParams = (PackagePtr)params->fParameters;

		CommandDispatcher(pProcNum, pResult, pParams); 
	}
	catch(...)
	{

	}
}

void CommandDispatcher (PA_long32 pProcNum, sLONG_PTR *pResult, PackagePtr pParams)
{
	switch(pProcNum)
	{
// --- ZBAR

		case 1 :
			ZBAR(pResult, pParams);
			break;

	}
}

// ------------------------------------- ZBAR -------------------------------------


void ZBAR(sLONG_PTR *pResult, PackagePtr pParams)
{
	C_PICTURE Param1;
	ARRAY_TEXT Param2;
	ARRAY_TEXT Param3;
	ARRAY_TEXT Param4;
	ARRAY_LONGINT Param5;
	C_TEXT returnValue;

	Param1.fromParamAtIndex(pParams, 1);

//	PA_Picture gs = Param1.createGrayScale();
	PA_Picture gs = Param1.getPicture();

	//setup
#if VERSIONMAC
	CGImageRef image = (CGImageRef)PA_CreateNativePictureForScreen(gs);
#else
	Gdiplus::Bitmap *image = (Gdiplus::Bitmap *)PA_CreateNativePictureForScreen(gs);
#endif
	
//	PA_DisposePicture(gs);
	
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
			
			CGRect rect = {{0,0},{w,h}};
			
			CGContextDrawImage(ctx, rect, image);
			
			size_t *pixels = (size_t *)CGBitmapContextGetData (ctx);
			
			uint32_t pixel, y8;
			size_t i = 0;
			
			for(size_t y = 0; y < h; y++) {
				PA_YieldAbsolute();
				for(size_t x = 0; x < w; x++) {
					pixel = pixels[y*w+x];
					y8 = (pixel >> 24) & 0xFF;
					buf[i] = y8;
					i++;
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
		
		void (*_PA_YieldAbsolute)(void) = PA_YieldAbsolute;
		
		using namespace zbar;
		
		/* create a reader */
		zbar_image_scanner_t *zScanner = zbar_image_scanner_create();
		
		zbar_image_scanner_set_config(zScanner, ZBAR_NONE, ZBAR_CFG_ENABLE, 0);
		zbar_image_scanner_set_config(zScanner, ZBAR_NONE, ZBAR_CFG_MIN_LEN, 0);
		
		/* configure the reader */
		zbar_image_scanner_set_config(zScanner, ZBAR_EAN13, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_EAN2, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_EAN5, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_EAN8, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_UPCA, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_UPCE, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_ISBN10, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_ISBN13, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_I25, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_DATABAR, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_DATABAR_EXP, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_CODABAR, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_CODE39, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_CODE93, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_CODE128, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_QRCODE, ZBAR_CFG_ENABLE, 1);
		zbar_image_scanner_set_config(zScanner, ZBAR_PDF417, ZBAR_CFG_ENABLE, 1);
		
		/* wrap image data */
		zbar_image_t *zImage = zbar_image_create();
		
		zbar_image_set_format(zImage, zbar_fourcc('Y','8','0','0'));
		zbar_image_set_size(zImage, w, h);
		zbar_image_set_data(zImage, &buf[0], w * h, NULL);
		
		/* scan the image for barcodes */
		unsigned int n = zbar_scan_image(zScanner, zImage, _PA_YieldAbsolute);
		unsigned int count = 0;
		
		if(n){
			
			Param2.setSize(1);
			Param3.setSize(1);
			Param4.setSize(1);
			Param5.setSize(1);
			
			/* extract results */
			const zbar_symbol_t *symbol = zbar_image_first_symbol(zImage);
			
			for(; symbol; symbol = zbar_symbol_next(symbol)) {
				
				(*_PA_YieldAbsolute)();
				
				count++;
				
#define BUFFER_SIZE 1024
				char *xml = (char *)malloc(BUFFER_SIZE);
				unsigned int xmlbuflen = BUFFER_SIZE;
				if(xml)
				{
//					@see http://zbar.sourceforge.net/2008/barcode.xsd
					char *p = zbar_symbol_xml(symbol, &xml, &xmlbuflen);
					printf("%s", p);
					free(p);
				}
				const char *zData = zbar_symbol_get_data(symbol);
				zbar_symbol_type_t symType = zbar_symbol_get_type(symbol);
				
				int confidence = zbar_symbol_get_quality(symbol);
				
				const char *zName = zbar_get_symbol_name(symType);
				const char *zAddon = zbar_get_addon_name(symType);
				
				CUTF8String uData = CUTF8String((const uint8_t *)zData);
				CUTF8String uName = CUTF8String((const uint8_t *)zName);
				CUTF8String uAddon = CUTF8String((const uint8_t *)zAddon);
				
				Param2.appendUTF8String(&uData);
				Param3.appendUTF8String(&uName);
				Param4.appendUTF8String(&uAddon);
				Param5.appendIntValue(confidence);
				
				if(count == 1){
					returnValue.setUTF8String(&uData);
				}
				
			}
			
		}
		
		zbar_image_scanner_destroy(zScanner); zScanner = NULL;
		zbar_image_destroy(zImage); zImage = NULL;
		
//cleanup
#if VERSIONMAC
		CGImageRelease(image); image = NULL;
#else
		delete image; image = NULL;
#endif
	}
	Param2.toParamAtIndex(pParams, 2);
	Param3.toParamAtIndex(pParams, 3);
	Param4.toParamAtIndex(pParams, 4);
	Param5.toParamAtIndex(pParams, 5);
	returnValue.setReturn(pResult);
}
