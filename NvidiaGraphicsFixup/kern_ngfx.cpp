//
//  kern_ngfx.cpp
//  NvidiaGraphicsFixup
//
//  Copyright © 2017 lvs1974. All rights reserved.
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_iokit.hpp>

#include "kern_config.hpp"
#include "kern_ngfx.hpp"


static const char *kextAGDPolicy[] { "/System/Library/Extensions/AppleGraphicsControl.kext/Contents/PlugIns/AppleGraphicsDevicePolicy.kext/Contents/MacOS/AppleGraphicsDevicePolicy" };
static const char *kextAGDPolicyId { "com.apple.driver.AppleGraphicsDevicePolicy" };

static const char *kextGeForce[] { "/System/Library/Extensions/GeForce.kext/Contents/MacOS/GeForce" };
static const char *kextGeForceId { "com.apple.GeForce" };

static const char *kextGeForceWeb[] { "/Library/Extensions/GeForceWeb.kext/Contents/MacOS/GeForceWeb", "/System/Library/Extensions/GeForceWeb.kext/Contents/MacOS/GeForceWeb" };
static const char *kextGeForceWebId { "com.nvidia.web.GeForceWeb" };


static const char *kextNVDAResmanWeb[] { "/Library/Extensions/NVDAResmanWeb.kext/Contents/MacOS/NVDAResmanWeb" };
static const char *kextNVDAResmanId { "com.nvidia.web.NVDAResmanWeb" };

static const char *kextIOGraphicsFamily[] { "/System/Library/Extensions/IOGraphicsFamily.kext/IOGraphicsFamily" };
static const char *kextIOGraphicsFamilyId { "com.apple.iokit.IOGraphicsFamily" };


static const char *kextIONDRVSupport[] { "/System/Library/Extensions/IONDRVSupport.kext/IONDRVSupport" };
static const char *kextIONDRVSupportId { "com.apple.iokit.IONDRVSupport" };

static KernelPatcher::KextInfo kextList[] {
    { kextAGDPolicyId,      kextAGDPolicy,   arrsize(kextAGDPolicy),  {true}, {}, KernelPatcher::KextInfo::Unloaded },
    { kextGeForceId,        kextGeForce,     arrsize(kextGeForce),    {},     {}, KernelPatcher::KextInfo::Unloaded },
    { kextGeForceWebId,     kextGeForceWeb,  arrsize(kextGeForceWeb), {},     {}, KernelPatcher::KextInfo::Unloaded },
    { kextNVDAResmanId,     kextNVDAResmanWeb,  arrsize(kextNVDAResmanWeb), {},     {}, KernelPatcher::KextInfo::Unloaded },
    { kextIOGraphicsFamilyId,     kextIOGraphicsFamily,  arrsize(kextIOGraphicsFamily), {},     {}, KernelPatcher::KextInfo::Unloaded },
    { kextIONDRVSupportId,     kextIONDRVSupport,  arrsize(kextIONDRVSupport), {},     {}, KernelPatcher::KextInfo::Unloaded },

};

static size_t kextListSize {arrsize(kextList)};

// Only used in apple-driven callbacks
static NGFX *callbackNGFX = nullptr;


bool NGFX::init() {
	if (getKernelVersion() > KernelVersion::Mavericks)
	{
		LiluAPI::Error error = lilu.onPatcherLoad(
		  [](void *user, KernelPatcher &patcher) {
			  callbackNGFX = static_cast<NGFX *>(user);
			  callbackNGFX->processKernel(patcher);
		  }, this);

		if (error != LiluAPI::Error::NoError) {
			SYSLOG("ngfx", "failed to register onPatcherLoad method %d", error);
			return false;
		}
	} else {
		progressState |= ProcessingState::KernelRouted;
	}
    
	LiluAPI::Error error = lilu.onKextLoad(kextList, kextListSize,
        [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
            callbackNGFX = static_cast<NGFX *>(user);
            callbackNGFX->processKext(patcher, index, address, size);
        }, this);
	
	if (error != LiluAPI::Error::NoError) {
		SYSLOG("ngfx", "failed to register onKextLoad method %d", error);
		return false;
	}
	
	return true;
}

void NGFX::deinit() {
}

void NGFX::processKernel(KernelPatcher &patcher) {
    if (!(progressState & ProcessingState::KernelRouted))
    {
        if (!config.nolibvalfix) {
            auto method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_csfg_get_teamid");
            if (method_address) {
                DBGLOG("ngfx", "obtained _csfg_get_teamid");
                csfg_get_teamid = reinterpret_cast<t_csfg_get_teamid>(method_address);
                
                method_address = patcher.solveSymbol(KernelPatcher::KernelID, "_csfg_get_platform_binary");
                if (method_address ) {
                    DBGLOG("ngfx", "obtained _csfg_get_platform_binary");
                    patcher.clearError();
                    org_csfg_get_platform_binary = reinterpret_cast<t_csfg_get_platform_binary>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(csfg_get_platform_binary), true));
                    if (patcher.getError() == KernelPatcher::Error::NoError) {
                        DBGLOG("ngfx", "routed _csfg_get_platform_binary");
                    } else {
                        SYSLOG("ngfx", "failed to route _csfg_get_platform_binary");
                    }
                } else {
                    SYSLOG("ngfx", "failed to resolve _csfg_get_platform_binary");
                }
                
            } else {
                SYSLOG("ngfx", "failed to resolve _csfg_get_teamid");
            }
        }
        
        progressState |= ProcessingState::KernelRouted;
    }
    
    // Ignore all the errors for other processors
    patcher.clearError();
}

