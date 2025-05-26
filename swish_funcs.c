#define _GNU_SOURCE

#include "swish_funcs.h"

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"

#define MAX_ARGS 10

int tokenize(char *s, strvec_t *tokens) {
    if (s == NULL) {
        return -1;
    }

    if (tokens == NULL) {
        return -1;
    }
    // Assume each token is separated by a single space (" ")
    char *token = strtok(s, " ");

    // Add each token to the 'tokens' parameter (a string vector)
    while (token != NULL) {
        if (strvec_add(tokens, token) == -1) {
            return -1;
        }
        token = strtok(NULL, " ");
    }
    // Return 0 on success, -1 on error
    return 0;
}

int run_command(strvec_t *tokens) {
    struct sigaction sac;
    sac.sa_handler = SIG_DFL;
    sac.sa_flags = 0;
    if (sigfillset(&sac.sa_mask) == -1) {
        perror("sigfillset failed");
        return -1;
    }
    if (sigaction(SIGTTIN, &sac, NULL) == -1) {
        perror("sigaction failed");
        return -1;
    }

    if (sigaction(SIGTTOU, &sac, NULL) == -1) {
        perror("sig failed");
        return -1;
    }

    // Call getpid() to get its process ID then call setpgid() and use this process
    // ID as the value for the new process group ID
    pid_t s_child_pid = getpid();
    if (setpgid(s_child_pid, s_child_pid) == -1) {
        perror("setpgid failed");
        return -1;
    }
    char *args[MAX_ARGS];
    int in_fd = -1;
    int out_fd = -1;
    int index = 0;
    int i, in_redirect = -1, out_redirect = -1, append_redirect = -1;

    // Performs output redirection before exec()'ing
    // Check for '<' (redirect input), '>' (redirect output), '>>' (redirect and append output)
    for (i = 0; i < tokens->length; i++) {
        char *token = strvec_get(tokens, i);
        if (strcmp(token, "<") == 0) {
            in_redirect = i;
        } else if (strcmp(token, ">") == 0) {
            out_redirect = i;
        } else if (strcmp(token, ">>") == 0) {
            append_redirect = i;
        }
    }
    // Open the necessary file for reading (<), writing (>), or appending (>>)
    for (int i = 0; i < tokens->length && i < MAX_ARGS - 1; i++) {
        if (i == in_redirect || i == out_redirect || i == append_redirect) {
            i++;
        } else {
            args[index++] = strvec_get(tokens, i);
        }
    }

    args[index] = NULL;

    if (in_redirect != -1 && in_redirect + 1 < tokens->length) {
        in_fd = open(strvec_get(tokens, in_redirect + 1), O_RDONLY);
        if (in_fd == -1) {
            perror("Failed to open input file");
            return -1;
        }
        // Use dup2() to redirect stdin (<), stdout (> or >>)
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }

    if (out_redirect != -1 && out_redirect + 1 < tokens->length) {
        out_fd = open(strvec_get(tokens, out_redirect + 1), O_CREAT | O_WRONLY | O_TRUNC,
                      S_IRUSR | S_IWUSR);
        if (out_fd == -1) {
            perror("Failed to open output file");
            return -1;
        }
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);

    } else if (append_redirect != -1 && append_redirect + 1 < tokens->length) {
        out_fd = open(strvec_get(tokens, append_redirect + 1), O_CREAT | O_WRONLY | O_APPEND,
                      S_IRUSR | S_IWUSR);
        if (out_fd == -1) {
            perror("Failed to open output file");
            return -1;
        }
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }
    // DO NOT pass redirection operators and file names to exec()'d program
    // E.g., "ls -l > out.txt" should be exec()'d with strings "ls", "-l", NULL
    execvp(args[0], args);
    perror("exec");
    return -1;
}

