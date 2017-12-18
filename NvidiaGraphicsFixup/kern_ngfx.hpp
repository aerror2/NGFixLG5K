//
//  kern_ngfx.hpp
//  NvidiaGraphicsFixup
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#ifndef kern_ngfx_hpp
#define kern_ngfx_hpp

#include <Headers/kern_patcher.hpp>
#include <Library/LegacyIOService.h>
#define IOFRAMEBUFFER_PRIVATE
#include <IOKit/IOUserClient.h>
#include <IOKit/ndrvsupport/IONDRVFramebuffer.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/pci/IOPCIDevice.h>

struct KextPatch {
    KernelPatcher::LookupPatch patch;
    uint32_t minKernel;
    uint32_t maxKernel;
};

class NGFX {
public:
	bool init();
	void deinit();
	
private:
    /**
     *  Patch kernel
     *
     *  @param patcher KernelPatcher instance
     */
    void processKernel(KernelPatcher &patcher);
    
	/**
	 *  Patch kext if needed and prepare other patches
	 *
	 *  @param patcher KernelPatcher instance
	 *  @param index   kinfo handle
	 *  @param address kinfo load address
	 *  @param size    kinfo memory size
	 */
	void processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
    
    /**
     *  SetAccelProperties callback type
     */
    using t_set_accel_properties = void (*) (IOService * that);

    /**
     *  AppleGraphicsDevicePolicy::start callback type
     */
    using t_agdp_start = bool (*) (IOService *that, IOService *);
    
    /**
     *  csfg_get_platform_binary callback type
     */
    using t_csfg_get_platform_binary = int (*) (void * fg);
    
    /**
     *  csfg_get_teamid callback type
     */
    using t_csfg_get_teamid = const char* (*) (void *fg);
    
    
    
    
    /**
     *  Hooked methods / callbacks
     */
    static void nvAccelerator_SetAccelProperties(IOService* that);

    static bool AppleGraphicsDevicePolicy_start(IOService *that, IOService *provider);
    
    static int csfg_get_platform_binary(void *fg);
    
   
    /**
     *  Trampolines for original method invocations
     */
    t_set_accel_properties      orgSetAccelProperties {nullptr};
    
    t_agdp_start                orgAgdpStart {nullptr};
    
    t_csfg_get_platform_binary  org_csfg_get_platform_binary {nullptr};
    
    t_csfg_get_teamid           csfg_get_teamid {nullptr};
    
    

    //    static IOReturn extSetStartupDisplayModePatch(OSObject * target, void * reference, IOExternalMethodArguments * args);
    //    static IOReturn nvda_setDetailedTiming_patch(IONDRVFramebuffer *that,IODisplayModeID mode, IOOptionBits options, void * _desc, IOByteCount descripSize);
    //
    //    t_extSetStartupDisplayMode orgExtSetStartupDisplayMode{nullptr};
    
//    t_getStartupDisplayMode  org_getStartupDisplayMode{nullptr};
//    t_doDriverIO             org_doDriverIO{nullptr};
//
//
//
//    using t_enablecontroller = IOReturn (*)(IONDRVFramebuffer *that);
//    using t_setDisplayMode = IOReturn (*)(IONDRVFramebuffer *that, IODisplayModeID displayMode, IOIndex depth );
//    using t_setDetailedTimings = IOReturn (*)(IONDRVFramebuffer *that, OSArray *timings);
//
//    t_enablecontroller  org_enablecontroller{nullptr};
//    t_setDisplayMode   org_setDisplayMode{nullptr};
//    t_setDetailedTimings org_setDetailedTimings{nullptr};
//
//    static IOReturn nvenablecontroller(IONDRVFramebuffer *that);
//    static IOReturn setDisplayMode(IONDRVFramebuffer *that, IODisplayModeID displayMode, IOIndex depth );
//    static IOReturn setDetailedTimings(IONDRVFramebuffer *that, OSArray *timings);
//
//    //  __ZN13IOFramebuffer16extSetPropertiesEP12OSDictionary:        // IOFramebuffer::extSetProperties(OSDictionary*)
//    using t_extSetProperties = IOReturn (*) (IOFramebuffer * that, OSDictionary * dict );
//    // __ZN13IOFramebuffer16matchFramebufferEv:        // IOFramebuffer::matchFramebuffer()
//    using t_matchFramebuffer = IOReturn (*) (IOFramebuffer * that);
//    //    __ZN17IONDRVFramebuffer20processConnectChangeEPm:        // IONDRVFramebuffer::processConnectChange(unsigned long*)
//    using t_processConnectChange= IOReturn (*)(IONDRVFramebuffer *that, uintptr_t * value );
//
//    t_extSetProperties org_extSetProperties{nullptr};
//    t_matchFramebuffer org_matchFramebuffer{nullptr};
//    t_processConnectChange org_processConnectChange{nullptr};
//
    


    
    