void NGFX::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	if (progressState != ProcessingState::EverythingDone) {
		for (size_t i = 0; i < kextListSize; i++) {
			if (kextList[i].loadIndex == index) {
                if (!(progressState & ProcessingState::GraphicsDevicePolicyPatched) && !strcmp(kextList[i].id, kextAGDPolicyId))
                {
                    DBGLOG("ngfx", "found %s", kextAGDPolicyId);
                    
                    const bool patch_vit9696 = (strstr(config.patch_list, "vit9696") != nullptr);
                    const bool patch_pikera  = (strstr(config.patch_list, "pikera")  != nullptr);
                    const bool patch_cfgmap  = (strstr(config.patch_list, "cfgmap")  != nullptr);

                    if (patch_vit9696)
                    {
                        const uint8_t find[]    = {0xBA, 0x05, 0x00, 0x00, 0x00};
                        const uint8_t replace[] = {0xBA, 0x00, 0x00, 0x00, 0x00};
                        KextPatch kext_patch {
                            {&kextList[i], find, replace, sizeof(find), 1},
                            KernelVersion::MountainLion, KernelPatcher::KernelAny
                        };
                        applyPatches(patcher, index, &kext_patch, 1, "vit9696");
                    }

                    if (patch_pikera)
                    {
                        const uint8_t find[]    = "board-id";
                        const uint8_t replace[] = "board-ix";
                        KextPatch kext_patch {
                            {&kextList[i], find, replace, strlen((const char*)find), 1},
                            KernelVersion::MountainLion, KernelPatcher::KernelAny
                        };
                        applyPatches(patcher, index, &kext_patch, 1, "pikera");
                    }

                    if (patch_cfgmap)
                    {
                        auto method_address = patcher.solveSymbol(index, "__ZN25AppleGraphicsDevicePolicy5startEP9IOService");
                        if (method_address) {
                            DBGLOG("ngfx", "obtained __ZN25AppleGraphicsDevicePolicy5startEP9IOService");
                            patcher.clearError();
                            orgAgdpStart = reinterpret_cast<t_agdp_start>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(AppleGraphicsDevicePolicy_start), true));
                            if (patcher.getError() == KernelPatcher::Error::NoError) {
                                DBGLOG("ngfx", "routed __ZN25AppleGraphicsDevicePolicy5startEP9IOService");
                            } else {
                                SYSLOG("ngfx", "failed to route __ZN25AppleGraphicsDevicePolicy5startEP9IOService");
                            }
                        } else {
                            SYSLOG("ngfx", "failed to resolve __ZN25AppleGraphicsDevicePolicy5startEP9IOService");
                        }
                    }
                    progressState |= ProcessingState::GraphicsDevicePolicyPatched;
                }
                else if (!(progressState & ProcessingState::GeForceRouted) && !strcmp(kextList[i].id, kextGeForceId))
                {
                    if (!config.novarenderer) {
                        DBGLOG("ngfx", "found %s", kextGeForceId);
                        auto method_address = patcher.solveSymbol(index, "__ZN13nvAccelerator18SetAccelPropertiesEv");
                        if (method_address) {
                            DBGLOG("ngfx", "obtained __ZN13nvAccelerator18SetAccelPropertiesEv");
                            patcher.clearError();
                            orgSetAccelProperties = reinterpret_cast<t_set_accel_properties>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(nvAccelerator_SetAccelProperties), true));
                            if (patcher.getError() == KernelPatcher::Error::NoError) {
                                DBGLOG("ngfx", "routed __ZN13nvAccelerator18SetAccelPropertiesEv");
                            } else {
                                SYSLOG("ngfx", "failed to route __ZN13nvAccelerator18SetAccelPropertiesEv");
                            }
                        } else {
                            SYSLOG("ngfx", "failed to resolve __ZN13nvAccelerator18SetAccelPropertiesEv");
                        }
                    }
                    
                    progressState |= ProcessingState::GeForceRouted;
                }
                else if (!(progressState & ProcessingState::GeForceWebRouted) && !strcmp(kextList[i].id, kextGeForceWebId))
                {
                    if (!config.novarenderer) {
                        DBGLOG("ngfx", "found %s", kextGeForceWebId);
                        auto method_address = patcher.solveSymbol(index, "__ZN19nvAcceleratorParent18SetAccelPropertiesEv");
                        if (method_address) {
                            DBGLOG("ngfx", "obtained __ZN19nvAcceleratorParent18SetAccelPropertiesEv");
                            patcher.clearError();
                            orgSetAccelProperties = reinterpret_cast<t_set_accel_properties>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(nvAccelerator_SetAccelProperties), true));
                            if (patcher.getError() == KernelPatcher::Error::NoError) {
                                DBGLOG("ngfx", "routed __ZN19nvAcceleratorParent18SetAccelPropertiesEv");
                            } else {
                                SYSLOG("ngfx", "failed to route __ZN19nvAcceleratorParent18SetAccelPropertiesEv");
                            }
                        } else {
                            SYSLOG("ngfx", "failed to resolve __ZN19nvAcceleratorParent18SetAccelPropertiesEv");
                        }
                    }
                    
                    progressState |= ProcessingState::GeForceWebRouted;
                }
                else if (!(progressState & ProcessingState::NVDAResmanRouted) && !strcmp(kextList[i].id, kextNVDAResmanId))
                {
                    DBGLOG("ngfx", "found %s", kextNVDAResmanId);
                    
                    auto method_address = patcher.solveSymbol(index, "__ZN4NVDA22validateDetailedTimingEPvy");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN4NVDA22validateDetailedTimingEPvy");
                        patcher.clearError();
                        org_nda_validateDetailedTiming = reinterpret_cast<t_nvda_validateDetailedTiming>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(nvda_validateDetailedTiming_patch), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA22validateDetailedTimingEPvy");

                    }
                    
                    
                    method_address = patcher.solveSymbol(index, "__ZN4NVDA25setAttributeForConnectionEijm");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN4NVDA25setAttributeForConnectionEijm");
                        patcher.clearError();
                        org_NVDA_setAttributeForConnection = reinterpret_cast<t_NVDA_setAttributeForConnection>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_setAttributeForConnection), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA25setAttributeForConnectionEijm");
                    }
                     method_address = patcher.solveSymbol(index, "__ZN4NVDA25getAttributeForConnectionEijPm");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN4NVDA25getAttributeForConnectionEijPm");
                        patcher.clearError();
                        org_NVDA_getAttributeForConnection = reinterpret_cast<t_NVDA_getAttributeForConnection>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_getAttributeForConnection), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA25getAttributeForConnectionEijPm");
                        
                    }

                    
                    //__ZN4NVDA16enableControllerEv
                    
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA16enableControllerEv");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA16enableControllerEv");
//                        patcher.clearError();
//                        org_enablecontroller = reinterpret_cast<t_enablecontroller>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(nvenablecontroller), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA16enableControllerEv");
//
//                    }
                    
                    
                    
                    //__ZN4NVDA28getInformationForDisplayModeEiP24IODisplayModeInformation:        // NVDA::getInformationForDisplayMode(int, IODisplayModeInformation*)
                    
                   // using t_getInformationForDisplayMode = IOReturn (*)(IONDRVFramebuffer *that, IODisplayModeID mode,   IODisplayModeInformation * info  );
                   // t_getInformationForDisplayMode  org_getInformationForDisplayMode{nullptr};
                    
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA28getInformationForDisplayModeEiP24IODisplayModeInformation");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA28getInformationForDisplayModeEiP24IODisplayModeInformation");
//                        patcher.clearError();
//                        org_getInformationForDisplayMode = reinterpret_cast<t_getInformationForDisplayMode>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(getInformationForDisplayMode), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA28getInformationForDisplayModeEiP24IODisplayModeInformation");
//
//                    }
                    
                    progressState |= ProcessingState::NVDAResmanRouted;

                }
                
                else if (!(progressState & ProcessingState::NVDAIOGraphicsRouted) && !strcmp(kextList[i].id, kextIOGraphicsFamilyId))
                {
                    DBGLOG("ngfx", "found %s", kextIOGraphicsFamilyId);

//                    auto method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer24extSetStartupDisplayModeEP8OSObjectPvP25IOExternalMethodArguments");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer24extSetStartupDisplayModeEP8OSObjectPvP25IOExternalMethodArguments");
//                        patcher.clearError();
//                        orgExtSetStartupDisplayMode = reinterpret_cast<t_extSetStartupDisplayMode>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(extSetStartupDisplayModePatch), true));
//
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer24extSetStartupDisplayModeEP8OSObjectPvP25IOExternalMethodArguments");
//
//                    }
//
                    
                    //  __ZN13IOFramebuffer16extSetPropertiesEP12OSDictionary:        // IOFramebuffer::extSetProperties(OSDictionary*)
                   // using t_extSetProperties = IOReturn (*) (IOFramebuffer * that, OSDictionary * dict );
                    
//                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer16extSetPropertiesEP12OSDictionary");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer16extSetPropertiesEP12OSDictionary");
//                        patcher.clearError();
//                        org_extSetProperties = reinterpret_cast<t_extSetProperties>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(extSetProperties), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer16extSetPropertiesEP12OSDictionary");
//                    }
                    
                    
                    // __ZN13IOFramebuffer16matchFramebufferEv:        // IOFramebuffer::matchFramebuffer()
                    //using t_matchFramebuffer = IOReturn (*) (IOFramebuffer * that);
//                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer16matchFramebufferEv");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer16matchFramebufferEv");
//                        patcher.clearError();
//                        org_matchFramebuffer = reinterpret_cast<t_matchFramebuffer>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(matchFramebuffer), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer16matchFramebufferEv");
//                    }
                    
            
                    
                    
                    //    IOReturn IOFramebuffer::extGetInformationForDisplayMode(
                    //                                                            OSObject * target, void * reference, IOExternalMethodArguments * args)
                    //__ZN13IOFramebuffer31extGetInformationForDisplayModeEP8OSObjectPvP25IOExternalMethodArguments:        // IOFramebuffer::extGetInformationForDisplayMode(OSObject*, void*, IOExternalMethodArguments*)
                    
//                    using t_extGetInformationForDisplayMode = IOReturn (*)(  OSObject * target, void * reference, IOExternalMethodArguments * args );
//                    t_extGetInformationForDisplayMode  org_extGetInformationForDisplayMode{nullptr};
                    
//                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer31extGetInformationForDisplayModeEP8OSObjectPvP25IOExternalMethodArguments");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer31extGetInformationForDisplayModeEP8OSObjectPvP25IOExternalMethodArguments");
//                        patcher.clearError();
//                        org_extGetInformationForDisplayMode = reinterpret_cast<t_extGetInformationForDisplayMode>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(extGetInformationForDisplayMode), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer31extGetInformationForDisplayModeEP8OSObjectPvP25IOExternalMethodArguments");
//                    }
//
     
                    
                    //     __ZN13IOFramebuffer7doSetupEb:        // IOFramebuffer::doSetup(bool)
                  //  using t_doSetup = IOReturn (*)( IONDRVFramebuffer *that,  bool full);

//                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer7doSetupEb");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer7doSetupEb");
//                        patcher.clearError();
//                        org_doSetup = reinterpret_cast<t_doSetup>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(doSetup), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer7doSetupEb");
//                    }
                    
                    
                    
                    progressState |= ProcessingState::NVDAIOGraphicsRouted;
                    
                }
                
                else if (!(progressState & ProcessingState::NVDASupportRouted) && !strcmp(kextList[i].id, kextIONDRVSupportId))
                {
                    DBGLOG("ngfx", "found %s", kextIONDRVSupportId);
                    
//                    auto method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer17setDetailedTimingEijPvy");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer17setDetailedTimingEijPvy");
//                        patcher.clearError();
//                        org_setDetailedTiming = reinterpret_cast<t_setDetailedTiming>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(nvda_setDetailedTiming_patch), true));
//
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer17setDetailedTimingEijPvy");
//
//                    }
//
                    
                    
                    //  __ZN17IONDRVFramebuffer21getStartupDisplayModeEPiS0_
                    
//                    method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer21getStartupDisplayModeEPiS0_");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer21getStartupDisplayModeEPiS0_");
//                        patcher.clearError();
//                        org_getStartupDisplayMode = reinterpret_cast<t_getStartupDisplayMode>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(getStartupDisplayMode), true));
//
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer21getStartupDisplayModeEPiS0_");
//
//                    }
                    
                    
                    //__ZN17IONDRVFramebuffer21setStartupDisplayModeEii
                    
                    
                    auto method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer21setStartupDisplayModeEii");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer21setStartupDisplayModeEii");
                        patcher.clearError();
                        org_setStartupDisplayMode = reinterpret_cast<t_setStartupDisplayMode>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(setStartupDisplayMode), true));
                        
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer21setStartupDisplayModeEii");
                        
                    }
                    
                    //__ZN17IONDRVFramebuffer18setDetailedTimingsEP7OSArray
                    
