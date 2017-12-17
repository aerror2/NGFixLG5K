NvidiaGraphicsFixup
===================

An open source kernel extension providing patches for NVidia GPUs.

#### Features
-Fixes LG Ultrafine 5K + gtx1080(TI) blackscreen problem with only one cable -- by aerror 2017-12-17

see https://www.tonymacx86.com/threads/nvidia-gtx1080-lg-ultrafine-5k-full-5k-60hz-success.240730/#post-1651508


I make two patches:
1.   NVDA::validateDetailedTiming, only return success when resolution is 3840x2160. It make LG ultrafine 5K can be light up with one cable. (Without this patch  LG ultrafine 5K just stay black always, no matter how I try). But it works flawly, In most of time screen show up for few seconds, then turn black again...
2.  setStartupDisplayMode just sleep for 10 seconds, It get more stable, will not turn black so much, I got 80% to light up the screen without problem with one cable(3840x2160). I make it sleep for 10 seconds, because I noticed that the screen turn black after 4-5 seconds. But it seems unreasonable, and very ugly patch, very very ugly, it cause the system boot up very slowly , takes a few minutes, and any display settings changing operation is very very slow.  Anyone can give me tips on how to patch it more nicely?

With above two patches, I still get black screen when I plug two cables( I let the 5120x2880 past in validateDetailedTiming).
So, any tips ? Help Please.

Patch codes attached
Patch 1:
[code]

IOReturn NGFX::nvda_validateDetailedTiming_patch(IONDRVFramebuffer *that, void* _desc, IOByteCount descripSize)
{
if(callbackNGFX && callbackNGFX->org_nda_validateDetailedTiming)
{

//kIOReturnSuccess
if (descripSize == sizeof(IOFBDisplayModeDescription))
{
IOFBDisplayModeDescription * fbdesc = (IOFBDisplayModeDescription *) _desc;


DBGLOG("ngfx", "validateDetailedTiming %llx info %u x %u  @ %u.%u hz, maxDepthIndex: %u",(void*)that,
fbdesc->info.nominalWidth,
fbdesc->info.nominalHeight,
(fbdesc->info.refreshRate>>16)&0xffff,
fbdesc->info.refreshRate&0xffff,
fbdesc->info.maxDepthIndex

);

if(
isValidTime(fbdesc->timingInfo)
)
{
logTimingInfoV2(that, "validateDetailedTiming isValidTiming", fbdesc->timingInfo.detailedInfo.v2);
}
else
{
logTimingInfoV2(that, "validateDetailedTiming notvalidTiming", fbdesc->timingInfo.detailedInfo.v2);
return kIOReturnUnsupported;
}


}
else if(sizeof(IODetailedTimingInformationV2)==descripSize)
{
IODetailedTimingInformationV2 *pifov2 =(IODetailedTimingInformationV2 *) _desc;

if(
isValidTimev2(*pifov2)
)
{
logTimingInfoV2(that, "validateDetailedTiming V2 isValidTiming", *pifov2);
}
else
{
logTimingInfoV2(that, "validateDetailedTiming V2 notvalidTiming",  *pifov2);
return kIOReturnUnsupported;
}

}
else
{
DBGLOG("ngfx", "validateDetailedTiming %llx called size %llu unknown  ", (void*)that, descripSize);

}



IOReturn ret=  callbackNGFX->org_nda_validateDetailedTiming(that,_desc, descripSize);
DBGLOG("ngfx", "validateDetailedTiming  %llx  org_nda_validateDetailedTiming return %x", (void*)that, ret);
return ret;


}

DBGLOG("ngfx", "validateDetailedTimin not install ");

return kIOReturnUnsupported;
}

bool isValidTimev2(IODetailedTimingInformationV2        &v2)

{
if (
(v2.horizontalActive == 1920  && v2.verticalActive == 1080)
||
(v2.horizontalActive == 3840  && v2.verticalActive == 2160)
||
(v2.horizontalActive == 1080  && v2.verticalActive == 1920)

)
{
return true;
}

return false;
}
bool isValidTime(IOTimingInformation         &timingInfo)
{
if (
isValidTimev2(timingInfo.detailedInfo.v2)
)
{
return true;
}

return false;
}

[/code]



Patch 2:
[code]
IOReturn NGFX::setStartupDisplayMode (IONDRVFramebuffer *that, IODisplayModeID displayMode, IOIndex depth )
{
if (callbackNGFX && callbackNGFX->org_setStartupDisplayMode)
{

DBGLOG("ngfx", "setStartupDisplayMode %llx displayMode %u depth %u called",that,  displayMode, depth);

IOSleep(10000); // 80% lightup
//do not call the orinal setStartupDisplayMode, just return kIOReturnSuccess
//
return kIOReturnSuccess;
}

return kIOReturnSuccess;
}
[/code]


I have found a perfect solution. there is a few kConnectionHandleDisplayPortEvent  attribute settings for this display, such as , kIODPEventStart, kIODPEventIdle and kIODPEventContentProtection. Althought I still not know it trigger by who, But I can get screen not black any more by skipping the kIODPEventIdle.  It works fine and fastly.  Patch 2 is no longer needed.

Patch3:
[code]

IOReturn NGFX::NVDA_setAttributeForConnection(IONDRVFramebuffer *that , IOIndex connectIndex,
IOSelect attribute, uintptr_t value )
{
switch(attribute)
{
case kConnectionHandleDisplayPortEvent:
{

if(kIODPEventIdle==evt)
{
return kIOReturnSuccess;
}

}
break;
}

}

IOReturn ret = callbackNGFX->org_NVDA_setAttributeForConnection(that, connectIndex,attribute,value);
DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s ret %x ",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
return ret;

}
[/code]

patch:
1. allow only 1920x1080  and 3820x2160 pa

- Fixes an issue in AppleGraphicsDevicePolicy.kext so that we could use a MacPro6,1 board-id/model combination, 
  without the usual hang with a black screen. 
  [Patching AppleGraphicsDevicePolicy.kext](https://pikeralpha.wordpress.com/2015/11/23/patching-applegraphicsdevicepolicy-kext)
- Modifies macOS to recognize NVIDIA's web drivers as platform binaries. This resolves the issue with transparent windows without content,
  which appear for applications that use Metal and have Library Validation enabled. Common affected applications are iBooks and Little Snitch Network Monitor,
  though this patch is universal and fixes them all.
  [NVWebDriverLibValFix](https://github.com/mologie/NVWebDriverLibValFix)
- Injects IOVARendererID into GPU properties (required for Shiki-based solution for non-freezing Intel and/or any discrete GPU)
- Allows to use ports HDMI, DP, Digital DVI with audio (Injects @0connector-type - @5connector-type properties into GPU)

#### Credits
- [Apple](https://www.apple.com) for macOS  
- [vit9696](https://github.com/vit9696) for [Lilu.kext](https://github.com/vit9696/Lilu) & for zero-length string comparison patch (AppleGraphicsDevicePolicy.kext )
- [Pike R. Alpha](https://github.com/Piker-Alpha) for board-id patch (AppleGraphicsDevicePolicy.kext)
- [FredWst](http://www.insanelymac.com/forum/user/509660-fredwst/)
- [igork](https://applelife.ru/members/igork.564) for adding properties IOVARendererID & IOVARendererSubID in nvAcceleratorParent::SetAccelProperties
- [mologie](https://github.com/mologie/NVWebDriverLibValFix) for creating NVWebDriverLibValFix.kext which forces macOS to recognize NVIDIA's web drivers as platform binaries
- [lvs1974](https://applelife.ru/members/lvs1974.53809) for writing the software and maintaining it
