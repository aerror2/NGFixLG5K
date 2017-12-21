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

    bzero(displayOnlineStates, sizeof(displayOnlineStates));
    
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
                    
                    method_address = patcher.solveSymbol(index, "__ZN4NVDA5startEP9IOService");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN4NVDA5startEP9IOService");
                        patcher.clearError();
                        org_NVDA_start = reinterpret_cast<t_NVDA_start>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_start), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA5startEP9IOService");
                    }
                    method_address = patcher.solveSymbol(index, "__ZN4NVDA4stopEP9IOService");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN4NVDA4stopEP9IOService");
                        patcher.clearError();
                        org_NVDA_stop = reinterpret_cast<t_NVDA_stop>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_stop), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA4stopEP9IOService");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN4NVDA16enableControllerEv");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN4NVDA16enableControllerEv");
                        patcher.clearError();
                        org_IONDRVFramebuffer_enableController = reinterpret_cast<t_IONDRVFramebuffer_enableController>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IONDRVFramebuffer_enableController), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA16enableControllerEv");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN4NVDA16getApertureRangeEi");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN4NVDA16getApertureRangeEi");
                        patcher.clearError();
                        org_NVDA_getApertureRange = reinterpret_cast<t_NVDA_getApertureRange>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_getApertureRange), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA16getApertureRangeEi");
                    }
                    method_address = patcher.solveSymbol(index, "__ZN4NVDA28getInformationForDisplayModeEiP24IODisplayModeInformation");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN4NVDA28getInformationForDisplayModeEiP24IODisplayModeInformation");
                        patcher.clearError();
                        org_NVDA_getInformationForDisplayMode = reinterpret_cast<t_NVDA_getInformationForDisplayMode>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_getInformationForDisplayMode), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA28getInformationForDisplayModeEiP24IODisplayModeInformation");
                    }
                    method_address = patcher.solveSymbol(index, "__ZN4NVDA12setAttributeEjm");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN4NVDA12setAttributeEjm");
                        patcher.clearError();
                        org_NVDA_setAttribute = reinterpret_cast<t_NVDA_setAttribute>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_setAttribute), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA12setAttributeEjm");
                    }
                    method_address = patcher.solveSymbol(index, "__ZN4NVDA12getAttributeEjPm");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN4NVDA12getAttributeEjPm");
                        patcher.clearError();
                        org_NVDA_getAttribute = reinterpret_cast<t_NVDA_getAttribute>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_getAttribute), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA12getAttributeEjPm");
                    }

                    method_address = patcher.solveSymbol(index, "__ZN4NVDA5probeEP9IOServicePi");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN4NVDA5probeEP9IOServicePi");
                        patcher.clearError();
                        org_NVDA_probe = reinterpret_cast<t_NVDA_probe>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_probe), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA5probeEP9IOServicePi");
                    }
                    
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA25doHotplugInterruptServiceEP8OSObjectP22IOInterruptEventSourcei");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA25doHotplugInterruptServiceEP8OSObjectP22IOInterruptEventSourcei");
//                        patcher.clearError();
//                        org_NVDA_doHotplugInterruptService = reinterpret_cast<t_NVDA_doHotplugInterruptService>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_doHotplugInterruptService), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA25doHotplugInterruptServiceEP8OSObjectP22IOInterruptEventSourcei");
//                    }
//
//
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA27doAudioHDCPInterruptServiceEP8OSObjectP22IOInterruptEventSourcei");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA27doAudioHDCPInterruptServiceEP8OSObjectP22IOInterruptEventSourcei");
//                        patcher.clearError();
//                        org_NVDA_doAudioHDCPInterruptService = reinterpret_cast<t_NVDA_doAudioHDCPInterruptService>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_doAudioHDCPInterruptService), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA27doAudioHDCPInterruptServiceEP8OSObjectP22IOInterruptEventSourcei");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA27doHDCPReadyInterruptServiceEP8OSObjectP22IOInterruptEventSourcei");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA27doHDCPReadyInterruptServiceEP8OSObjectP22IOInterruptEventSourcei");
//                        patcher.clearError();
//                        org_NVDA_doHDCPReadyInterruptService = reinterpret_cast<t_NVDA_doHDCPReadyInterruptService>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_doHDCPReadyInterruptService), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA27doHDCPReadyInterruptServiceEP8OSObjectP22IOInterruptEventSourcei");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA13newUserClientEP4taskPvjPP12IOUserClient");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA13newUserClientEP4taskPvjPP12IOUserClient");
//                        patcher.clearError();
//                        org_NVDA_newUserClient = reinterpret_cast<t_NVDA_newUserClient>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_newUserClient), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA13newUserClientEP4taskPvjPP12IOUserClient");
//                    }

//
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA11setPropertyEPK8OSSymbolP8OSObject");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA11setPropertyEPK8OSSymbolP8OSObject");
//                        patcher.clearError();
//                        org_NVDA_setProperty = reinterpret_cast<t_NVDA_setProperty>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_setProperty), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA11setPropertyEPK8OSSymbolP8OSObject");
//                    }
                    
                    
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA18doHotPlugInterruptEv");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA18doHotPlugInterruptEv");
//                        patcher.clearError();
//                        org_NVDA_doHotPlugInterrupt = reinterpret_cast<t_NVDA_doHotPlugInterrupt>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_doHotPlugInterrupt), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA18doHotPlugInterruptEv");
//                    }
//
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA14doVBLInterruptEv");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA14doVBLInterruptEv");
//                        patcher.clearError();
//                        org_NVDA_doVBLInterrupt = reinterpret_cast<t_NVDA_doVBLInterrupt>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_doVBLInterrupt), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA14doVBLInterruptEv");
//                    }
                    
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA14DevTypeDisplayEP15IORegistryEntry");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA14DevTypeDisplayEP15IORegistryEntry");
//                        patcher.clearError();
//                        org_NVDA_DevTypeDisplay = reinterpret_cast<t_NVDA_DevTypeDisplay>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_DevTypeDisplay), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA14DevTypeDisplayEP15IORegistryEntry");
//                    }