//                    method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer18setDetailedTimingsEP7OSArray");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer18setDetailedTimingsEP7OSArray");
//                        patcher.clearError();
//                        org_setDetailedTimings = reinterpret_cast<t_setDetailedTimings>(patcher.routeFunction(method_address,
//                                                                                                              reinterpret_cast<mach_vm_address_t>(setDetailedTimings), true));
//
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer18setDetailedTimingsEP7OSArray");
//
//                    }
//
                    
                    // __ZN17IONDRVFramebuffer14setDisplayModeEii:        // IONDRVFramebuffer::setDisplayMode(int, int)
                    
//                    method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer14setDisplayModeEii");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer14setDisplayModeEii");
//                        patcher.clearError();
//                        org_setDisplayMode = reinterpret_cast<t_setDisplayMode>(patcher.routeFunction(method_address,
//                                                                                                              reinterpret_cast<mach_vm_address_t>(setDisplayMode), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer14setDisplayModeEii");
//
//                    }
                    
                    //    __ZN17IONDRVFramebuffer20processConnectChangeEPm:        // IONDRVFramebuffer::processConnectChange(unsigned long*)
                    //using t_processConnectChange= IOReturn (*)(IONDRVFramebuffer *that, uintptr_t * value );
                    
//                    method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer20processConnectChangeEPm");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer20processConnectChangeEPm");
//                        patcher.clearError();
//                        org_processConnectChange = reinterpret_cast<t_processConnectChange>(patcher.routeFunction(method_address,
//                                                                                                      reinterpret_cast<mach_vm_address_t>(processConnectChange), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer20processConnectChangeEPm");
//
//                    }
//
                    
                    
                    //IOReturn IONDRVFramebuffer::validateDisplayMode( IODisplayModeID _mode, IOOptionBits flags, VDDetailedTimingRec ** detailed )
                    //  __ZN17IONDRVFramebuffer19validateDisplayModeEijPP19VDDetailedTimingRec:        // IONDRVFramebuffer::validateDisplayMode(int, unsigned int, VDDetailedTimingRec**)
