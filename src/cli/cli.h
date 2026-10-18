#ifndef VML_CLI_H
#define VML_CLI_H

/* The `vml` command line (SPEC 6c). Each command returns the process exit code. */

/* script.c — `vml file.vml [flags]`: compile and run a single script */
int cli_run_script(int argc, char **argv);

/* image.c — `.vmb` images */
int cli_build_script(int argc, char **argv);   /* vml build file.vml -o out.vmb */
int cli_interface(int argc, char **argv);      /* vml interface file.vmb|file.vml */
int cli_is_image_file(const char *path);
int cli_run_image(const char *path, int argc, const char *const *argv);   /* args = os.args() */

/* project.c — vml.toml projects and workspaces */
int project_cmd_new(int argc, char **argv);
int project_cmd_run(int argc, char **argv);
int project_cmd_build(int argc, char **argv);
int project_cmd_test(int argc, char **argv);
int project_cmd_clean(int argc, char **argv);

#endif
