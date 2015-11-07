#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void open_dump_files();
void close_dump_files();

void blending_sources(struct obs_source *source);

#ifdef __cplusplus
};
#endif