//                    using t_validateDisplayMode = IOReturn (*)(IONDRVFramebuffer *that, IODisplayModeID _mode, IOOptionBits flags, VDDetailedTimingRec ** detailed );
//                    t_validateDisplayMode  org_validateDisplayMode;

//                    method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer19validateDisplayModeEijPP19VDDetailedTimingRec");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer19validateDisplayModeEijPP19VDDetailedTimingRec");
//                        patcher.clearError();
//                        org_validateDisplayMode = reinterpret_cast<t_validateDisplayMode>(patcher.routeFunction(method_address,
//                                                                                                                  reinterpret_cast<mach_vm_address_t>(validateDisplayMode), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer19validateDisplayModeEijPP19VDDetailedTimingRec");
//
//                    }
//
                    //  __ZN17IONDRVFramebuffer20getResInfoForArbModeEiP24IODisplayModeInformation:        // IONDRVFramebuffer::getResInfoForArbMode(int, IODisplayModeInformation*)
//                    using t_getResInfoForArbMode = IOReturn (*)(IONDRVFramebuffer *that, IODisplayModeID mode, IODisplayModeInformation* info);
//                    t_getResInfoForArbMode  org_getResInfoForArbMode{nullptr};
                    
//                    method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer20getResInfoForArbModeEiP24IODisplayModeInformation");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer20getResInfoForArbModeEiP24IODisplayModeInformation");
//                        patcher.clearError();
//                        org_getResInfoForArbMode = reinterpret_cast<t_getResInfoForArbMode>(patcher.routeFunction(method_address,
//                                                                                                                  reinterpret_cast<mach_vm_address_t>(getResInfoForArbMode), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer20getResInfoForArbModeEiP24IODisplayModeInformation");
//
//                    }
//                    //__ZN17IONDRVFramebuffer17getResInfoForModeEiP24IODisplayModeInformation:        // IONDRVFramebuffer::getResInfoForMode(int, IODisplayModeInformation*)
//
//                    //    IOReturn IONDRVFramebuffer::getResInfoForMode( IODisplayModeID modeID,
//                    //                                                  IODisplayModeInformation * info )
////                    using t_getResInfoForMode = IOReturn (*)(IONDRVFramebuffer *that, IODisplayModeID mode,   IODisplayModeInformation * info );
////                    t_getResInfoForMode  org_getResInfoForMode{nullptr};
//
//                    method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer17getResInfoForModeEiP24IODisplayModeInformation");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer17getResInfoForModeEiP24IODisplayModeInformation");
//                        patcher.clearError();
//                        org_getResInfoForMode = reinterpret_cast<t_getResInfoForMode>(patcher.routeFunction(method_address,
//                                                                                                                  reinterpret_cast<mach_vm_address_t>(getResInfoForMode), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer17getResInfoForModeEiP24IODisplayModeInformation");
//
//                    }
//
//
//                    //__ZN17IONDRVFramebuffer19getPixelInformationEiiiP18IOPixelInformation:        // IONDRVFramebuffer::getPixelInformation(int, int, int, IOPixelInformation*)
////                    using t_getPixelInformation = IOReturn (*)( IONDRVFramebuffer *that,  IODisplayModeID displayMode, IOIndex depth,
////                                                               IOPixelAperture aperture, IOPixelInformation * pixelInfo);
////                    t_getPixelInformation  org_getPixelInformation{nullptr};
//
//                    method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer19getPixelInformationEiiiP18IOPixelInformation");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer19getPixelInformationEiiiP18IOPixelInformation");
//                        patcher.clearError();
//                        org_getPixelInformation = reinterpret_cast<t_getPixelInformation>(patcher.routeFunction(method_address,
//                                                                                                            reinterpret_cast<mach_vm_address_t>(getPixelInformation), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer19getPixelInformationEiiiP18IOPixelInformation");
//
//                    }
                    
                    progressState |= ProcessingState::NVDASupportRouted;
                    
                }
			}
		}
	}
    
	// Ignore all the errors for other processors
	patcher.clearError();
}

