/*
 * Forced into every file of the VIC-20 core (xvic) by tools/get_vice.sh: its entry points
 * and the VICE functions PS64 calls get a vic20_ prefix, so the C64 and VIC-20 cores can be
 * linked into one program. Everything else in the core is made local after a partial link
 * (tools/vice_vic20_keep.txt lists what stays global). Keep both lists in step.
 */
#ifndef PS64_VIC20_NAMES_H
#define PS64_VIC20_NAMES_H
#define retro_init vic20_retro_init
#define retro_deinit vic20_retro_deinit
#define retro_api_version vic20_retro_api_version
#define retro_get_system_info vic20_retro_get_system_info
#define retro_set_environment vic20_retro_set_environment
#define retro_set_video_refresh vic20_retro_set_video_refresh
#define retro_set_audio_sample vic20_retro_set_audio_sample
#define retro_set_audio_sample_batch vic20_retro_set_audio_sample_batch
#define retro_set_input_poll vic20_retro_set_input_poll
#define retro_set_input_state vic20_retro_set_input_state
#define retro_load_game vic20_retro_load_game
#define retro_load_game_special vic20_retro_load_game_special
#define retro_unload_game vic20_retro_unload_game
#define retro_run vic20_retro_run
#define retro_reset vic20_retro_reset
#define retro_get_system_av_info vic20_retro_get_system_av_info
#define retro_set_controller_port_device vic20_retro_set_controller_port_device
#define retro_get_region vic20_retro_get_region
#define retro_get_memory_data vic20_retro_get_memory_data
#define retro_get_memory_size vic20_retro_get_memory_size
#define retro_serialize_size vic20_retro_serialize_size
#define retro_serialize vic20_retro_serialize
#define retro_unserialize vic20_retro_unserialize
#define retro_cheat_reset vic20_retro_cheat_reset
#define retro_cheat_set vic20_retro_cheat_set
#define reload_restart vic20_reload_restart
#define keyboard_set_keyarr vic20_keyboard_set_keyarr
#define keyboard_clear_keymatrix vic20_keyboard_clear_keymatrix
#define machine_set_restore_key vic20_machine_set_restore_key
#define machine_trigger_reset vic20_machine_trigger_reset
#define file_system_attach_disk vic20_file_system_attach_disk
#define cartridge_attach_image vic20_cartridge_attach_image
#define cartridge_detach_image vic20_cartridge_detach_image
#endif