    using t_nvda_validateDetailedTiming = IOReturn (*) (IONDRVFramebuffer *that, void * _desc, IOByteCount descripSize);
    t_nvda_validateDetailedTiming  org_nda_validateDetailedTiming{nullptr};
    static IOReturn nvda_validateDetailedTiming_patch(IONDRVFramebuffer *that, void*, unsigned long long);
    
    using t_setStartupDisplayMode =  IOReturn (*)(IONDRVFramebuffer *that, IODisplayModeID displayMode, IOIndex depth );
    t_setStartupDisplayMode  org_setStartupDisplayMode{nullptr};
    static IOReturn setStartupDisplayMode (IONDRVFramebuffer *that, IODisplayModeID displayMode, IOIndex depth );
    
  
    
//    using t_extSetStartupDisplayMode = IOReturn (*) (OSObject * target, void * reference, IOExternalMethodArguments * args);
//
    
    //IOReturn IONDRVFramebuffer::setDetailedTiming(IODisplayModeID mode, IOOptionBits options, void * _desc, IOByteCount descripSize )
    
//    using t_setDetailedTiming = IOReturn (*)(IONDRVFramebuffer *that,IODisplayModeID mode, IOOptionBits options, void * _desc, IOByteCount descripSize);
//
//    using t_getStartupDisplayMode =  IOReturn  (*)(IONDRVFramebuffer *that, IODisplayModeID * displayMode, IOIndex * depth );
//    using t_doDriverIO =  IOReturn (*)(IONDRVFramebuffer *that, UInt32 commandID, void * contents, UInt32 commandCode, UInt32 commandKind );
//
//
    
    
    
    
//    static IOReturn getStartupDisplayMode(IONDRVFramebuffer *that, IODisplayModeID * displayMode, IOIndex * depth );

//    static IOReturn doDriverIO(IONDRVFramebuffer *that, UInt32 commandID, void * contents, UInt32 commandCode, UInt32 commandKind );
//
   
    
   

//    static IOReturn  extSetProperties(IOFramebuffer * that, OSDictionary * dict );
//    static IOReturn matchFramebuffer(IOFramebuffer * that);
  //  static IOReturn processConnectChange(IONDRVFramebuffer *that, uintptr_t * value);
    
//    //IOReturn IONDRVFramebuffer::validateDisplayMode( IODisplayModeID _mode, IOOptionBits flags, VDDetailedTimingRec ** detailed )
//    //  __ZN17IONDRVFramebuffer19validateDisplayModeEijPP19VDDetailedTimingRec:        // IONDRVFramebuffer::validateDisplayMode(int, unsigned int, VDDetailedTimingRec**)
//    using t_validateDisplayMode = IOReturn (*)(IONDRVFramebuffer *that, IODisplayModeID _mode, IOOptionBits flags, VDDetailedTimingRec ** detailed );
//    t_validateDisplayMode  org_validateDisplayMode;
//
//    //  __ZN17IONDRVFramebuffer20getResInfoForArbModeEiP24IODisplayModeInformation:        // IONDRVFramebuffer::getResInfoForArbMode(int, IODisplayModeInformation*)
//    using t_getResInfoForArbMode = IOReturn (*)(IONDRVFramebuffer *that, IODisplayModeID mode, IODisplayModeInformation* info);
//    t_getResInfoForArbMode  org_getResInfoForArbMode{nullptr};
//
//
////    IOReturn IONDRVFramebuffer::getResInfoForMode( IODisplayModeID modeID,
////                                                  IODisplayModeInformation * info )
//    using t_getResInfoForMode = IOReturn (*)(IONDRVFramebuffer *that, IODisplayModeID mode,   IODisplayModeInformation * info );
//    t_getResInfoForMode  org_getResInfoForMode{nullptr};
//
//
//    //__ZN4NVDA28getInformationForDisplayModeEiP24IODisplayModeInformation:        // NVDA::getInformationForDisplayMode(int, IODisplayModeInformation*)
//
//    using t_getInformationForDisplayMode = IOReturn (*)(IONDRVFramebuffer *that, IODisplayModeID mode,   IODisplayModeInformation * info  );
//    t_getInformationForDisplayMode  org_getInformationForDisplayMode{nullptr};
//
//
//    //    IOReturn IOFramebuffer::extGetInformationForDisplayMode(
//    //                                                            OSObject * target, void * reference, IOExternalMethodArguments * args)
//    //__ZN13IOFramebuffer31extGetInformationForDisplayModeEP8OSObjectPvP25IOExternalMethodArguments:        // IOFramebuffer::extGetInformationForDisplayMode(OSObject*, void*, IOExternalMethodArguments*)
//
//    using t_extGetInformationForDisplayMode = IOReturn (*)(  OSObject * target, void * reference, IOExternalMethodArguments * args );
//    t_extGetInformationForDisplayMode  org_extGetInformationForDisplayMode{nullptr};
//
//
//    static IOReturn validateDisplayMode (IONDRVFramebuffer *that, IODisplayModeID _mode, IOOptionBits flags, VDDetailedTimingRec ** detailed );
//    static IOReturn getResInfoForArbMode (IONDRVFramebuffer *that, IODisplayModeID mode, IODisplayModeInformation* info );
//    static IOReturn getResInfoForMode (IONDRVFramebuffer *that, IODisplayModeID mode,   IODisplayModeInformation * info );
//    static IOReturn getInformationForDisplayMode (IONDRVFramebuffer *that, IODisplayModeID mode,   IODisplayModeInformation * info  );
//    static IOReturn extGetInformationForDisplayMode (OSObject * target, void * reference, IOExternalMethodArguments * args);
//
//
//    //__ZN17IONDRVFramebuffer19getPixelInformationEiiiP18IOPixelInformation:        // IONDRVFramebuffer::getPixelInformation(int, int, int, IOPixelInformation*)
//    using t_getPixelInformation = IOReturn (*)( IONDRVFramebuffer *that,  IODisplayModeID displayMode, IOIndex depth,
//                                               IOPixelAperture aperture, IOPixelInformation * pixelInfo);
//    t_getPixelInformation  org_getPixelInformation{nullptr};
//    static IOReturn getPixelInformation ( IONDRVFramebuffer *that,  IODisplayModeID displayMode, IOIndex depth,
//                                            IOPixelAperture aperture, IOPixelInformation * pixelInfo);
//
//    //     __ZN13IOFramebuffer7doSetupEb:        // IOFramebuffer::doSetup(bool)
//    using t_doSetup = IOReturn (*)( IONDRVFramebuffer *that,  bool full);
//    t_doSetup  org_doSetup{nullptr};
//    static IOReturn doSetup ( IONDRVFramebuffer *that,  bool full);
    
    
    
    
    