void NGFX::nvAccelerator_SetAccelProperties(IOService* that)
{
    DBGLOG("ngfx", "SetAccelProperties is called");
    
    if (callbackNGFX && callbackNGFX->orgSetAccelProperties)
    {
        callbackNGFX->orgSetAccelProperties(that);

        if (!that->getProperty("IOVARendererID"))
        {
			uint8_t rendererId[] {0x08, 0x00, 0x04, 0x01};
            that->setProperty("IOVARendererID", rendererId, sizeof(rendererId));
            DBGLOG("ngfx", "set IOVARendererID to 08 00 04 01");
        }
        
        if (!that->getProperty("IOVARendererSubID"))
        {
			uint8_t rendererSubId[] {0x03, 0x00, 0x00, 0x00};
            that->setProperty("IOVARendererSubID", rendererSubId, sizeof(rendererSubId));
            DBGLOG("ngfx", "set IOVARendererSubID to value 03 00 00 00");
        }
    }
}





bool NGFX::AppleGraphicsDevicePolicy_start(IOService *that, IOService *provider)
{
    bool result = false;
    
    DBGLOG("ngfx", "AppleGraphicsDevicePolicy::start is calledv %s", that->getName());
    if (callbackNGFX && callbackNGFX->orgAgdpStart)
    {
        char board_id[32];
        if (WIOKit::getComputerInfo(nullptr, 0, board_id, sizeof(board_id)))
        {
            DBGLOG("ngfx", "got board-id '%s'", board_id);
            auto dict = that->getPropertyTable();
            auto newProps = OSDynamicCast(OSDictionary, dict->copyCollection());
            OSDictionary *configMap = OSDynamicCast(OSDictionary, newProps->getObject("ConfigMap"));
            if (configMap != nullptr)
            {
                OSString *value = OSDynamicCast(OSString, configMap->getObject(board_id));
                if (value != nullptr)
                    DBGLOG("ngfx", "Current value for board-id '%s' is %s", board_id, value->getCStringNoCopy());
                if (!configMap->setObject(board_id, OSString::withCString("none")))
                    SYSLOG("ngfx", "Configuration for board-id '%s' can't be set, setObject was failed.", board_id);
                else
                    DBGLOG("ngfx", "Configuration for board-id '%s' has been set to none", board_id);
                that->setPropertyTable(newProps);
            }
            else
            {
                SYSLOG("ngfx", "ConfigMap key was not found in personalities");
                OSSafeReleaseNULL(newProps);
            }
        }
        
        result = callbackNGFX->orgAgdpStart(that, provider);
        DBGLOG("ngfx", "AppleGraphicsDevicePolicy::start returned %d", result);
    }
    
    return result;
}