int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground) {
    int index;
    //  Resume stopped jobs in the foreground
    if (tokens->length < 2) {
        fprintf(stderr, "foreground or background failed\n");
        return -1;
    }
    // Look up the relevant job information (in a job_t) from the jobs list
    if (sscanf(strvec_get(tokens, 1), "%d", &index) != 1) {
        fprintf(stderr, "Index is not valid\n");
        return -1;
    }

    job_t *job = job_list_get(jobs, index);
    if (job == NULL) {
        fprintf(stderr, "Job index out of bounds\n");
        return -1;
    }

    if (is_foreground) {
        int status;
        // tcsetpgrp(STDIN_FILENO, <job_pid>) where job_pid is the job's process ID
        if (tcsetpgrp(STDIN_FILENO, job->pid) == -1) {
            perror("tcsetpgrp failed");
            return -1;
        }

        // Sends the process the SIGCONT signal with the kill() system call
        if (kill(job->pid, SIGCONT) == -1) {
            perror("kill failed");
            return -1;
        }
        // waitpid() logic as in main
        if (waitpid(job->pid, &status, WUNTRACED) == -1) {
            perror("waitpid failed");
            return -1;
        }
        // If the job has terminated (not stopped), remove it from the 'jobs' list
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (job_list_remove(jobs, index) == -1) {
                fprintf(stderr, "Failed to remove job\n");
                return -1;
            }

        } else if (WIFSTOPPED(status)) {
            job->status = STOPPED;
        }

        // tcsetpgrp(STDIN_FILENO, <shell_pid>). shell_pid is the *current*
        //    process's pid, since we call this function from the main shell process
        pid_t shell_pid = getpid();
        if (tcsetpgrp(STDIN_FILENO, shell_pid) == -1) {
            perror("tcsetpgrp failed");
            return -1;
        }
    } else {
        //  The ability to resume stopped jobs in the background.
        if (kill(job->pid, SIGCONT) == -1) {
            perror("kill failed");
            return -1;
        }
        job->status = BACKGROUND;
    }

    return 0;
}

int await_background_job(strvec_t *tokens, job_list_t *jobs) {
    if (tokens->length < 2) {
        fprintf(stderr, "wait-for action failed\n");
        return -1;
    }
    //  Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    int index;
    if (sscanf(strvec_get(tokens, 1), "%d", &index) != 1) {
        fprintf(stderr, "Invalid job index\n");
        return -1;
    }

    job_t *job = job_list_get(jobs, index);
    if (job == NULL) {
        fprintf(stderr, "Job index out of bounds\n");
        return -1;
    }
    // Make sure the job's status is BACKGROUND (no sense waiting for a stopped job)
    if (job->status != BACKGROUND) {
        fprintf(stderr, "Job index is for stopped process not background process\n");
        return -1;
    }
    //  Use waitpid() to wait for the job to terminate, as you have in resume_job() and main().
    int status;
    if (waitpid(job->pid, &status, WUNTRACED) == -1) {
        perror("waitpid failed");
        return -1;
    }
    // If the process terminates (is not stopped by a signal) remove it from the jobs list
    if (WIFEXITED(status)) {
        if (job_list_remove(jobs, index) == -1) {
            fprintf(stderr, "Failed to remove job\n");
            return -1;
        }
    }
    if (WIFSIGNALED(status)) {
        if (job_list_remove(jobs, index) == -1) {
            fprintf(stderr, "Failed to remove job\n");
            return -1;
        }
    }
    return 0;
}

int await_all_background_jobs(job_list_t *jobs) {
    // Iterate through the jobs list, ignoring any stopped jobs
    job_t *current_job = jobs->head;
    int i = 0;
    while (current_job != NULL) {
        job_t *next_job = current_job->next;

        // For a background job, call waitpid() with WUNTRACED.
        if (current_job->status == BACKGROUND) {
            int status;
            if (waitpid(current_job->pid, &status, WUNTRACED) == -1) {
                perror("waitpid failed");
                return -1;
            }

            // If the job has stopped (check with WIFSTOPPED), change its
            //    status to STOPPED. If the job has terminated, do nothing until the
            //    next step (don't attempt to remove it while iterating through the list).
            if (WIFSTOPPED(status)) {
                current_job->status = STOPPED;

            } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
                //  Remove all background jobs (which have all just terminated) from jobs list.
                //    Use the job_list_remove_by_status() function.
                job_list_remove(jobs, i);
                i--;
            }
        }
        current_job = next_job;
        i++;
    }

    return 0;
}