    using t_NVDA_start=bool (*) (IONDRVFramebuffer *that,IOService * provider) ;
    t_NVDA_start org_NVDA_start={nullptr};
    static bool NVDA_start(IONDRVFramebuffer *that,IOService * provider) ;
    using t_NVDA_stop=void (*) (IONDRVFramebuffer *that,IOService * provider ) ;
    t_NVDA_stop org_NVDA_stop={nullptr};
    static void NVDA_stop(IONDRVFramebuffer *that,IOService * provider ) ;
    using t_NVDA_glkClose=IOReturn (*) (IONDRVFramebuffer *that,IOService* svc) ;
    t_NVDA_glkClose org_NVDA_glkClose={nullptr};
    static IOReturn NVDA_glkClose(IONDRVFramebuffer *that,IOService* svc) ;
    using t_IONDRVFramebuffer_enableController=IOReturn (*) (IONDRVFramebuffer *that) ;
    t_IONDRVFramebuffer_enableController org_IONDRVFramebuffer_enableController={nullptr};
    static IOReturn IONDRVFramebuffer_enableController(IONDRVFramebuffer *that) ;
    using t_NVDA_ScanList=IOReturn (*) (IONDRVFramebuffer *that,IOService* svc) ;
    t_NVDA_ScanList org_NVDA_ScanList={nullptr};
    static IOReturn NVDA_ScanList(IONDRVFramebuffer *that,IOService* svc) ;
    using t_NVDA_InsertNode=IOReturn (*) (IONDRVFramebuffer *that,IOService* svc) ;
    t_NVDA_InsertNode org_NVDA_InsertNode={nullptr};
    static IOReturn NVDA_InsertNode(IONDRVFramebuffer *that,IOService* svc) ;
    using t_NVDA_DevTypeDisplay=IOReturn (*) (IONDRVFramebuffer *that,IORegistryEntry* preg) ;
    t_NVDA_DevTypeDisplay org_NVDA_DevTypeDisplay={nullptr};
    static IOReturn NVDA_DevTypeDisplay(IONDRVFramebuffer *that,IORegistryEntry* preg) ;
    using t_NVDA_initAppleMux=IOReturn (*) (IONDRVFramebuffer *that,IORegistryEntry* preg) ;
    t_NVDA_initAppleMux org_NVDA_initAppleMux={nullptr};
    static IOReturn NVDA_initAppleMux(IONDRVFramebuffer *that,IORegistryEntry* preg) ;
    using t_NVDA_setAppleMux=IOReturn (*) (IONDRVFramebuffer *that,unsigned int p1) ;
    t_NVDA_setAppleMux org_NVDA_setAppleMux={nullptr};
    static IOReturn NVDA_setAppleMux(IONDRVFramebuffer *that,unsigned int p1) ;
    using t_NVDA_rmStart=IOReturn (*) (IONDRVFramebuffer *that,IOService* svc, unsigned int p1) ;
    t_NVDA_rmStart org_NVDA_rmStart={nullptr};
    static IOReturn NVDA_rmStart(IONDRVFramebuffer *that,IOService* svc, unsigned int p1) ;
  
