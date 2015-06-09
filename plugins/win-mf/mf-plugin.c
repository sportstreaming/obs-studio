#include <obs-module.h>

extern struct obs_encoder_info mf_aac_enc_info;

bool obs_module_load(void)
{
	obs_register_encoder(&mf_aac_enc_info);
	return true;
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-mf", "en-US")