//
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA8glkCloseEP9IOService");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA8glkCloseEP9IOService");
//                        patcher.clearError();
//                        org_NVDA_glkClose = reinterpret_cast<t_NVDA_glkClose>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_glkClose), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA8glkCloseEP9IOService");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA8ScanListEP9IOService");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA8ScanListEP9IOService");
//                        patcher.clearError();
//                        org_NVDA_ScanList = reinterpret_cast<t_NVDA_ScanList>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_ScanList), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA8ScanListEP9IOService");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA10InsertNodeEP9IOService");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA10InsertNodeEP9IOService");
//                        patcher.clearError();
//                        org_NVDA_InsertNode = reinterpret_cast<t_NVDA_InsertNode>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_InsertNode), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA10InsertNodeEP9IOService");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA12initAppleMuxEP15IORegistryEntry");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA12initAppleMuxEP15IORegistryEntry");
//                        patcher.clearError();
//                        org_NVDA_initAppleMux = reinterpret_cast<t_NVDA_initAppleMux>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_initAppleMux), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA12initAppleMuxEP15IORegistryEntry");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA11setAppleMuxEj");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA11setAppleMuxEj");
//                        patcher.clearError();
//                        org_NVDA_setAppleMux = reinterpret_cast<t_NVDA_setAppleMux>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_setAppleMux), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA11setAppleMuxEj");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA7rmStartEP9IOServicej");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA7rmStartEP9IOServicej");
//                        patcher.clearError();
//                        org_NVDA_rmStart = reinterpret_cast<t_NVDA_rmStart>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_rmStart), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA7rmStartEP9IOServicej");
//                    }

                    
                    
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA7glkOpenEP9IOService");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA7glkOpenEP9IOService");
//                        patcher.clearError();
//                        org_NVDA_glkOpen = reinterpret_cast<t_NVDA_glkOpen>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_glkOpen), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA7glkOpenEP9IOService");
//                    }
                    
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA36getNVAcceleratorPropertyFromRegistryEPKcPj");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA36getNVAcceleratorPropertyFromRegistryEPKcPj");
//                        patcher.clearError();
//                        org_NVDA_getNVAcceleratorPropertyFromRegistry = reinterpret_cast<t_NVDA_getNVAcceleratorPropertyFromRegistry>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_getNVAcceleratorPropertyFromRegistry), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA36getNVAcceleratorPropertyFromRegistryEPKcPj");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA20callPlatformFunctionEPK8OSSymbolbPvS3_S3_S3_");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA20callPlatformFunctionEPK8OSSymbolbPvS3_S3_S3_");
//                        patcher.clearError();
//                        org_NVDA_callPlatformFunction = reinterpret_cast<t_NVDA_callPlatformFunction>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_callPlatformFunction), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA20callPlatformFunctionEPK8OSSymbolbPvS3_S3_S3_");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA21mergeDevicePropertiesEPKcS1_");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA21mergeDevicePropertiesEPKcS1_");
//                        patcher.clearError();
//                        org_NVDA_mergeDeviceProperties = reinterpret_cast<t_NVDA_mergeDeviceProperties>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_mergeDeviceProperties), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA21mergeDevicePropertiesEPKcS1_");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA33doGlkWindowServerTransitionNotifyEjjPjj");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA33doGlkWindowServerTransitionNotifyEjjPjj");
//                        patcher.clearError();
//                        org_NVDA_doGlkWindowServerTransitionNotify = reinterpret_cast<t_NVDA_doGlkWindowServerTransitionNotify>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_doGlkWindowServerTransitionNotify), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA33doGlkWindowServerTransitionNotifyEjjPjj");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA13setGammaTableEjjjPv");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA13setGammaTableEjjjPv");
//                        patcher.clearError();
//                        org_NVDA_setGammaTable = reinterpret_cast<t_NVDA_setGammaTable>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_setGammaTable), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA13setGammaTableEjjjPv");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA18setCLUTWithEntriesEP12IOColorEntryjjj");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA18setCLUTWithEntriesEP12IOColorEntryjjj");
//                        patcher.clearError();
//                        org_NVDA_setCLUTWithEntries = reinterpret_cast<t_NVDA_setCLUTWithEntries>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_setCLUTWithEntries), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA18setCLUTWithEntriesEP12IOColorEntryjjj");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA22undefinedSymbolHandlerEPKcS1_");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA22undefinedSymbolHandlerEPKcS1_");
//                        patcher.clearError();
//                        org_NVDA_undefinedSymbolHandler = reinterpret_cast<t_NVDA_undefinedSymbolHandler>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_undefinedSymbolHandler), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA22undefinedSymbolHandlerEPKcS1_");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA14copyACPIDeviceEP15IORegistryEntry");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA14copyACPIDeviceEP15IORegistryEntry");
//                        patcher.clearError();
//                        org_NVDA_copyACPIDevice = reinterpret_cast<t_NVDA_copyACPIDevice>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_copyACPIDevice), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA14copyACPIDeviceEP15IORegistryEntry");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA21findAppleMuxSubdeviceEP15IORegistryEntryj");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA21findAppleMuxSubdeviceEP15IORegistryEntryj");
//                        patcher.clearError();
//                        org_NVDA_findAppleMuxSubdevice = reinterpret_cast<t_NVDA_findAppleMuxSubdevice>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_findAppleMuxSubdevice), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA21findAppleMuxSubdeviceEP15IORegistryEntryj");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA20setAppleMuxSubdeviceEP20IOACPIPlatformDevicej");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA20setAppleMuxSubdeviceEP20IOACPIPlatformDevicej");
//                        patcher.clearError();
//                        org_NVDA_setAppleMuxSubdevice = reinterpret_cast<t_NVDA_setAppleMuxSubdevice>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_setAppleMuxSubdevice), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA20setAppleMuxSubdeviceEP20IOACPIPlatformDevicej");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA11getAppleMuxEv");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA11getAppleMuxEv");
//                        patcher.clearError();
//                        org_NVDA_getAppleMux = reinterpret_cast<t_NVDA_getAppleMux>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_getAppleMux), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA11getAppleMuxEv");
//                    }

//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA10addIntrRegEjPv");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA10addIntrRegEjPv");
//                        patcher.clearError();
//                        org_NVDA_addIntrReg = reinterpret_cast<t_NVDA_addIntrReg>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_addIntrReg), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA10addIntrRegEjPv");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA13removeIntrRegEPv");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA13removeIntrRegEPv");
//                        patcher.clearError();
//                        org_NVDA_removeIntrReg = reinterpret_cast<t_NVDA_removeIntrReg>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_removeIntrReg), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA13removeIntrRegEPv");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA11findIntrRegEPv");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA11findIntrRegEPv");
//                        patcher.clearError();
//                        org_NVDA_findIntrReg = reinterpret_cast<t_NVDA_findIntrReg>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_findIntrReg), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA11findIntrRegEPv");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA24registerForInterruptTypeEjPFvP8OSObjectPvES1_S2_PS2_");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA24registerForInterruptTypeEjPFvP8OSObjectPvES1_S2_PS2_");
//                        patcher.clearError();
//                        org_NVDA_registerForInterruptType = reinterpret_cast<t_NVDA_registerForInterruptType>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_registerForInterruptType), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA24registerForInterruptTypeEjPFvP8OSObjectPvES1_S2_PS2_");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA19unregisterInterruptEPv");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA19unregisterInterruptEPv");
//                        patcher.clearError();
//                        org_NVDA_unregisterInterrupt = reinterpret_cast<t_NVDA_unregisterInterrupt>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_unregisterInterrupt), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA19unregisterInterruptEPv");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA17setInterruptStateEPvj");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA17setInterruptStateEPvj");
//                        patcher.clearError();
//                        org_NVDA_setInterruptState = reinterpret_cast<t_NVDA_setInterruptState>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_setInterruptState), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA17setInterruptStateEPvj");
//                    }
                 
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA7messageEjP9IOServicePv");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA7messageEjP9IOServicePv");
//                        patcher.clearError();
//                        org_NVDA_message = reinterpret_cast<t_NVDA_message>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_message), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA7messageEjP9IOServicePv");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA13createNVDCNubEj");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA13createNVDCNubEj");
//                        patcher.clearError();
//                        org_NVDA_createNVDCNub = reinterpret_cast<t_NVDA_createNVDCNub>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_createNVDCNub), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA13createNVDCNubEj");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA24get10BPCFormatGLKSupportEPh");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA24get10BPCFormatGLKSupportEPh");
//                        patcher.clearError();
//                        org_NVDA_get10BPCFormatGLKSupport = reinterpret_cast<t_NVDA_get10BPCFormatGLKSupport>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_get10BPCFormatGLKSupport), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA24get10BPCFormatGLKSupportEPh");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA32getIOPCIDeviceFromDeviceInstanceEjPP11IOPCIDevice");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA32getIOPCIDeviceFromDeviceInstanceEjPP11IOPCIDevice");
//                        patcher.clearError();
//                        org_NVDA_getIOPCIDeviceFromDeviceInstance = reinterpret_cast<t_NVDA_getIOPCIDeviceFromDeviceInstance>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_getIOPCIDeviceFromDeviceInstance), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA32getIOPCIDeviceFromDeviceInstanceEjPP11IOPCIDevice");
//                    }
//                    method_address = patcher.solveSymbol(index, "__ZN4NVDA34getIOPCIDevicePropertyFromRegistryEPKcPt");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN4NVDA34getIOPCIDevicePropertyFromRegistryEPKcPt");
//                        patcher.clearError();
//                        org_NVDA_getIOPCIDevicePropertyFromRegistry = reinterpret_cast<t_NVDA_getIOPCIDevicePropertyFromRegistry>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(NVDA_getIOPCIDevicePropertyFromRegistry), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN4NVDA34getIOPCIDevicePropertyFromRegistryEPKcPt");
//                    }
//                    
                    progressState |= ProcessingState::NVDAResmanRouted;

                }
                
                else if (!(progressState & ProcessingState::NVDAIOGraphicsRouted) && !strcmp(kextList[i].id, kextIOGraphicsFamilyId))
                {
                    DBGLOG("ngfx", "found %s", kextIOGraphicsFamilyId);

                    
                    auto method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer7doSetupEb");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer7doSetupEb");
                        patcher.clearError();
                        org_IOFramebuffer_doSetup = reinterpret_cast<t_IOFramebuffer_doSetup>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_doSetup), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer7doSetupEb");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer14checkPowerWorkEj");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer14checkPowerWorkEj");
                        patcher.clearError();
                        org_IOFramebuffer_checkPowerWork = reinterpret_cast<t_IOFramebuffer_checkPowerWork>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_checkPowerWork), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer14checkPowerWorkEj");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer18dpProcessInterruptEv");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer18dpProcessInterruptEv");
                        patcher.clearError();
                        org_IOFramebuffer_dpProcessInterrupt = reinterpret_cast<t_IOFramebuffer_dpProcessInterrupt>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_dpProcessInterrupt), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer18dpProcessInterruptEv");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer4openEv");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer4openEv");
                        patcher.clearError();
                        org_IOFramebuffer_open = reinterpret_cast<t_IOFramebuffer_open>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_open), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer4openEv");
                    }
                    
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer20processConnectChangeEj");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer20processConnectChangeEj");
                        patcher.clearError();
                        org_IOFramebuffer_processConnectChange = reinterpret_cast<t_IOFramebuffer_processConnectChange>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_processConnectChange), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer20processConnectChangeEj");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer16extSetPropertiesEP12OSDictionary");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer16extSetPropertiesEP12OSDictionary");
                        patcher.clearError();
                        org_IOFramebuffer_extSetProperties = reinterpret_cast<t_IOFramebuffer_extSetProperties>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_extSetProperties), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer16extSetPropertiesEP12OSDictionary");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer24extSetStartupDisplayModeEP8OSObjectPvP25IOExternalMethodArguments");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer24extSetStartupDisplayModeEP8OSObjectPvP25IOExternalMethodArguments");
                        patcher.clearError();
                        org_IOFramebuffer_extSetStartupDisplayMode = reinterpret_cast<t_IOFramebuffer_extSetStartupDisplayMode>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_extSetStartupDisplayMode), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer24extSetStartupDisplayModeEP8OSObjectPvP25IOExternalMethodArguments");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer8postWakeEv");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer8postWakeEv");
                        patcher.clearError();
                        org_IOFramebuffer_postWake = reinterpret_cast<t_IOFramebuffer_postWake>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_postWake), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer8postWakeEv");
                    }
                    
                    
                    method_address = patcher.solveSymbol(index, "__ZN17IODisplayWrangler14activityChangeEP13IOFramebuffer");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN17IODisplayWrangler14activityChangeEP13IOFramebuffer");
                        patcher.clearError();
                        org_IODisplayWrangler_activityChange = reinterpret_cast<t_IODisplayWrangler_activityChange>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IODisplayWrangler_activityChange), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN17IODisplayWrangler14activityChangeEP13IOFramebuffer");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer15muxPowerMessageEj:");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer15muxPowerMessageEj:");
                        patcher.clearError();
                        org_IOFramebuffer_muxPowerMessage = reinterpret_cast<t_IOFramebuffer_muxPowerMessage>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_muxPowerMessage), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer15muxPowerMessageEj:");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer11setCapturedEb");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer11setCapturedEb");
                        patcher.clearError();
                        org_IOFramebuffer_setCaptured = reinterpret_cast<t_IOFramebuffer_setCaptured>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_setCaptured), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer11setCapturedEb");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer13setDimDisableEb");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer13setDimDisableEb");
                        patcher.clearError();
                        org_IOFramebuffer_setDimDisable = reinterpret_cast<t_IOFramebuffer_setDimDisable>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_setDimDisable), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer13setDimDisableEb");
                    }
                    
                    