    using t_NVDA_doHotplugInterruptService=void (*) (OSObject * owner,IOInterruptEventSource * evtSrc, int intCount) ;
    t_NVDA_doHotplugInterruptService org_NVDA_doHotplugInterruptService={nullptr};
    static void NVDA_doHotplugInterruptService(OSObject * owner,IOInterruptEventSource * evtSrc, int intCount) ;
    using t_NVDA_doAudioHDCPInterruptService=void (*) (OSObject * owner,IOInterruptEventSource * evtSrc, int intCount) ;
    t_NVDA_doAudioHDCPInterruptService org_NVDA_doAudioHDCPInterruptService={nullptr};
    static void NVDA_doAudioHDCPInterruptService(OSObject * owner,IOInterruptEventSource * evtSrc, int intCount) ;
    using t_NVDA_doHDCPReadyInterruptService=void (*) (OSObject * owner,IOInterruptEventSource * evtSrc, int intCount) ;
    t_NVDA_doHDCPReadyInterruptService org_NVDA_doHDCPReadyInterruptService={nullptr};
    static void NVDA_doHDCPReadyInterruptService(OSObject * owner,IOInterruptEventSource * evtSrc, int intCount) ;
    using t_NVDA_glkOpen=IOReturn (*) (IONDRVFramebuffer *that,IOService* svc) ;
    
