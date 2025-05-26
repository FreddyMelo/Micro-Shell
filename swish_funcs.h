#ifndef SWISH_FUNCS_H
#define SWISH_FUNCS_H

#include "job_list.h"
#include "string_vector.h"

int tokenize(char *s, strvec_t *tokens);

int run_command(strvec_t *tokens);

int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground);

int await_background_job(strvec_t *tokens, job_list_t *jobs);

int await_all_background_jobs(job_list_t *jobs);

#endif    // SWISH_FUNCS_H
