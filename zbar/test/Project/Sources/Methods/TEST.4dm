//%attributes = {}
$path:=Get 4D folder:C485(Current resources folder:K5:16)

DOCUMENT LIST:C474($path;$paths;Ignore invisible:K24:16 | Absolute path:K24:14)

C_OBJECT:C1216($result)

For ($i;1;Size of array:C274($paths))
	
	$path:=$paths{$i}
	
	Case of 
		: ($path="@pdf417@")
			
			  //pdf417 is not working
			
		Else 
			
			READ PICTURE FILE:C678($path;$image)
			
			$result:=ZBAR ($image)
			
			If ($result.success=False:C215)
				
				TRACE:C157  //resolution issue?
				
			End if 
			
	End case 
	
End for 