int NGFX::csfg_get_platform_binary(void *fg)
{
    //DBGLOG("ngfx", "csfg_get_platform_binary is called"); // is called quite often
    
    if (callbackNGFX && callbackNGFX->org_csfg_get_platform_binary && callbackNGFX->csfg_get_teamid)
    {
        int result = callbackNGFX->org_csfg_get_platform_binary(fg);
        if (!result)
        {
            // Special case NVIDIA drivers
            const char *teamId = callbackNGFX->csfg_get_teamid(fg);
            if (teamId != nullptr && strcmp(teamId, kNvidiaTeamId) == 0)
            {
                DBGLOG("ngfx", "platform binary override for %s", kNvidiaTeamId);
                return 1;
            }
        }
        
        return result;
    }
    
    // Default to error
    return 0;
}


void NGFX::applyPatches(KernelPatcher &patcher, size_t index, const KextPatch *patches, size_t patchNum, const char* name) {
    DBGLOG("ngfx", "applying patch '%s' for %zu kext", name, index);
    for (size_t p = 0; p < patchNum; p++) {
        auto &patch = patches[p];
        if (patch.patch.kext->loadIndex == index) {
            if (patcher.compatibleKernel(patch.minKernel, patch.maxKernel)) {
                DBGLOG("ngfx", "applying %zu patch for %zu kext", p, index);
                patcher.applyLookupPatch(&patch.patch);
                // Do not really care for the errors for now
                patcher.clearError();
            }
        }
    }
}


void logTimingInfoV2(IOService * that, const char *szPrefix,  IODetailedTimingInformationV2 & v2)
{
    DBGLOG("ngfx", "%s  %s:%s timing: %u x %u  @ %llu hz numLinks: %u  signalLevels: %u signalConfig: %x",
           szPrefix,
           that->getName(),
            that->getProvider()->getName(),
           v2.horizontalActive,
           v2.verticalActive,
           v2.pixelClock,
           v2.numLinks,
           v2.signalLevels,
           v2.signalConfig
           );
}

bool isValidTimev2(IODetailedTimingInformationV2        &v2)