//                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer10systemWorkEP8OSObjectP22IOInterruptEventSourcei");
//                    if (method_address) {
//                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer10systemWorkEP8OSObjectP22IOInterruptEventSourcei");
//                        patcher.clearError();
//                        org_IOFramebuffer_systemWork = reinterpret_cast<t_IOFramebuffer_systemWork>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_systemWork), true));
//                    }
//                    else
//                    {
//                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer10systemWorkEP8OSObjectP22IOInterruptEventSourcei");
//                    }

                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer12updateOnlineEv");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer12updateOnlineEv");
                        patcher.clearError();
                        org_IOFramebuffer_updateOnline = reinterpret_cast<t_IOFramebuffer_updateOnline>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_updateOnline), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer12updateOnlineEv");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer12notifyServerEh");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer12notifyServerEh");
                        patcher.clearError();
                        org_IOFramebuffer_notifyServer = reinterpret_cast<t_IOFramebuffer_notifyServer>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_notifyServer), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer12notifyServerEh");
                    }
                    
                    
                    
                    method_address = patcher.solveSymbol(index, "__ZN14IOFBController19checkConnectionWorkEj");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN14IOFBController19checkConnectionWorkEj");
                        patcher.clearError();
                        org_IOFBController_checkConnectionWork = reinterpret_cast<t_IOFBController_checkConnectionWork>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFBController_checkConnectionWork), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN14IOFBController19checkConnectionWorkEj");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN9IODisplay20setDisplayPowerStateEm");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN9IODisplay20setDisplayPowerStateEm");
                        patcher.clearError();
                        org_IODisplay_setDisplayPowerState = reinterpret_cast<t_IODisplay_setDisplayPowerState>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IODisplay_setDisplayPowerState), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN9IODisplay20setDisplayPowerStateEm");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer30deliverFramebufferNotificationEiPv");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer30deliverFramebufferNotificationEiPv");
                        patcher.clearError();
                        org_IOFramebuffer_deliverFramebufferNotification = reinterpret_cast<t_IOFramebuffer_deliverFramebufferNotification>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_deliverFramebufferNotification), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer30deliverFramebufferNotificationEiPv");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN13IOFramebuffer7suspendEb");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN13IOFramebuffer7suspendEb");
                        patcher.clearError();
                        org_IOFramebuffer_suspend = reinterpret_cast<t_IOFramebuffer_suspend>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IOFramebuffer_suspend), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN13IOFramebuffer7suspendEb");
                    }
                    
                    
                    progressState |= ProcessingState::NVDAIOGraphicsRouted;
                    
                }
                
                else if (!(progressState & ProcessingState::NVDASupportRouted) && !strcmp(kextList[i].id, kextIONDRVSupportId))
                {
                    DBGLOG("ngfx", "found %s", kextIONDRVSupportId);
                    auto method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer20processConnectChangeEPm");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer20processConnectChangeEPm");
                        patcher.clearError();
                        org_IONDRVFramebuffer_processConnectChange = reinterpret_cast<t_IONDRVFramebuffer_processConnectChange>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IONDRVFramebuffer_processConnectChange), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer20processConnectChangeEPm");
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer21setStartupDisplayModeEii");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer21setStartupDisplayModeEii");
                        patcher.clearError();
                        org_setStartupDisplayMode = reinterpret_cast<t_setStartupDisplayMode>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(setStartupDisplayMode), true));
                        
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer21setStartupDisplayModeEii");
                        
                    }
                    
                    method_address = patcher.solveSymbol(index, "__ZN17IONDRVFramebuffer14setDisplayModeEii");
                    if (method_address) {
                        DBGLOG("ngfx", "obtained __ZN17IONDRVFramebuffer14setDisplayModeEii");
                        patcher.clearError();
                        org_IONDRVFramebuffer_setDisplayMode = reinterpret_cast<t_IONDRVFramebuffer_setDisplayMode>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>(IONDRVFramebuffer_setDisplayMode), true));
                    }
                    else
                    {
                        SYSLOG("ngfx", "failed to resolve __ZN17IONDRVFramebuffer14setDisplayModeEii");
                    }
                    
                    
           
                    
                    
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
//               ||
//               (v2.horizontalActive == 2560  &&  v2.verticalActive == 1440)
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


const char * getIOAtrributeName(IOSelect &attribute, char *sztmp)
{
    switch(attribute)
    {
        case kConnectionFlags: return "kConnectionFlags"; break;
        case kConnectionSyncEnable: return "kConnectionSyncEnable"; break;
        case kConnectionSyncFlags: return "kConnectionSyncFlags"; break;
        case kConnectionSupportsAppleSense: return "kConnectionSupportsAppleSense"; break;
        case kConnectionSupportsLLDDCSense: return "kConnectionSupportsLLDDCSense"; break;
        case kConnectionSupportsHLDDCSense: return "kConnectionSupportsHLDDCSense"; break;
        case kConnectionEnable: return "kConnectionEnable"; break;
        case kConnectionCheckEnable: return "kConnectionCheckEnable"; break;
        case kConnectionProbe: return "kConnectionProbe"; break;
        case kConnectionChanged: return "kConnectionChanged"; break;
        case kConnectionPower: return "kConnectionPower"; break;
        case kConnectionPostWake: return "kConnectionPostWake"; break;
        case kConnectionDisplayParameterCount: return "kConnectionDisplayParameterCount"; break;
        case kConnectionDisplayParameters: return "kConnectionDisplayParameters"; break;
        case kConnectionOverscan: return "kConnectionOverscan"; break;
        case kConnectionVideoBest: return "kConnectionVideoBest"; break;
        case kConnectionRedGammaScale: return "kConnectionRedGammaScale"; break;
        case kConnectionGreenGammaScale: return "kConnectionGreenGammaScale"; break;
        case kConnectionBlueGammaScale: return "kConnectionBlueGammaScale"; break;
        case kConnectionGammaScale: return "kConnectionGammaScale"; break;
        case kConnectionFlushParameters: return "kConnectionFlushParameters"; break;
        case kConnectionVBLMultiplier: return "kConnectionVBLMultiplier"; break;
        case kConnectionHandleDisplayPortEvent: return "kConnectionHandleDisplayPortEvent"; break;
        case kConnectionPanelTimingDisable: return "kConnectionPanelTimingDisable"; break;
        case kConnectionColorMode: return "kConnectionColorMode"; break;
        case kConnectionColorModesSupported: return "kConnectionColorModesSupported"; break;
        case kConnectionColorDepthsSupported: return "kConnectionColorDepthsSupported"; break;
        case kConnectionControllerDepthsSupported: return "kConnectionControllerDepthsSupported"; break;
        case kConnectionControllerColorDepth: return "kConnectionControllerColorDepth"; break;
        case kConnectionControllerDitherControl: return "kConnectionControllerDitherControl"; break;
        case kConnectionDisplayFlags: return "kConnectionDisplayFlags"; break;
        case kConnectionEnableAudio: return "kConnectionEnableAudio"; break;
        case kConnectionAudioStreaming: return "kConnectionAudioStreaming"; break;
        default:
        {
            sztmp[0] = attribute >>24 & 0xff;
             sztmp[1] = attribute >>16 & 0xff;
             sztmp[2] = attribute >>8 & 0xff;
             sztmp[3] = attribute & 0xff;
            sztmp[4] = 0;
            snprintf(sztmp+4, 20, "(%X)",attribute);
            return sztmp;
        }
    }
    return NULL;
}

