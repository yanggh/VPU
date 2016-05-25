#ifndef __CYCLE_OPERATION_H__
#define __CYCLE_OPERATION_H__


extern void *cycle_thread(__attribute__((unused))void *arg);
extern int check_filename(char *name);
extern int change_name_format(char *tmpname, char *filename);
extern int delete_invalid_file(char *filename, char *path);

#endif