{
    if (
        (v2.horizontalActive == 1920  && v2.verticalActive == 1080)
        ||
        (v2.horizontalActive == 3840  && v2.verticalActive == 2160)
     ||
        (v2.horizontalActive == 1080  && v2.verticalActive == 1920)
   //     ||
//        (v2.horizontalActive == 2160  &&  v2.verticalActive == 3840)
               ||
                (v2.horizontalActive == 5120  &&  v2.verticalActive == 2880)
//                ||
        //        (v2.horizontalActive == 2560  &&  v2.verticalActive == 1440)
           //     ||
            //    (v2.horizontalActive == 2560  &&  v2.verticalActive == 2880)
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



IOReturn NGFX::nvda_validateDetailedTiming_patch(IONDRVFramebuffer *that, void* _desc, IOByteCount descripSize)
{
    if(callbackNGFX && callbackNGFX->org_nda_validateDetailedTiming)
    {
        
        //kIOReturnSuccess
        if (descripSize == sizeof(IOFBDisplayModeDescription))
        {
            IOFBDisplayModeDescription * fbdesc = (IOFBDisplayModeDescription *) _desc;
            
          
            
            //            IOFixed1616 xf = fbdesc->info.refreshRate;
            //            float hz = 0.0f;
            //            hz = *((float *)&xf);
            //
            DBGLOG("ngfx", "validateDetailedTiming %s:%s %u info %u x %u  @ %u.%u hz, maxDepthIndex: %u",that->getName(),   that->getProvider()->getName(),
                   fbdesc->timingInfo.appleTimingID,
                   fbdesc->info.nominalWidth,
                   fbdesc->info.nominalHeight,
                   (fbdesc->info.refreshRate>>16)&0xffff,
                   fbdesc->info.refreshRate&0xffff,
                   fbdesc->info.maxDepthIndex

                   );
           // if (strcmp(that->getProvider()->getName(), "NVDA,Display-C")==0)
            {
                //4096 x 2304
                if(isValidTime(fbdesc->timingInfo)
                   )
                {
//                    fbdesc->timingInfo.detailedInfo.v2.signalConfig = 0;
//                    fbdesc->timingInfo.detailedInfo.v2.signalLevels = 0;
//                    fbdesc->timingInfo.detailedInfo.v2.numLinks = 1;
                    
                    logTimingInfoV2(that, "validateDetailedTiming isValidTiming", fbdesc->timingInfo.detailedInfo.v2);
                }
                else
                {
                    logTimingInfoV2(that, "validateDetailedTiming notvalidTiming", fbdesc->timingInfo.detailedInfo.v2);
                    return kIOReturnUnsupported;
                }
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
            DBGLOG("ngfx", "validateDetailedTiming %s:%s called size %llu unknown  ", that->getName(),   that->getProvider()->getName(),  descripSize);

        }
        
        
        
        IOReturn ret=  callbackNGFX->org_nda_validateDetailedTiming(that,_desc, descripSize);
        DBGLOG("ngfx", "validateDetailedTiming  %s:%s  org_nda_validateDetailedTiming return %x",that->getName(),   that->getProvider()->getName(),  ret);
        return ret;
        
        
    }
    
    DBGLOG("ngfx", "validateDetailedTimin not install ");
    
    return kIOReturnUnsupported;
}


IOReturn NGFX::setStartupDisplayMode (IONDRVFramebuffer *that, IODisplayModeID displayMode, IOIndex depth )
{
    if (callbackNGFX && callbackNGFX->org_setStartupDisplayMode)
    {

        DBGLOG("ngfx", "setStartupDisplayMode %s:%s displayMode %u depth %u called",that->getName(), that->getProvider()->getName(),  displayMode, depth);

        IODisplayModeID curdisplayMode;
        IOIndex curdepth;
        that->getCurrentDisplayMode( &curdisplayMode,  &curdepth);
        
        DBGLOG("ngfx", "setStartupDisplayMode %s:%s  mode %u current mode %u",that->getName(), that->getProvider()->getName(),displayMode,curdisplayMode);
        IOReturn ret =callbackNGFX->org_setStartupDisplayMode(that,displayMode,depth);
        DBGLOG("ngfx", "org_setStartupDisplayMode %s:%s ret %x displayMode %u depth %u",that->getName(),  that->getProvider()->getName(), ret, displayMode, depth);
       // IOSleep(10000);
        //return   that->setDisplayMode(displayMode, depth);
        return ret;
    }
//
    return kIOReturnSuccess;
}


IOReturn NGFX::NVDA_setAttributeForConnection(IONDRVFramebuffer *that , IOIndex connectIndex,
                                               IOSelect attribute, uintptr_t value )
{
    if (callbackNGFX && callbackNGFX->org_NVDA_setAttributeForConnection)
    {
//        DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:%s begin ",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,(const char *)&attribute);
        
        switch(attribute)
        {
     
                
            case kConnectionFlags: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionFlags",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionSyncEnable: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionSyncEnable",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionSyncFlags: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionSyncFlags",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionSupportsAppleSense: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionSupportsAppleSense",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionSupportsLLDDCSense: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionSupportsLLDDCSense",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionSupportsHLDDCSense: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionSupportsHLDDCSense",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionEnable: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionEnable",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionCheckEnable: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionCheckEnable",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionProbe: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionProbe",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionChanged: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionChanged",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionPower: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionPower",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionPostWake: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionPostWake",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionDisplayParameterCount: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionDisplayParameterCount",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionDisplayParameters: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionDisplayParameters",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionOverscan: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionOverscan",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionVideoBest: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionVideoBest",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionRedGammaScale: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionRedGammaScale",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionGreenGammaScale: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionGreenGammaScale",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionBlueGammaScale: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionBlueGammaScale",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionGammaScale: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionGammaScale",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionFlushParameters: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionFlushParameters",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionVBLMultiplier: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionVBLMultiplier",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionHandleDisplayPortEvent:
            {
                uintptr_t evt= value!=0?(*((uintptr_t *)value)):0;
                const char *eventname=(value==0)?"notset":"unknown";
                switch (evt) {
                    case  kIODPEventStart:
                         eventname ="kIODPEventStart";
                           break;
                    case kIODPEventIdle:
                         eventname ="kIODPEventIdle";
                          break;
                    case kIODPEventForceRetrain:
                         eventname ="kIODPEventForceRetrain";
                        break;
                        
                    case kIODPEventRemoteControlCommandPending:
                         eventname ="kIODPEventRemoteControlCommandPending";
                        break;
                    case kIODPEventAutomatedTestRequest:
                         eventname ="kIODPEventAutomatedTestRequest";
                        break;
                    case  kIODPEventContentProtection:
                         eventname ="kIODPEventContentProtection";
                        
                        break;
                    case kIODPEventMCCS:
                         eventname ="kIODPEventMCCS";
                        break;
                    case kIODPEventSinkSpecific:
                         eventname ="kIODPEventSinkSpecific";
                        break;
                    default:
                        break;
                }
                DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionHandleDisplayPortEvent value:%s",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex, eventname);
                
                if(kIODPEventIdle==evt)
                {
                     return kIOReturnSuccess;
                }

            }
                break;
            case kConnectionPanelTimingDisable: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionPanelTimingDisable",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionColorMode: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionColorMode",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionColorModesSupported: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionColorModesSupported",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionColorDepthsSupported: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionColorDepthsSupported",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionControllerDepthsSupported: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionControllerDepthsSupported",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionControllerColorDepth: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionControllerColorDepth",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionControllerDitherControl: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionControllerDitherControl",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionDisplayFlags: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionDisplayFlags",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionEnableAudio: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionEnableAudio",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
            case kConnectionAudioStreaming: DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:kConnectionAudioStreaming",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex);break;
                
        }
        
        IOReturn ret = callbackNGFX->org_NVDA_setAttributeForConnection(that, connectIndex,attribute,value);
        DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s ret %x ",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_getAttributeForConnection(IONDRVFramebuffer *that,IOIndex connectIndex,
                                               IOSelect attribute, uintptr_t * value)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_getAttributeForConnection)
    {
        DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s  attribute:%s  begin ",that->getName(),
               that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",(const char*)&attribute);
        
     
        IOReturn ret = callbackNGFX->org_NVDA_getAttributeForConnection(that,connectIndex,attribute,value);
        
        switch ( attribute)
        {
            case kConnectionFlags: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionFlags value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionSyncEnable: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionSyncEnable value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionSyncFlags: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionSyncFlags value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionSupportsAppleSense: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionSupportsAppleSense value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionSupportsLLDDCSense: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionSupportsLLDDCSense value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionSupportsHLDDCSense: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionSupportsHLDDCSense value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionEnable: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionEnable value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionCheckEnable: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionCheckEnable value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionProbe: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionProbe value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionChanged: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionChanged value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionPower: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionPower value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionPostWake: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionPostWake value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionDisplayParameterCount: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionDisplayParameterCount value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionDisplayParameters: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionDisplayParameters value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionOverscan: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionOverscan value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionVideoBest: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionVideoBest value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionRedGammaScale: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionRedGammaScale value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionGreenGammaScale: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionGreenGammaScale value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionBlueGammaScale: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionBlueGammaScale value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionGammaScale: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionGammaScale value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionFlushParameters: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionFlushParameters value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionVBLMultiplier: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionVBLMultiplier value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionHandleDisplayPortEvent:
                
                DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionHandleDisplayPortEvent value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);
                break;
            case kConnectionPanelTimingDisable: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionPanelTimingDisable value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionColorMode: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionColorMode value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionColorModesSupported: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionColorModesSupported value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionColorDepthsSupported: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionColorDepthsSupported value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionControllerDepthsSupported: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionControllerDepthsSupported value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionControllerColorDepth: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionControllerColorDepth value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionControllerDitherControl: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionControllerDitherControl value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionDisplayFlags: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionDisplayFlags value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionEnableAudio: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionEnableAudio value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            case kConnectionAudioStreaming: DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:kConnectionAudioStreaming value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,value!=NULL?*value:0, ret);break;
            default:
                
                DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s ret %x  index:%u attribute:%s value:%lu",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret, connectIndex,(const char*)&attribute,value!=NULL?*value:0);
                break;
        }

        return ret;
    }
    return kIOReturnSuccess;
}