const char *getDisplayPortEventName(uintptr_t evt)
{
    switch (evt) {
        case kIODPEventStart: return "kIODPEventStart=1"; break;
        case kIODPEventIdle: return "kIODPEventIdle=2"; break;
        case kIODPEventForceRetrain: return "kIODPEventForceRetrain=3"; break;
        case kIODPEventRemoteControlCommandPending: return "kIODPEventRemoteControlCommandPending=256"; break;
        case kIODPEventAutomatedTestRequest: return "kIODPEventAutomatedTestRequest=257"; break;
        case kIODPEventContentProtection: return "kIODPEventContentProtection=258"; break;
        case kIODPEventMCCS: return "kIODPEventMCCS=259"; break;
        case kIODPEventSinkSpecific: return "kIODPEventSinkSpecific=260"; break;
            
        default:
            return "notknow";
            break;
    }
    
    return NULL;
}
IOReturn NGFX::NVDA_setAttributeForConnection(IONDRVFramebuffer *that , IOIndex connectIndex,
                                               IOSelect attribute, uintptr_t value )
{
    if (callbackNGFX && callbackNGFX->org_NVDA_setAttributeForConnection)
    {
        char sztmp[30];
        const char *attributeName =  getIOAtrributeName(attribute,sztmp);
        if(attribute == kConnectionHandleDisplayPortEvent)
        {
            uintptr_t evt= value!=0?(*((uintptr_t *)value)):0;
            const char *eventname =getDisplayPortEventName(evt);
            
            DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:%s value:%s",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,attributeName, eventname);
            
            if(value!=0 && evt != kIODPEventStart)
            {
                DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:%s value:%s end SKIP IT",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,attributeName, eventname);
                *((uintptr_t *)value) = kIODPEventStart;
                //return kIOReturnSuccess;
            }
        }
        else
        {
             DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:%s",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,attributeName);
        }
        
        IOReturn ret = callbackNGFX->org_NVDA_setAttributeForConnection(that, connectIndex,attribute,value);
        DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:%s ret %x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,attributeName,ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_getAttributeForConnection(IONDRVFramebuffer *that,IOIndex connectIndex,
                                               IOSelect attribute, uintptr_t * value)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_getAttributeForConnection)
    {
        char sztmp[30];

         const char *attributeName =  getIOAtrributeName(attribute,sztmp);
        
        DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s  attribute:%s  begin ",that->getName(),
               that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",attributeName);
        
     
        IOReturn ret = callbackNGFX->org_NVDA_getAttributeForConnection(that,connectIndex,attribute,value);
        
        if( kConnectionHandleDisplayPortEvent == attribute)
        {
           
            DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:%s value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,attributeName, value!=NULL?*value:0, ret);
        }
        else
        {
            DBGLOG("ngfx", "NVDA_getAttributeForConnection %s:%s index:%u attribute:%s value:%lu ret:%x",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,attributeName, value!=NULL?*value:0, ret);
        }


        return ret;
    }
    return kIOReturnSuccess;
}


