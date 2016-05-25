#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include "vpu.h"
#include "conf.h"
#include "counter.h"
#include "fax_queue.h"
#include "voice_parser.h"
#include "cycle_operation.h"
void *cycle_thread(__attribute__((unused))void *arg)
{
    time_t start_time;
    time_t tim;
    struct tm cur_tm;
    struct tm *tmp;
    int last_day;
    time_t time_before;
    struct tm tmp_before;
    time_t time_nextday;
    struct tm nextday;
    char date_dir[256] = {0}; //, filename[128] = {0};
    char rm_cmd[256] = {0};
    struct stat dir_stat;
    struct statfs disk_info;
    int i = 0, j = 0, k = 0;
    uint64_t block_size;
    uint64_t avail_disk;

    //get start time
    time(&start_time);

    tmp = localtime_r(&start_time, &cur_tm);
    last_day = cur_tm.tm_mday;

    while (1) {
        tim = time(NULL);
        tmp = localtime_r(&tim, &cur_tm);
        if (tmp != NULL) {
            if (cur_tm.tm_mday != last_day) {
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_CYCLE, 
                        "today: %d, last day: %d\n", cur_tm.tm_mday, last_day);
                time_nextday = tim + 24 * 60 * 60;
                localtime_r(&time_nextday, &nextday);
                create_all_dirs(nextday.tm_year + 1900, nextday.tm_mon + 1, nextday.tm_mday);
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_CYCLE, "create %04d-%02d-%02d dirs\n", 
                        nextday.tm_year + 1900, nextday.tm_mon + 1, nextday.tm_mday);

                time_before = tim - storage_time * 24 * 60 * 60;
                localtime_r(&time_before, &tmp_before);
				//snprintf(filename, 128, "%04d-%02d-%02d", tmp_before.tm_year + 1900, tmp_before.tm_mon + 1, tmp_before.tm_mday);
				//snprintf(date_dir, 256, "%s/%s/", file_dirs.dirs[i].dir_name, NORMAL_DIR);
				//delete_invalid_file(filename, date_dir);
#if 1
                for (i = 0; i < file_dirs.num; i++) {
                    memset(date_dir, 0, 256);
                    snprintf(date_dir, 256, "%s/%s/%04d-%02d-%02d", 
                            file_dirs.dirs[i].dir_name, NORMAL_DIR, 
                            tmp_before.tm_year + 1900, tmp_before.tm_mon + 1, tmp_before.tm_mday);
                    if (stat(date_dir, &dir_stat) == 0 && (dir_stat.st_mode & S_IFMT) == S_IFDIR) {
                        memset(rm_cmd, 0, 256);
                        snprintf(rm_cmd, 256, "rm -rf %s", date_dir);
                        system(rm_cmd);
                        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_CYCLE, "rm dir %s\n", date_dir);
                    }
                }
#endif
            }
            last_day = cur_tm.tm_mday;

#if 1
            for (i = 0; i < SR155_NUM; i++) {
                for (j = 0; j < E1_NUM; j++) {
                    for (k = 0; k < TS_NUM; k++) {
                        pthread_mutex_lock(&(voice_file[i][j][k].mutex));
                        if (voice_file[i][j][k].update_time != 0 
                                && tim - voice_file[i][j][k].update_time >= 60) {
                            
                            cleanup_file_struct(&(voice_file[i][j][k]));

                            voice_file[i][j][k].update_time = 0;
                        }
                        pthread_mutex_unlock(&(voice_file[i][j][k].mutex));
                    }
                }
            }
#endif
        }

        for (i = 0; i < file_dirs.num; i++) {
            statfs(file_dirs.dirs[i].dir_name, &disk_info);

            block_size = disk_info.f_bsize;
            avail_disk = disk_info.f_bavail * block_size;
            avail_disk = avail_disk>>30;
            if (avail_disk <= disk_left) {
                file_dirs.dirs[i].write_flag = 0;
                applog(APP_LOG_LEVEL_WARNING, APP_VPU_LOG_MASK_CYCLE, 
                        "%s left %luGB space only, no more data write to it!", 
                        file_dirs.dirs[i].dir_name, avail_disk);
            } else {
                file_dirs.dirs[i].write_flag = 1;
            }

            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_CYCLE, 
                    "dir %s avail disk: %lu GB", file_dirs.dirs[i].dir_name, avail_disk);
        }

        if (unlikely(svm_signal_flags & (SVM_KILL | SVM_STOP))) {
            if (atomic64_read(&call_online) == 0) {
                applog(APP_LOG_LEVEL_NOTICE, APP_VPU_LOG_MASK_CYCLE, "receive SIGINT and no call online");
                svm_signal_flags |= SVM_DONE;
                break;
            }
        }

        sleep(60);
    }

    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_CYCLE, "pthread counter_thread exit\n");
    pthread_exit(NULL);

    return 0;
}

