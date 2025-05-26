Micro Shell - A Simplified Linux Shell

A minimalist shell program written in C that mimics the core functionalities of a Unix shell. It supports foreground and background job execution, built-in command handling, basic job control (`fg`, `bg`, `jobs`), and I/O redirection. This project was developed with an emphasiso on further developing system-level programming skills and demonstrate knowledge of process management, signal handling, and dynamic memory structures.

Features

- **Built-in job control**
  - `jobs` — list background/stopped processes
  - `fg [n]` — resume job `n` in the foreground
  - `bg [n]` — resume job `n` in the background
  - `wait-for [n]` — wait for a background job to finish
  - `wait-all` — wait for all background jobs to finish

- **Built-in commands**
  - `cd [dir]` — change working directory
  - `pwd` — print current directory
  - `exit` — quit the shell

- **Command execution**
  - Executes external programs using `execvp`
  - Supports background execution via `&`

- **Redirection support**
  - `>` — redirect stdout
  - `<` — redirect stdin
  - `>>` — append to stdout