bool NGFX::NVDA_start(IONDRVFramebuffer *that,IOService * provider)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_start)
    {
        DBGLOG("ngfx", "NVDA_start %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        bool ret = callbackNGFX->org_NVDA_start(that, provider);
        DBGLOG("ngfx", "NVDA_start %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
void NGFX::NVDA_stop(IONDRVFramebuffer *that,IOService * provider )
{
    if (callbackNGFX && callbackNGFX->org_NVDA_stop)
    {
        DBGLOG("ngfx", "NVDA_stop %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
         callbackNGFX->org_NVDA_stop(that,provider);
        DBGLOG("ngfx", "NVDA_stop %s:%s ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
    }
  //  return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_glkClose(IONDRVFramebuffer *that,IOService* svc)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_glkClose)
    {
        DBGLOG("ngfx", "NVDA_glkClose %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_glkClose(that,svc);
        DBGLOG("ngfx", "NVDA_glkClose %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::IONDRVFramebuffer_enableController(IONDRVFramebuffer *that)
{
    if (callbackNGFX && callbackNGFX->org_IONDRVFramebuffer_enableController)
    {
        DBGLOG("ngfx", "IONDRVFramebuffer_enableController %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_IONDRVFramebuffer_enableController(that);
        DBGLOG("ngfx", "IONDRVFramebuffer_enableController %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}


IOReturn NGFX::NVDA_ScanList(IONDRVFramebuffer *that,IOService* svc)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_ScanList)
    {
        DBGLOG("ngfx", "NVDA_ScanList %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_ScanList(that,svc);
        DBGLOG("ngfx", "NVDA_ScanList %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_InsertNode(IONDRVFramebuffer *that,IOService* svc)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_InsertNode)
    {
        DBGLOG("ngfx", "NVDA_InsertNode %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_InsertNode(that,svc);
        DBGLOG("ngfx", "NVDA_InsertNode %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_DevTypeDisplay(IONDRVFramebuffer *that,IORegistryEntry* preg)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_DevTypeDisplay)
    {
        DBGLOG("ngfx", "NVDA_DevTypeDisplay %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_DevTypeDisplay(that,preg);
        DBGLOG("ngfx", "NVDA_DevTypeDisplay %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_initAppleMux(IONDRVFramebuffer *that,IORegistryEntry* preg)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_initAppleMux)
    {
        DBGLOG("ngfx", "NVDA_initAppleMux %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_initAppleMux(that,preg);
        DBGLOG("ngfx", "NVDA_initAppleMux %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_setAppleMux(IONDRVFramebuffer *that,unsigned int p1)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_setAppleMux)
    {
        DBGLOG("ngfx", "NVDA_setAppleMux %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_setAppleMux(that,p1);
        DBGLOG("ngfx", "NVDA_setAppleMux %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_rmStart(IONDRVFramebuffer *that,IOService* svc, unsigned int p1)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_rmStart)
    {
        DBGLOG("ngfx", "NVDA_rmStart %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_rmStart(that,svc,p1);
        DBGLOG("ngfx", "NVDA_rmStart %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
void NGFX::NVDA_doHotplugInterruptService(OSObject * owner,IOInterruptEventSource * evtSrc, int intCount)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_doHotplugInterruptService)
    {
        IOFramebuffer * that = (IOFramebuffer *) owner;

        DBGLOG("ngfx", "NVDA_doHotplugInterruptService %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
         callbackNGFX->org_NVDA_doHotplugInterruptService(owner, evtSrc, intCount);
        DBGLOG("ngfx", "NVDA_doHotplugInterruptService %s:%s ret ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
    }
}
void NGFX::NVDA_doAudioHDCPInterruptService(OSObject * owner,IOInterruptEventSource * evtSrc, int intCount)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_doAudioHDCPInterruptService)
    {
           IOFramebuffer * that = (IOFramebuffer *) owner;
        DBGLOG("ngfx", "NVDA_doAudioHDCPInterruptService %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        callbackNGFX->org_NVDA_doAudioHDCPInterruptService(owner, evtSrc, intCount);
        DBGLOG("ngfx", "NVDA_doAudioHDCPInterruptService %s:%s ret ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
    }
}
void NGFX::NVDA_doHDCPReadyInterruptService(OSObject * owner,IOInterruptEventSource * evtSrc, int intCount)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_doHDCPReadyInterruptService)
    {
           IOFramebuffer * that = (IOFramebuffer *) owner;
        DBGLOG("ngfx", "NVDA_doHDCPReadyInterruptService %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        callbackNGFX->org_NVDA_doHDCPReadyInterruptService(owner, evtSrc, intCount);
        DBGLOG("ngfx", "NVDA_doHDCPReadyInterruptService %s:%s ret",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
    }
}
IOReturn NGFX::NVDA_glkOpen(IONDRVFramebuffer *that,IOService* svc)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_glkOpen)
    {
        
        DBGLOG("ngfx", "NVDA_glkOpen %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_glkOpen(that,svc);
        DBGLOG("ngfx", "NVDA_glkOpen %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_getNVAcceleratorPropertyFromRegistry(IONDRVFramebuffer *that,char const* p1, unsigned int* p2)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_getNVAcceleratorPropertyFromRegistry)
    {
        DBGLOG("ngfx", "NVDA_getNVAcceleratorPropertyFromRegistry %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_getNVAcceleratorPropertyFromRegistry(that,p1,p2);
        DBGLOG("ngfx", "NVDA_getNVAcceleratorPropertyFromRegistry %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_callPlatformFunction(IONDRVFramebuffer *that,OSSymbol const* p1, bool p2, void* p3, void* p4, void* p5, void* p6)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_callPlatformFunction)
    {
        DBGLOG("ngfx", "NVDA_callPlatformFunction %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_callPlatformFunction(that,p1,p2,p3,p4,p5,p6);
        DBGLOG("ngfx", "NVDA_callPlatformFunction %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_mergeDeviceProperties(IONDRVFramebuffer *that,char const* p1, char const* p2)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_mergeDeviceProperties)
    {
        DBGLOG("ngfx", "NVDA_mergeDeviceProperties %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_mergeDeviceProperties(that, p1,p2);
        DBGLOG("ngfx", "NVDA_mergeDeviceProperties %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IODeviceMemory *  NGFX::NVDA_getApertureRange(IONDRVFramebuffer *that,IOPixelAperture aperture)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_getApertureRange)
    {
        DBGLOG("ngfx", "NVDA_getApertureRange %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IODeviceMemory * ret  = callbackNGFX->org_NVDA_getApertureRange(that, aperture);
        DBGLOG("ngfx", "NVDA_getApertureRange %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver", ret!=NULL?1:0);
        return ret;
    }
    return NULL;
}
IOReturn NGFX::NVDA_getInformationForDisplayMode(IONDRVFramebuffer *that,IODisplayModeID displayMode,
                                                 IODisplayModeInformation * info )
{
    if (callbackNGFX && callbackNGFX->org_NVDA_getInformationForDisplayMode)
    {
        DBGLOG("ngfx", "NVDA_getInformationForDisplayMode %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_getInformationForDisplayMode(that,displayMode, info);
        DBGLOG("ngfx", "NVDA_getInformationForDisplayMode %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}

const char * getControllerAttriuteName(IOSelect attribute, char *sztmp)
{
    
    switch (attribute) {
        case kIOPowerStateAttribute: return "kIOPowerStateAttribute";
        case kIOPowerAttribute: return "kIOPowerAttribute";
        case kIODriverPowerAttribute: return "kIODriverPowerAttribute";
        case kIOHardwareCursorAttribute: return "kIOHardwareCursorAttribute";
        case kIOMirrorAttribute: return "kIOMirrorAttribute";
        case kIOMirrorDefaultAttribute: return "kIOMirrorDefaultAttribute";
        case kIOCapturedAttribute: return "kIOCapturedAttribute";
        case kIOCursorControlAttribute: return "kIOCursorControlAttribute";
        case kIOSystemPowerAttribute: return "kIOSystemPowerAttribute";
        case kIOWindowServerActiveAttribute: return "kIOWindowServerActiveAttribute";
        case kIOVRAMSaveAttribute: return "kIOVRAMSaveAttribute";
        case kIODeferCLUTSetAttribute: return "kIODeferCLUTSetAttribute";
        case kIOClamshellStateAttribute: return "kIOClamshellStateAttribute";
        case kIOFBDisplayPortTrainingAttribute: return "kIOFBDisplayPortTrainingAttribute";
        case kIOFBDisplayState: return "kIOFBDisplayState";
        case kIOFBVariableRefreshRate: return "kIOFBVariableRefreshRate";
        case kIOFBLimitHDCPAttribute: return "kIOFBLimitHDCPAttribute";
        case kIOFBLimitHDCPStateAttribute: return "kIOFBLimitHDCPStateAttribute";
        case kIOFBStop: return "kIOFBStop";
        case kIOFBRedGammaScaleAttribute: return "kIOFBRedGammaScaleAttribute";
        case kIOFBGreenGammaScaleAttribute: return "kIOFBGreenGammaScaleAttribute";
        case kIOFBBlueGammaScaleAttribute: return "kIOFBBlueGammaScaleAttribute";
        default:
            sztmp[0] = attribute >>24 & 0xff;
            sztmp[1] = attribute >>16 & 0xff;
            sztmp[2] = attribute >>8 & 0xff;
            sztmp[3] = attribute & 0xff;
            sztmp[4] = 0;
            snprintf(sztmp+4, 20, "(%X)",attribute);
            return sztmp;
    }
    
    return "unknown";
}

int bitoffsetByDisplayName(const char *szDisplayName)
{
    if(strcmp(szDisplayName,"NVDA,Display-A")==0)
    {
        return 0;
    }
    else if(strcmp(szDisplayName,"NVDA,Display-B")==0)
    {
        return 1;
    }
    else if(strcmp(szDisplayName,"NVDA,Display-C")==0)
    {
        return 2;
    }
    else if(strcmp(szDisplayName,"NVDA,Display-D")==0)
    {
        return 3;
    }
    else if(strcmp(szDisplayName,"NVDA,Display-E")==0)
    {
        return 4;
    }
    else if(strcmp(szDisplayName,"NVDA,Display-F")==0)
    {
        return 5;
    }
    else if(strcmp(szDisplayName,"NVDA,Display-G")==0)
    {
        return 6;
    }
    
    return -1;
}

void NGFX::setDisplayOnline(const char *szDisplayName, int state)
{
    int setOffset = bitoffsetByDisplayName(szDisplayName);
    if(setOffset<0)
        return ;
    
    displayOnlineStates[setOffset] = state;
  
}


int NGFX::getDisplayOnline(const char *szDisplayName)
{
    int setOffset = bitoffsetByDisplayName(szDisplayName);
    if(setOffset<0)
        return false;
    
    return displayOnlineStates[setOffset];
}

void NGFX::clearDisplayOnline(const char *szDisplayName)
{
    int setOffset = bitoffsetByDisplayName(szDisplayName);
    if(setOffset<0)
        return ;
    displayOnlineStates[setOffset] = 0;

}



IOReturn NGFX::NVDA_setAttribute(IONDRVFramebuffer *that,IOSelect attribute, uintptr_t value)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_setAttribute)
    {
        char sztmp[30];
        const char *szAttributeName = getControllerAttriuteName(attribute, sztmp);
        
        switch (attribute) {
            case kIOPowerStateAttribute:
               if(that->getProvider()!=NULL)
               {
                    if(value==1 || value==2)
                        callbackNGFX->setDisplayOnline(that->getProvider()->getName(), (int)value);
                   
               }
                
                DBGLOG("ngfx", "NVDA_setAttribute %s:%s %s value: %u begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",szAttributeName, static_cast<unsigned int >(value));
            break;
         
            default:
                  DBGLOG("ngfx", "NVDA_setAttribute %s:%s %s value %lu begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",szAttributeName,value);
                break;
        }
        IOReturn ret = callbackNGFX->org_NVDA_setAttribute(that,attribute,value);
        DBGLOG("ngfx", "NVDA_setAttribute %s:%s  %s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",szAttributeName,ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_getAttribute(IONDRVFramebuffer *that,IOSelect attribute, uintptr_t * value )
{
    if (callbackNGFX && callbackNGFX->org_NVDA_getAttribute)
    {
        char sztmp[30];
        const char *szAttributeName = getControllerAttriuteName(attribute, sztmp);
        DBGLOG("ngfx", "NVDA_getAttribute %s:%s %s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",szAttributeName);
        switch (attribute) {
            case kIOFBDisplayState:
                if(that->getProvider()!=NULL && value!=NULL)
                {
                    int val  = callbackNGFX->getDisplayOnline(that->getProvider()->getName());
                    
//                    if(val==1)
//                    {
//                        *value = kIOFBDisplayState_PipelineBlack;
//                         DBGLOG("ngfx", "NVDA_getAttribute %s:%s %s ret kIOFBDisplayState_PipelineBlack value %lu ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",szAttributeName, value!=NULL?*value:0);
//                        return kIOReturnSuccess;
//                    }
//                    else
                    if(val==2)
                    {
                       // *value = kIOFBDisplayState_AlreadyActive;
                      
                        uintptr_t konline = 0;
                        that->getAttributeForConnection(0, kConnectionCheckEnable, &konline);
    
                        
                          DBGLOG("ngfx", "NVDA_getAttribute %s:%s %s   value %lu online %lu",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",szAttributeName, value!=NULL?*value:0,konline);
                        
                        if(konline!=0)
                        {
//                            uintptr_t evt=kIODPEventStart;
//                            const char *eventname =getDisplayPortEventName(evt);
//
//                            DBGLOG("ngfx", "NVDA_setAttributeForConnection %s:%s index:%u attribute:%s value:%s",that->getName(),  that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",connectIndex,attributeName, eventname);
//                            if(evt == kIODPEventIdle)
//                            {
//
//                            that->dpProcessInterrupt();
                            //that->setAttributeForConnection(0, kConnectionHandleDisplayPortEvent, uintptr_t(&evt));
                            
//                            if(strcmp(that->getProvider()->getName(),"NVDA,Display-B")==0 )
//                            {
//                                IOSleep(10000);
//                            }
//
//                            *value = kIOFBDisplayState_AlreadyActive;
//                            return kIOReturnSuccess;
                        }
                        
                    
                    }
                  //  callbackNGFX->clearDisplayOnline(that->getProvider()->getName());
                }
                break;
            default:
                break;
        }
        
        IOReturn ret = callbackNGFX->org_NVDA_getAttribute(that,attribute,value);
        DBGLOG("ngfx", "NVDA_getAttribute %s:%s %s ret %x value %lu ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",szAttributeName,ret, value!=NULL?*value:0);
        
        return ret;
    }
    return kIOReturnSuccess;
}
void NGFX::NVDA_doVBLInterrupt(IONDRVFramebuffer *that)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_doVBLInterrupt)
    {
        DBGLOG("ngfx", "NVDA_doVBLInterrupt %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
       // IOReturn ret =
        callbackNGFX->org_NVDA_doVBLInterrupt(that);
        DBGLOG("ngfx", "NVDA_doVBLInterrupt %s:%s ret ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
  //      return ret;
    }
//    return kIOReturnSuccess;
}
void NGFX::NVDA_doHotPlugInterrupt(IONDRVFramebuffer *that)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_doHotPlugInterrupt)
    {
        DBGLOG("ngfx", "NVDA_doHotPlugInterrupt %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        callbackNGFX->org_NVDA_doHotPlugInterrupt(that);
        DBGLOG("ngfx", "NVDA_doHotPlugInterrupt %s:%s ret ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
       // return ret;
    }
 //   return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_doGlkWindowServerTransitionNotify(IONDRVFramebuffer *that,unsigned int p1, unsigned int p2, unsigned int* p3, unsigned int p4)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_doGlkWindowServerTransitionNotify)
    {
        DBGLOG("ngfx", "NVDA_doGlkWindowServerTransitionNotify %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_doGlkWindowServerTransitionNotify(that,p1,p2,p3,p4);
        DBGLOG("ngfx", "NVDA_doGlkWindowServerTransitionNotify %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_setGammaTable(IONDRVFramebuffer *that,unsigned int p1, unsigned int p2, unsigned int p3, void* p4)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_setGammaTable)
    {
        DBGLOG("ngfx", "NVDA_setGammaTable %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_setGammaTable(that,p1,p2,p3,p4);
        DBGLOG("ngfx", "NVDA_setGammaTable %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_setCLUTWithEntries(IONDRVFramebuffer *that,IOColorEntry* p1, unsigned int p2, unsigned int p3, unsigned int p4)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_setCLUTWithEntries)
    {
        DBGLOG("ngfx", "NVDA_setCLUTWithEntries %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_setCLUTWithEntries(that,p1,p2,p3,p4);
        DBGLOG("ngfx", "NVDA_setCLUTWithEntries %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_undefinedSymbolHandler(IONDRVFramebuffer *that,char const* p1, char const* p2)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_undefinedSymbolHandler)
    {
        DBGLOG("ngfx", "NVDA_undefinedSymbolHandler %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_undefinedSymbolHandler(that,p1,p2);
        DBGLOG("ngfx", "NVDA_undefinedSymbolHandler %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOService * NGFX::NVDA_probe(IONDRVFramebuffer *that,IOService * provider, SInt32 * score)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_probe)
    {
        DBGLOG("ngfx", "NVDA_probe %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOService * ret = callbackNGFX->org_NVDA_probe(that,provider,score);
        DBGLOG("ngfx", "NVDA_probe %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret!=NULL?1:0);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_newUserClient(IONDRVFramebuffer *that,task_t          owningTask,void *          security_id,UInt32          type, IOUserClient ** clientH)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_newUserClient)
    {
        DBGLOG("ngfx", "NVDA_newUserClient %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_newUserClient(that,owningTask,security_id, type, clientH);
        DBGLOG("ngfx", "NVDA_newUserClient %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_copyACPIDevice(IONDRVFramebuffer *that,IORegistryEntry* p1)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_copyACPIDevice)
    {
        DBGLOG("ngfx", "NVDA_copyACPIDevice %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_copyACPIDevice(that,p1);
        DBGLOG("ngfx", "NVDA_copyACPIDevice %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_findAppleMuxSubdevice(IONDRVFramebuffer *that,IORegistryEntry* p1, unsigned int p2)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_findAppleMuxSubdevice)
    {
        DBGLOG("ngfx", "NVDA_findAppleMuxSubdevice %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_findAppleMuxSubdevice(that,p1,p2);
        DBGLOG("ngfx", "NVDA_findAppleMuxSubdevice %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_setAppleMuxSubdevice(IONDRVFramebuffer *that,IOACPIPlatformDevice* p1, unsigned int p2)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_setAppleMuxSubdevice)
    {
        DBGLOG("ngfx", "NVDA_setAppleMuxSubdevice %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_setAppleMuxSubdevice(that,p1,p2);
        DBGLOG("ngfx", "NVDA_setAppleMuxSubdevice %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_getAppleMux(IONDRVFramebuffer *that)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_getAppleMux)
    {
        DBGLOG("ngfx", "NVDA_getAppleMux %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_getAppleMux(that);
        DBGLOG("ngfx", "NVDA_getAppleMux %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
bool NGFX::NVDA_setProperty(IONDRVFramebuffer *that,const OSSymbol * aKey, OSObject * anObject)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_setProperty)
    {
        DBGLOG("ngfx", "NVDA_setProperty %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        bool ret = callbackNGFX->org_NVDA_setProperty(that,aKey,anObject);
        DBGLOG("ngfx", "NVDA_setProperty %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_addIntrReg(IONDRVFramebuffer *that,unsigned int p1, void* p2)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_addIntrReg)
    {
        DBGLOG("ngfx", "NVDA_addIntrReg %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_addIntrReg(that,p1,p2);
        DBGLOG("ngfx", "NVDA_addIntrReg %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_removeIntrReg(IONDRVFramebuffer *that,void* p1)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_removeIntrReg)
    {
        DBGLOG("ngfx", "NVDA_removeIntrReg %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_removeIntrReg(that,p1);
        DBGLOG("ngfx", "NVDA_removeIntrReg %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_findIntrReg(IONDRVFramebuffer *that,void* p1)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_findIntrReg)
    {
        DBGLOG("ngfx", "NVDA_findIntrReg %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_findIntrReg(that,p1);
        DBGLOG("ngfx", "NVDA_findIntrReg %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_registerForInterruptType(IONDRVFramebuffer *that,IOSelect interruptType,IOFBInterruptProc proc, OSObject * target, void * ref,void ** interruptRef)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_registerForInterruptType)
    {
        DBGLOG("ngfx", "NVDA_registerForInterruptType %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_registerForInterruptType(that,interruptType,proc,target,ref,interruptRef);
        DBGLOG("ngfx", "NVDA_registerForInterruptType %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_unregisterInterrupt(IONDRVFramebuffer *that,void* interruptRef)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_unregisterInterrupt)
    {
        DBGLOG("ngfx", "NVDA_unregisterInterrupt %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_unregisterInterrupt(that,interruptRef);
        DBGLOG("ngfx", "NVDA_unregisterInterrupt %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_setInterruptState(IONDRVFramebuffer *that,void * interruptRef, UInt32 state)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_setInterruptState)
    {
        DBGLOG("ngfx", "NVDA_setInterruptState %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_setInterruptState(that,interruptRef, state);
        DBGLOG("ngfx", "NVDA_setInterruptState %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}


IOReturn NGFX::NVDA_message(IONDRVFramebuffer *that,UInt32 type, IOService *provider, void *argument)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_message)
    {
        DBGLOG("ngfx", "NVDA_message %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_message(that,type,provider,argument);
        DBGLOG("ngfx", "NVDA_message %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_createNVDCNub(IONDRVFramebuffer *that,unsigned int p1)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_createNVDCNub)
    {
        DBGLOG("ngfx", "NVDA_createNVDCNub %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_createNVDCNub(that,p1);
        DBGLOG("ngfx", "NVDA_createNVDCNub %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_get10BPCFormatGLKSupport(IONDRVFramebuffer *that,unsigned char* p1)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_get10BPCFormatGLKSupport)
    {
        DBGLOG("ngfx", "NVDA_get10BPCFormatGLKSupport %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_get10BPCFormatGLKSupport(that,p1);
        DBGLOG("ngfx", "NVDA_get10BPCFormatGLKSupport %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_getIOPCIDeviceFromDeviceInstance(IONDRVFramebuffer *that,unsigned int p1, IOPCIDevice** lppdevice)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_getIOPCIDeviceFromDeviceInstance)
    {
        DBGLOG("ngfx", "NVDA_getIOPCIDeviceFromDeviceInstance %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_getIOPCIDeviceFromDeviceInstance(that,p1,lppdevice);
        DBGLOG("ngfx", "NVDA_getIOPCIDeviceFromDeviceInstance %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::NVDA_getIOPCIDevicePropertyFromRegistry(IONDRVFramebuffer *that,char const* p1, unsigned short* p2)
{
    if (callbackNGFX && callbackNGFX->org_NVDA_getIOPCIDevicePropertyFromRegistry)
    {
        DBGLOG("ngfx", "NVDA_getIOPCIDevicePropertyFromRegistry %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_NVDA_getIOPCIDevicePropertyFromRegistry(that,p1,p2);
        DBGLOG("ngfx", "NVDA_getIOPCIDevicePropertyFromRegistry %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::IOFramebuffer_doSetup(IONDRVFramebuffer *that,bool bfull)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_doSetup)
    {
        DBGLOG("ngfx", "IOFramebuffer_doSetup %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_IOFramebuffer_doSetup(that,bfull);
        DBGLOG("ngfx", "IOFramebuffer_doSetup %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}
IOReturn NGFX::IONDRVFramebuffer_processConnectChange(IONDRVFramebuffer *that,uintptr_t * value)
{
    if (callbackNGFX && callbackNGFX->org_IONDRVFramebuffer_processConnectChange)
    {
        DBGLOG("ngfx", "IONDRVFramebuffer_processConnectChange %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_IONDRVFramebuffer_processConnectChange(that,value);
        DBGLOG("ngfx", "IONDRVFramebuffer_processConnectChange %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}

const char * getIOOptionBitsStr(IOOptionBits state,char * sztmp)
{
    if(state==0)
        return "none";
    
    int offset = 0;
    if ( state & 0x00000001) snprintf(sztmp+offset, 255-offset, "|kIOFBDidWork");
    if ( state & 0x00000002) snprintf(sztmp+offset, 255-offset, "|kIOFBWorking");
    if ( state & 0x00000004) snprintf(sztmp+offset, 255-offset, "|kIOFBPaging");
    if ( state & 0x00000008) snprintf(sztmp+offset, 255-offset, "|kIOFBWsWait");
    if ( state & 0x00000010) snprintf(sztmp+offset, 255-offset, "|kIOFBDimmed");
    if ( state & 0x00000020) snprintf(sztmp+offset, 255-offset, "|kIOFBServerSlept");
    if ( state & 0x00000040) snprintf(sztmp+offset, 255-offset, "|kIOFBServerUp");
    if ( state & 0x00000080) snprintf(sztmp+offset, 255-offset, "|kIOFBServerDown");
    if ( state & 0x00000100) snprintf(sztmp+offset, 255-offset, "|kIOFBCaptured");
    if ( state & 0x00000200) snprintf(sztmp+offset, 255-offset, "|kIOFBDimDisable");
    if ( state & 0x00001000) snprintf(sztmp+offset, 255-offset, "|kIOFBDisplaysChanging");
    return sztmp;
}

IOOptionBits NGFX::IOFramebuffer_checkPowerWork(IONDRVFramebuffer *that,IOOptionBits state)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_checkPowerWork)
    {
        char szbitoptionstr[255];
        const char *szOption = getIOOptionBitsStr(state,szbitoptionstr);
        DBGLOG("ngfx", "IOFramebuffer_checkPowerWork %s:%s option:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",szOption);
        IOOptionBits ret = callbackNGFX->org_IOFramebuffer_checkPowerWork(that,state);
        szOption = getIOOptionBitsStr(ret,szbitoptionstr);
        DBGLOG("ngfx", "IOFramebuffer_checkPowerWork %s:%s ret %x option:%s ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret,szOption);
        return ret;
    }
    return 0;
}



void NGFX::IOFramebuffer_dpProcessInterrupt(IONDRVFramebuffer *that)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_dpProcessInterrupt)
    {
        DBGLOG("ngfx", "IOFramebuffer_dpProcessInterrupt %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
         callbackNGFX->org_IOFramebuffer_dpProcessInterrupt(that);
        DBGLOG("ngfx", "IOFramebuffer_dpProcessInterrupt %s:%s ret ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
    }
}



IOReturn NGFX::IOFramebuffer_open(IONDRVFramebuffer *that)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_open)
    {
        DBGLOG("ngfx", "IOFramebuffer_open %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_IOFramebuffer_open(that);
        DBGLOG("ngfx", "IOFramebuffer_open %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}

IOReturn NGFX::IOFramebuffer_extSetProperties(IONDRVFramebuffer *that,OSDictionary* dic)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_extSetProperties)
    {
        DBGLOG("ngfx", "IOFramebuffer_extSetProperties %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_IOFramebuffer_extSetProperties(that,dic);
        DBGLOG("ngfx", "IOFramebuffer_extSetProperties %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}

IOReturn NGFX::IOFramebuffer_processConnectChange(IONDRVFramebuffer *that,IOOptionBits mode)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_processConnectChange)
    {
        DBGLOG("ngfx", "IOFramebuffer_processConnectChange %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_IOFramebuffer_processConnectChange(that,mode);
        DBGLOG("ngfx", "IOFramebuffer_processConnectChange %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}


IOReturn NGFX::IOFramebuffer_extSetStartupDisplayMode( OSObject * target, void * reference, IOExternalMethodArguments * args)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_extSetStartupDisplayMode)
    {
        IONDRVFramebuffer *that= (IONDRVFramebuffer *)target;
        DBGLOG("ngfx", "IOFramebuffer_extSetStartupDisplayMode %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_IOFramebuffer_extSetStartupDisplayMode(that, reference,args);
        DBGLOG("ngfx", "IOFramebuffer_extSetStartupDisplayMode %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}



IOReturn NGFX::IOFramebuffer_postWake(IONDRVFramebuffer *that)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_postWake)
    {
        DBGLOG("ngfx", "IOFramebuffer_postWake %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_IOFramebuffer_postWake(that);
        DBGLOG("ngfx", "IOFramebuffer_postWake %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}

IOReturn NGFX::IOFramebuffer_systemWork(OSObject * owner,
                                        IOInterruptEventSource * evtSrc, int intCount)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_systemWork)
    {
        IOFramebuffer *that = (IOFramebuffer *) owner;
        DBGLOG("ngfx", "IOFramebuffer_systemWork %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_IOFramebuffer_systemWork(that,evtSrc,intCount);
        DBGLOG("ngfx", "IOFramebuffer_systemWork %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}


bool NGFX::IOFramebuffer_updateOnline(IONDRVFramebuffer *that)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_updateOnline)
    {
        DBGLOG("ngfx", "IOFramebuffer_updateOnline %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        bool ret = callbackNGFX->org_IOFramebuffer_updateOnline(that);
        DBGLOG("ngfx", "IOFramebuffer_updateOnline %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}


IOReturn NGFX::IONDRVFramebuffer_setDisplayMode(IONDRVFramebuffer *that,IODisplayModeID displayMode, IOIndex depth)
{
    if (callbackNGFX && callbackNGFX->org_IONDRVFramebuffer_setDisplayMode)
    {
        DBGLOG("ngfx", "IONDRVFramebuffer_setDisplayMode %s:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
        IOReturn ret = callbackNGFX->org_IONDRVFramebuffer_setDisplayMode(that,displayMode, depth);
        DBGLOG("ngfx", "IONDRVFramebuffer_setDisplayMode %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}




IOReturn NGFX::IOFramebuffer_notifyServer(IONDRVFramebuffer *that,UInt8 state)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_notifyServer)
    {
        char sztmp[255];
        const char *szState = getIOOptionBitsStr(state,sztmp);
        DBGLOG("ngfx", "IOFramebuffer_notifyServer %s:%s state %s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver", szState);
        IOReturn ret = callbackNGFX->org_IOFramebuffer_notifyServer(that,state);
        DBGLOG("ngfx", "IOFramebuffer_notifyServer %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}



IOOptionBits NGFX::IOFBController_checkConnectionWork(IOFBController *that,IOOptionBits state)
{
    if (callbackNGFX && callbackNGFX->org_IOFBController_checkConnectionWork)
    {
        char sztmp[255];
        const char *szState = getIOOptionBitsStr(state,sztmp);
       // OSObject * that = (OSObject *)pThis;
        DBGLOG("ngfx", "IOFBController_checkConnectionWork state %s begin ", szState);
        IOOptionBits ret = callbackNGFX->org_IOFBController_checkConnectionWork(that,state);
        szState = getIOOptionBitsStr(ret,sztmp);
        DBGLOG("ngfx", "IOFBController_checkConnectionWork ret %s ",szState);
        return ret;
    }
    return 0;
}


void NGFX::IODisplayWrangler_activityChange(IOService *that,IOFramebuffer* fb)
{
    if (callbackNGFX && callbackNGFX->org_IODisplayWrangler_activityChange)
    {
        DBGLOG("ngfx", "IODisplayWrangler_activityChange %s:%s begin fb:%s",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver", fb->getProvider()!=NULL?fb->getProvider()->getName():fb->getName());
        callbackNGFX->org_IODisplayWrangler_activityChange(that,fb);
        DBGLOG("ngfx", "IODisplayWrangler_activityChange %s:%s ret  ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
    }
}


IOReturn NGFX::IOFramebuffer_muxPowerMessage(IONDRVFramebuffer *that,UInt32 messageType)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_muxPowerMessage)
    {
        DBGLOG("ngfx", "IOFramebuffer_muxPowerMessage %s:%s %x begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",messageType);
        IOReturn ret = callbackNGFX->org_IOFramebuffer_muxPowerMessage(that,messageType);
        DBGLOG("ngfx", "IOFramebuffer_muxPowerMessage %s:%s ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret);
        return ret;
    }
    return kIOReturnSuccess;
}

void NGFX::IOFramebuffer_setCaptured(IONDRVFramebuffer *that,bool val)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_setCaptured)
    {
        DBGLOG("ngfx", "IOFramebuffer_setCaptured %s:%s begin value:%s ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver", val?"true":"false");
        callbackNGFX->org_IOFramebuffer_setCaptured(that,val);
        DBGLOG("ngfx", "IOFramebuffer_setCaptured %s:%s ret ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
    }
}


void NGFX::IOFramebuffer_setDimDisable(IOFramebuffer *that,bool val)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_setDimDisable)
    {
        DBGLOG("ngfx", "IOFramebuffer_setDimDisable %s:%s begin %s",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",val?"true":"false");
        callbackNGFX->org_IOFramebuffer_setDimDisable(that,val);
        DBGLOG("ngfx", "IOFramebuffer_setDimDisable %s:%s ret",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
    }
}

void NGFX::IODisplay_setDisplayPowerState(IOService *that,unsigned long state)
{
    if (callbackNGFX && callbackNGFX->org_IODisplay_setDisplayPowerState)
    {
        DBGLOG("ngfx", "IODisplay_setDisplayPowerState %s:%s begin state %lX ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver", state);
        callbackNGFX->org_IODisplay_setDisplayPowerState(that,state);
        DBGLOG("ngfx", "IODisplay_setDisplayPowerState %s:%s ret",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver");
    }
}

const char *getNotificationEventName(IOIndex event, char * szTmp)
{
    switch (event) {
        case kIOFBNotifyDisplayModeWillChange: return "kIOFBNotifyDisplayModeWillChange";
        case kIOFBNotifyDisplayModeDidChange: return "kIOFBNotifyDisplayModeDidChange";
        case kIOFBNotifyWillSleep: return "kIOFBNotifyWillSleep";
        case kIOFBNotifyDidWake: return "kIOFBNotifyDidWake";
        case kIOFBNotifyDidPowerOff: return "kIOFBNotifyDidPowerOff";
        case kIOFBNotifyWillPowerOn: return "kIOFBNotifyWillPowerOn";
    //    case kIOFBNotifyDidSleep: return "kIOFBNotifyDidSleep";
     //   case kIOFBNotifyWillWake: return "kIOFBNotifyWillWake";
        case kIOFBNotifyWillPowerOff: return "kIOFBNotifyWillPowerOff";
        case kIOFBNotifyDidPowerOn: return "kIOFBNotifyDidPowerOn";
        case kIOFBNotifyWillChangeSpeed: return "kIOFBNotifyWillChangeSpeed";
        case kIOFBNotifyDidChangeSpeed: return "kIOFBNotifyDidChangeSpeed";
        case kIOFBNotifyClamshellChange: return "kIOFBNotifyClamshellChange";
        case kIOFBNotifyCaptureChange: return "kIOFBNotifyCaptureChange";
        case kIOFBNotifyOnlineChange: return "kIOFBNotifyOnlineChange";
        case kIOFBNotifyDisplayDimsChange: return "kIOFBNotifyDisplayDimsChange";
        case kIOFBNotifyProbed: return "kIOFBNotifyProbed";
        case kIOFBNotifyVRAMReady: return "kIOFBNotifyVRAMReady";
        case kIOFBNotifyWillNotify: return "kIOFBNotifyWillNotify";
        case kIOFBNotifyDidNotify: return "kIOFBNotifyDidNotify";
        case kIOFBNotifyWSAAWillEnterDefer: return "kIOFBNotifyWSAAWillEnterDefer";
        case kIOFBNotifyWSAAWillExitDefer: return "kIOFBNotifyWSAAWillExitDefer";
        case kIOFBNotifyWSAADidEnterDefer: return "kIOFBNotifyWSAADidEnterDefer";
        case kIOFBNotifyWSAADidExitDefer: return "kIOFBNotifyWSAADidExitDefer";
       // case kIOFBNotifyWSAAEnterDefer: return "kIOFBNotifyWSAAEnterDefer";
      //  case kIOFBNotifyWSAAExitDefer: return "kIOFBNotifyWSAAExitDefer";
        case kIOFBNotifyTerminated: return "kIOFBNotifyTerminated";
        default:
            snprintf(szTmp, 30, "%X",event);
            return szTmp;
            break;
    }
    return "unknown";
}

IOReturn NGFX::IOFramebuffer_deliverFramebufferNotification(IOFramebuffer *that, IOIndex event, void * info)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_deliverFramebufferNotification)
    {
        char sztmp[30];
        const char *szevent = getNotificationEventName(event,sztmp);
        DBGLOG("ngfx", "IOFramebuffer_deliverFramebufferNotification %s:%s event: %s begin" ,that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",szevent);
        IOReturn ret = callbackNGFX->org_IOFramebuffer_deliverFramebufferNotification(that,event,info);
        DBGLOG("ngfx", "IOFramebuffer_deliverFramebufferNotification %s:%s event: %s  ret %x ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",szevent, ret);
        return ret;
    }
    return kIOReturnSuccess;
}

bool NGFX::IOFramebuffer_suspend(IONDRVFramebuffer *that,bool now)
{
    if (callbackNGFX && callbackNGFX->org_IOFramebuffer_suspend)
    {
        DBGLOG("ngfx", "IOFramebuffer_suspend %s:%s now:%s begin ",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():"nopriver", now?"true":"false");
        bool ret = callbackNGFX->org_IOFramebuffer_suspend(that, now);
        DBGLOG("ngfx", "IOFramebuffer_suspend %s:%s ret %s ",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():"nopriver",ret?"true":"false");
        return ret;
    }
    return true;
}