    t_NVDA_glkOpen org_NVDA_glkOpen={nullptr};
    static IOReturn NVDA_glkOpen(IONDRVFramebuffer *that,IOService* svc) ;
    using t_NVDA_getNVAcceleratorPropertyFromRegistry=IOReturn (*) (IONDRVFramebuffer *that,char const* p1, unsigned int* p2) ;
    t_NVDA_getNVAcceleratorPropertyFromRegistry org_NVDA_getNVAcceleratorPropertyFromRegistry={nullptr};
    static IOReturn NVDA_getNVAcceleratorPropertyFromRegistry(IONDRVFramebuffer *that,char const* p1, unsigned int* p2) ;
    using t_NVDA_callPlatformFunction=IOReturn (*) (IONDRVFramebuffer *that,OSSymbol const* p1, bool p2, void* p3, void* p4, void* p5, void* p6) ;
    t_NVDA_callPlatformFunction org_NVDA_callPlatformFunction={nullptr};
    static IOReturn NVDA_callPlatformFunction(IONDRVFramebuffer *that,OSSymbol const* p1, bool p2, void* p3, void* p4, void* p5, void* p6) ;
    using t_NVDA_mergeDeviceProperties=IOReturn (*) (IONDRVFramebuffer *that,char const*, char const*) ;
    t_NVDA_mergeDeviceProperties org_NVDA_mergeDeviceProperties={nullptr};
    static IOReturn NVDA_mergeDeviceProperties(IONDRVFramebuffer *that,char const*, char const*) ;
    using t_NVDA_getApertureRange=IODeviceMemory *  (*) (IONDRVFramebuffer *that,int) ;
    t_NVDA_getApertureRange org_NVDA_getApertureRange={nullptr};
    static IODeviceMemory *  NVDA_getApertureRange(IONDRVFramebuffer *that,int) ;
    using t_NVDA_getInformationForDisplayMode=IOReturn (*) (IONDRVFramebuffer *that,int, IODisplayModeInformation*) ;
    t_NVDA_getInformationForDisplayMode org_NVDA_getInformationForDisplayMode={nullptr};
    static IOReturn NVDA_getInformationForDisplayMode(IONDRVFramebuffer *that,int, IODisplayModeInformation*) ;
    using t_NVDA_setAttribute=IOReturn (*) (IONDRVFramebuffer *that,IOSelect attribute, uintptr_t value) ;
    t_NVDA_setAttribute org_NVDA_setAttribute={nullptr};
    static IOReturn NVDA_setAttribute(IONDRVFramebuffer *that,IOSelect attribute, uintptr_t value) ;
    using t_NVDA_getAttribute=IOReturn (*) (IONDRVFramebuffer *that,IOSelect attribute, uintptr_t * value ) ;
    t_NVDA_getAttribute org_NVDA_getAttribute={nullptr};
    static IOReturn NVDA_getAttribute(IONDRVFramebuffer *that,IOSelect attribute, uintptr_t * value ) ;
    using t_NVDA_doVBLInterrupt=void (*) (IONDRVFramebuffer *that) ;
    t_NVDA_doVBLInterrupt org_NVDA_doVBLInterrupt={nullptr};
    static void NVDA_doVBLInterrupt(IONDRVFramebuffer *that) ;
    using t_NVDA_doHotPlugInterrupt=void (*) (IONDRVFramebuffer *that) ;
    t_NVDA_doHotPlugInterrupt org_NVDA_doHotPlugInterrupt={nullptr};
    static void NVDA_doHotPlugInterrupt(IONDRVFramebuffer *that) ;
    using t_NVDA_doGlkWindowServerTransitionNotify=IOReturn (*) (IONDRVFramebuffer *that,unsigned int p1, unsigned int p2, unsigned int* p3, unsigned int p4) ;
    t_NVDA_doGlkWindowServerTransitionNotify org_NVDA_doGlkWindowServerTransitionNotify={nullptr};
    static IOReturn NVDA_doGlkWindowServerTransitionNotify(IONDRVFramebuffer *that,unsigned int p1, unsigned int p2, unsigned int* p3, unsigned int p4) ;
    using t_NVDA_setGammaTable=IOReturn (*) (IONDRVFramebuffer *that,unsigned int p1, unsigned int p2, unsigned int p3, void* p4) ;
    t_NVDA_setGammaTable org_NVDA_setGammaTable={nullptr};
    static IOReturn NVDA_setGammaTable(IONDRVFramebuffer *that,unsigned int p1, unsigned int p2, unsigned int p3, void* p4) ;
    using t_NVDA_setCLUTWithEntries=IOReturn (*) (IONDRVFramebuffer *that,IOColorEntry* p1, unsigned int p2, unsigned int p3, unsigned int p4) ;
    t_NVDA_setCLUTWithEntries org_NVDA_setCLUTWithEntries={nullptr};
    static IOReturn NVDA_setCLUTWithEntries(IONDRVFramebuffer *that,IOColorEntry* p1, unsigned int p2, unsigned int p3, unsigned int p4) ;
    using t_NVDA_undefinedSymbolHandler=IOReturn (*) (IONDRVFramebuffer *that,char const* p1, char const* p2) ;
    t_NVDA_undefinedSymbolHandler org_NVDA_undefinedSymbolHandler={nullptr};
    static IOReturn NVDA_undefinedSymbolHandler(IONDRVFramebuffer *that,char const* p1, char const* p2) ;
    using t_NVDA_probe=IOService * (*) (IONDRVFramebuffer *that,IOService * provider, SInt32 * score) ;
    t_NVDA_probe org_NVDA_probe={nullptr};
    static IOService * NVDA_probe(IONDRVFramebuffer *that,IOService * provider, SInt32 * score) ;
    using t_NVDA_newUserClient=IOReturn (*) (IONDRVFramebuffer *that,task_t          owningTask,void *          security_id,UInt32          type, IOUserClient ** clientH) ;
    t_NVDA_newUserClient org_NVDA_newUserClient={nullptr};
    static IOReturn NVDA_newUserClient(IONDRVFramebuffer *that,task_t          owningTask,void *          security_id,UInt32          type, IOUserClient ** clientH) ;
    using t_NVDA_copyACPIDevice=IOReturn (*) (IONDRVFramebuffer *that,IORegistryEntry* p1) ;
    t_NVDA_copyACPIDevice org_NVDA_copyACPIDevice={nullptr};
    static IOReturn NVDA_copyACPIDevice(IONDRVFramebuffer *that,IORegistryEntry* p1) ;
    using t_NVDA_findAppleMuxSubdevice=IOReturn (*) (IONDRVFramebuffer *that,IORegistryEntry* p1, unsigned int p2) ;
    t_NVDA_findAppleMuxSubdevice org_NVDA_findAppleMuxSubdevice={nullptr};
    static IOReturn NVDA_findAppleMuxSubdevice(IONDRVFramebuffer *that,IORegistryEntry* p1, unsigned int p2) ;
    using t_NVDA_setAppleMuxSubdevice=IOReturn (*) (IONDRVFramebuffer *that,IOACPIPlatformDevice* p1, unsigned int p2) ;
    t_NVDA_setAppleMuxSubdevice org_NVDA_setAppleMuxSubdevice={nullptr};
    static IOReturn NVDA_setAppleMuxSubdevice(IONDRVFramebuffer *that,IOACPIPlatformDevice* p1, unsigned int p2) ;
    using t_NVDA_getAppleMux=IOReturn (*) (IONDRVFramebuffer *that) ;
    t_NVDA_getAppleMux org_NVDA_getAppleMux={nullptr};
    static IOReturn NVDA_getAppleMux(IONDRVFramebuffer *that) ;
    using t_NVDA_setProperty=bool (*) (IONDRVFramebuffer *that,const OSSymbol * aKey, OSObject * anObject) ;
    t_NVDA_setProperty org_NVDA_setProperty={nullptr};
    static bool NVDA_setProperty(IONDRVFramebuffer *that,const OSSymbol * aKey, OSObject * anObject) ;
    using t_NVDA_addIntrReg=IOReturn (*) (IONDRVFramebuffer *that,unsigned int p1, void* p2) ;
    t_NVDA_addIntrReg org_NVDA_addIntrReg={nullptr};
    static IOReturn NVDA_addIntrReg(IONDRVFramebuffer *that,unsigned int p1, void* p2) ;
    using t_NVDA_removeIntrReg=IOReturn (*) (IONDRVFramebuffer *that,void* p1) ;
    t_NVDA_removeIntrReg org_NVDA_removeIntrReg={nullptr};
    static IOReturn NVDA_removeIntrReg(IONDRVFramebuffer *that,void* p1) ;
    using t_NVDA_findIntrReg=IOReturn (*) (IONDRVFramebuffer *that,void* p1) ;
    t_NVDA_findIntrReg org_NVDA_findIntrReg={nullptr};
    static IOReturn NVDA_findIntrReg(IONDRVFramebuffer *that,void* p1) ;
    using t_NVDA_registerForInterruptType=IOReturn (*) (IONDRVFramebuffer *that,IOSelect interruptType,IOFBInterruptProc proc, OSObject * target, void * ref,void ** interruptRef) ;
    t_NVDA_registerForInterruptType org_NVDA_registerForInterruptType={nullptr};
    static IOReturn NVDA_registerForInterruptType(IONDRVFramebuffer *that,IOSelect interruptType,IOFBInterruptProc proc, OSObject * target, void * ref,void ** interruptRef) ;
    using t_NVDA_unregisterInterrupt=IOReturn (*) (IONDRVFramebuffer *that,void* interruptRef) ;
    t_NVDA_unregisterInterrupt org_NVDA_unregisterInterrupt={nullptr};
    static IOReturn NVDA_unregisterInterrupt(IONDRVFramebuffer *that,void* interruptRef) ;
    using t_NVDA_setInterruptState=IOReturn (*) (IONDRVFramebuffer *that,void * interruptRef, UInt32 state) ;
    t_NVDA_setInterruptState org_NVDA_setInterruptState={nullptr};
    static IOReturn NVDA_setInterruptState(IONDRVFramebuffer *that,void * interruptRef, UInt32 state) ;
    using t_NVDA_setAttributeForConnection=IOReturn (*) (IONDRVFramebuffer *that,IOIndex connectIndex,IOSelect attribute, uintptr_t value) ;
    t_NVDA_setAttributeForConnection org_NVDA_setAttributeForConnection={nullptr};
    static IOReturn NVDA_setAttributeForConnection(IONDRVFramebuffer *that,IOIndex connectIndex,IOSelect attribute, uintptr_t value) ;
    using t_NVDA_getAttributeForConnection=IOReturn (*) (IONDRVFramebuffer *that,IOIndex connectIndex,IOSelect attribute, uintptr_t * value ) ;
    t_NVDA_getAttributeForConnection org_NVDA_getAttributeForConnection={nullptr};
    static IOReturn NVDA_getAttributeForConnection(IONDRVFramebuffer *that,IOIndex connectIndex,IOSelect attribute, uintptr_t * value ) ;
    using t_NVDA_message=IOReturn (*) (IONDRVFramebuffer *that,UInt32 type, IOService *provider, void *argument) ;
    t_NVDA_message org_NVDA_message={nullptr};
    static IOReturn NVDA_message(IONDRVFramebuffer *that,UInt32 type, IOService *provider, void *argument) ;
    using t_NVDA_createNVDCNub=IOReturn (*) (IONDRVFramebuffer *that,unsigned int p1) ;
    t_NVDA_createNVDCNub org_NVDA_createNVDCNub={nullptr};
    static IOReturn NVDA_createNVDCNub(IONDRVFramebuffer *that,unsigned int p1) ;
    using t_NVDA_get10BPCFormatGLKSupport=IOReturn (*) (IONDRVFramebuffer *that,unsigned char* p1) ;
    t_NVDA_get10BPCFormatGLKSupport org_NVDA_get10BPCFormatGLKSupport={nullptr};
    static IOReturn NVDA_get10BPCFormatGLKSupport(IONDRVFramebuffer *that,unsigned char* p1) ;
    using t_NVDA_getIOPCIDeviceFromDeviceInstance=IOReturn (*) (IONDRVFramebuffer *that,unsigned int p1, IOPCIDevice** lppdevice) ;
    t_NVDA_getIOPCIDeviceFromDeviceInstance org_NVDA_getIOPCIDeviceFromDeviceInstance={nullptr};
    static IOReturn NVDA_getIOPCIDeviceFromDeviceInstance(IONDRVFramebuffer *that,unsigned int p1, IOPCIDevice** lppdevice) ;
    using t_NVDA_getIOPCIDevicePropertyFromRegistry=IOReturn (*) (IONDRVFramebuffer *that,char const* p1, unsigned short* p2) ;
    t_NVDA_getIOPCIDevicePropertyFromRegistry org_NVDA_getIOPCIDevicePropertyFromRegistry={nullptr};
    static IOReturn NVDA_getIOPCIDevicePropertyFromRegistry(IONDRVFramebuffer *that,char const* p1, unsigned short* p2) ;
    using t_IOFramebuffer_doSetup=IOReturn (*) (IONDRVFramebuffer *that,bool bfull) ;
    t_IOFramebuffer_doSetup org_IOFramebuffer_doSetup={nullptr};
    static IOReturn IOFramebuffer_doSetup(IONDRVFramebuffer *that,bool bfull) ;
    using t_IONDRVFramebuffer_processConnectChange=IOReturn (*) (IONDRVFramebuffer *that,uintptr_t * value) ;
    t_IONDRVFramebuffer_processConnectChange org_IONDRVFramebuffer_processConnectChange={nullptr};
    static IOReturn IONDRVFramebuffer_processConnectChange(IONDRVFramebuffer *that,uintptr_t * value) ;

    
    
