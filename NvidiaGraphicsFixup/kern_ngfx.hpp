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
    
    //__ZN4NVDA25getAttributeForConnectionEijPm:

    using t_NVDA_getAttributeForConnection=IOReturn (*) (IONDRVFramebuffer *that, IOIndex connectIndex, IOSelect attribute, uintptr_t * value) ;
    t_NVDA_getAttributeForConnection org_NVDA_getAttributeForConnection={nullptr};
    static IOReturn NVDA_getAttributeForConnection(IONDRVFramebuffer *that, IOIndex connectIndex, IOSelect attribute, uintptr_t * value) ;
    
    
    //   __ZN4NVDA25setAttributeForConnectionEijm:        // NVDA::setAttributeForConnection(int, unsigned int, unsigned long)
    using t_NVDA_setAttributeForConnection=IOReturn (*) (IONDRVFramebuffer *that, IOIndex connectIndex,IOSelect attribute, uintptr_t value) ;
    t_NVDA_setAttributeForConnection org_NVDA_setAttributeForConnection={nullptr};
    static IOReturn NVDA_setAttributeForConnection(IONDRVFramebuffer *that, IOIndex connectIndex,IOSelect attribute, uintptr_t value) ;
    
    
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
