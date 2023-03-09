
catnip_package(dswifi DEFAULT calico)

catnip_add_preset(calico
	TOOLSET    NDS
	PROCESSOR  armv5te
	BUILD_TYPE Release
	CACHE      DKP_NDS_PLATFORM_LIBRARY=calico
)

#catnip_add_preset(hybrid
#	TOOLSET    NDS
#	PROCESSOR  armv5te
#	BUILD_TYPE Release
#)