    using t_IOFramebuffer_checkPowerWork=IOOptionBits (*) (IONDRVFramebuffer *that,unsigned int) ;
    t_IOFramebuffer_checkPowerWork org_IOFramebuffer_checkPowerWork={nullptr};
    static IOOptionBits IOFramebuffer_checkPowerWork(IONDRVFramebuffer *that,unsigned int) ;
    
    
    using t_IOFramebuffer_dpProcessInterrupt=void (*) (IONDRVFramebuffer *that) ;
    t_IOFramebuffer_dpProcessInterrupt org_IOFramebuffer_dpProcessInterrupt={nullptr};
    static void IOFramebuffer_dpProcessInterrupt(IONDRVFramebuffer *that) ;

    using t_IOFramebuffer_open=IOReturn (*) (IONDRVFramebuffer *that) ;
    t_IOFramebuffer_open org_IOFramebuffer_open={nullptr};
    static IOReturn IOFramebuffer_open(IONDRVFramebuffer *that) ;
    /**
     *  Apply kext patches for loaded kext index
     *
     *  @param patcher    KernelPatcher instance
     *  @param index      kinfo index
     *  @param patches    patch list
     *  @param patchesNum patch number
     */
    void applyPatches(KernelPatcher &patcher, size_t index, const KextPatch *patches, size_t patchesNum, const char *name);
	
	/**
	 *  Current progress mask
	 */
	struct ProcessingState {
		enum {
			NothingReady = 0,
			GraphicsDevicePolicyPatched = 2,
            GeForceRouted = 4,
            GeForceWebRouted = 8,
            KernelRouted = 16,
            NVDAResmanRouted = 32,
            NVDAIOGraphicsRouted = 64,
            NVDASupportRouted = 128,

			EverythingDone = GraphicsDevicePolicyPatched | GeForceRouted | GeForceWebRouted | KernelRouted | NVDAResmanRouted,
		};
	};
    int progressState {ProcessingState::NothingReady};
    
    static constexpr const char* kNvidiaTeamId { "6KR3T733EC" };
};

#endif /* kern_ngfx */