int delete_invalid_file(char *filename, char *path)
{
	if (filename == NULL || path == NULL)
		return -1;
	time_t cur;
	struct tm *curp;
	char name[128], rmbuf[1024],tmpname[128];
	DIR *dirp;
	struct dirent *dir;
	int start, end, now;

	memset(name, 0, sizeof(name));
	memset(tmpname, 0, sizeof(tmpname));
	time(&cur);
	curp = localtime(&cur);
	sprintf(name, "%d-%d%d-%d%d", curp->tm_year + 1900, (curp->tm_mon + 1)/ 10, 
		(curp->tm_mon + 1) % 10, curp->tm_mday / 10, curp->tm_mday % 10);
	if (0 != check_filename(filename))
		return -1;

	dirp = opendir(path);
	if (dirp == NULL)
		return -1;
	if (0 != change_name_format(tmpname, filename))
		return -1;
	start = atoi(tmpname);
	if (0 != change_name_format(tmpname, name))
		return -1;
	end = atoi(tmpname);
	while (NULL != (dir = readdir(dirp))) {
		if (dir->d_name[0] == '.')
			continue;
		if (check_filename(dir->d_name) != 0) {
			memset(rmbuf, 0, sizeof(rmbuf));
			sprintf(rmbuf, "rm -rf %s%s", path, dir->d_name);
			system(rmbuf);
		} else {
			if (0 != change_name_format(tmpname, dir->d_name))
				return -1;
			now = atoi(tmpname);
			if (now < start || now > end) {
				memset(rmbuf, 0, sizeof(rmbuf));
				sprintf(rmbuf, "rm -rf %s%s", path, dir->d_name);
				system(rmbuf);
			}
		}
	}

	closedir(dirp);
	return 0;
}

int change_name_format(char *tmpname, char *filename)
{
	if (tmpname == NULL || filename == NULL)
		return -1;
	char *p = NULL;
	int ret = 0;
	char rm_buf[128];
	
	bzero(tmpname, 0);
	ret = check_filename(filename);
	if (ret != 0) {
		memset(rm_buf, 0, sizeof(rm_buf));
		snprintf(rm_buf, 128, "rm -rf %s", filename);
		system(rm_buf);
		return -1;
	}
	
	p = strtok(filename, "-");
	strcpy(tmpname, p);
	p = strtok(NULL, "-");
	strcat(tmpname, p);
	p = strtok(NULL, "-");
	strcat(tmpname, p);
	
	return 0;
}

int check_filename(char *name)
{
	if (name == NULL)
		return -1;
	char *p = name, buff[8];
	int count = 0, num = 0;
	time_t t;
	struct tm *tp;
	int year, mon, day;
	struct stat dir_stat;
	
	time(&t);
	tp = localtime(&t);
	while (*p != '\0') {
		if (*p == '-') {
			num ++;
			continue;
		}

		if (*p < '0' || *p > '9')
			return -2;
		else {
			count ++;
		}
		p ++;
	}
	if (count != 8 || num != 2)
		return -2;
	
	if (stat(name, &dir_stat) != 0 && (dir_stat.st_mode & S_IFMT) != S_IFDIR)
		return -2;
	
	p = name;
	memset(buff, 0, sizeof(buff));
	memcpy(buff, p, 4);
	year = atoi(buff);
	if (year < tp->tm_year + 1899 || year > tp->tm_year + 1901)
		return -2;
	memset(buff, 0, sizeof(buff));
	memcpy(buff, p + 5, 2);
	mon = atoi(buff);
	if (mon < 1 || mon > 12)
		return -2;

	memset(buff, 0, sizeof(buff));
	memcpy(buff, p + 8, 2);
	day = atoi(buff);
	
	switch(mon) {
		case 1:
		case 3:
		case 5:
		case 7:
		case 8:
		case 10:
		case 12:
			if (day < 1 || day > 31)
				return -2;
			break;
		case 2:
			if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
				if (day > 29 || day < 1)
					return -2;
			} else {
				if (day > 28 || day < 1)
					return -2;
			}
			break;
		case 4:
		case 6:
		case 9:
		case 11:
			if (day < 1 || day > 30)
				return -2;
			break;
		default:
			return -2;
	}

	return 0;
}
