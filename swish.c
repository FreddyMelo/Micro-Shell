#define _GNU_SOURCE

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"
#include "swish_funcs.h"

#define CMD_LEN 512
#define PROMPT "@> "

int main(int argc, char **argv) {
    // Shell ignores SIGTTIN, SIGTTOU when put in background
    struct sigaction sac;
    sac.sa_handler = SIG_IGN;
    if (sigfillset(&sac.sa_mask) == -1) {
        perror("sigfillset failure");
        return 1;
    }
    sac.sa_flags = 0;

    if (sigaction(SIGTTIN, &sac, NULL) == -1) {
        perror("sigaction: SIGTTIN");
        return 1;
    }

    if (sigaction(SIGTTOU, &sac, NULL) == -1) {
        perror("sigation: SIGTTOU");
        return 1;
    }

    strvec_t tokens;
    strvec_init(&tokens);
    job_list_t jobs;
    job_list_init(&jobs);
    char cmd[CMD_LEN];

    printf("%s", PROMPT);
    while (fgets(cmd, CMD_LEN, stdin) != NULL) {
        int i = 0;
        while (cmd[i] != '\n') {
            i++;
        }
        cmd[i] = '\0';

        if (tokenize(cmd, &tokens) != 0) {
            printf("Failed to parse command\n");
            strvec_clear(&tokens);
            job_list_free(&jobs);
            return 1;
        }
        if (tokens.length == 0) {
            printf("%s", PROMPT);
            continue;
        }
        const char *first_token = strvec_get(&tokens, 0);

        if (strcmp(first_token, "pwd") == 0) {
            // Prints the shell's current working directory
            char current_dir[CMD_LEN];
            if (getcwd(current_dir, sizeof(current_dir)) != NULL) {
                printf("%s\n", current_dir);
            } else {
                perror("getcwd failed");
            }
        }

        else if (strcmp(first_token, "cd") == 0) {
            const char *directory;
            if (tokens.length > 1) {
                directory = strvec_get(&tokens, 1);
            } else {
                // This is available in the HOME environment variable (use getenv())
                directory = getenv("HOME");
            }

            if (directory == NULL || chdir(directory) != 0) {
                perror("chdir");
            }
        }

        else if (strcmp(first_token, "exit") == 0) {
            strvec_clear(&tokens);
            break;
        }

        // Prints out current list of pending jobs
        else if (strcmp(first_token, "jobs") == 0) {
            int i = 0;
            job_t *current_directory = jobs.head;
            while (current_directory != NULL) {
                char *status_desc;
                if (current_directory->status == BACKGROUND) {
                    status_desc = "background";
                } else {
                    status_desc = "stopped";
                }
                printf("%d: %s (%s)\n", i, current_directory->name, status_desc);
                i++;
                current_directory = current_directory->next;
            }
        }

        // Moves stopped job into foreground
        else if (strcmp(first_token, "fg") == 0) {
            if (resume_job(&tokens, &jobs, 1) == -1) {
                printf("Failed to resume job in foreground\n");
            }
        }

        // Moves stopped job into background
        else if (strcmp(first_token, "bg") == 0) {
            if (resume_job(&tokens, &jobs, 0) == -1) {
                printf("Failed to resume job in background\n");
            }
        }

        // Waits for a specific job identified by its index in job list
        else if (strcmp(first_token, "wait-for") == 0) {
            if (await_background_job(&tokens, &jobs) == -1) {
                printf("Failed to wait for background job\n");
            }
        }

        // Waits for all background jobs
        else if (strcmp(first_token, "wait-all") == 0) {
            if (await_all_background_jobs(&jobs) == -1) {
                printf("Failed to wait for all background jobs\n");
            }
        }

        else {
            int is_background = 0;
            if (tokens.length > 0 && strcmp(strvec_get(&tokens, tokens.length - 1), "&") == 0) {
                is_background = 1;
                // Remove the '&' token
                strvec_take(&tokens, tokens.length - 1);
            }
            pid_t s_child_pid = fork();
            if (s_child_pid == -1) {
                perror("fork failed");
                //   2. Call run_command() in the child process
            } else if (s_child_pid == 0) {
                if (run_command(&tokens) == -1) {
                    exit(1);
                }
            } else {
                if (is_background) {
                    // Adds the job to the jobs list with BACKGROUND status
                    if (job_list_add(&jobs, s_child_pid, strvec_get(&tokens, 0), BACKGROUND) ==
                        -1) {
                        fprintf(stderr, "Failed to add job to list\n");
                    }

                } else {
                    if (tcsetpgrp(STDIN_FILENO, s_child_pid) == -1) {
                        perror("tcsetpgrp failed");
                        exit(1);
                    }
                    int status;
                    if (waitpid(s_child_pid, &status, WUNTRACED) == -1) {
                        perror("waitpid failed");
                        exit(1);
                    }

                    if (WIFSTOPPED(status)) {
                        if (job_list_add(&jobs, s_child_pid, strvec_get(&tokens, 0), STOPPED) ==
                            -1) {
                            fprintf(stderr, "Failed to add job to list\n");
                        }
                    }
                    pid_t s_parent_pid = getpid();
                    if (tcsetpgrp(STDIN_FILENO, s_parent_pid) == -1) {
                        perror("tcsetpgrp failed");
                        exit(1);
                    }
                }
            }
        }

        strvec_clear(&tokens);
        printf("%s", PROMPT);
    }

    job_list_free(&jobs);
    return 0;
}
