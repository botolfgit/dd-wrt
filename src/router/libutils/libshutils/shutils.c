/*
 * Shell-like utility functions
 *
 * Copyright 2001-2003, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id: shutils.c,v 1.2 2005/11/11 09:26:18 seg Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#ifdef __UCLIBC__
#include <error.h>
#endif
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <net/ethernet.h>
#include <dirent.h>
#include <bcmnvram.h>
#include <shutils.h>
#include <utils.h>

/*
 * Reads file and returns contents
 * @param       fd      file descriptor
 * @return      contents of file or NULL if an error occurred
 */
char *fd2str(int fd)
{
	char *buf = NULL;
	size_t count = 0, n;

	do {
		buf = realloc(buf, count + 512);
		n = read(fd, buf + count, 512);
		if (n < 0) {
			free(buf);
			buf = NULL;
		}
		count += n;
	}
	while (n == 512);

	close(fd);
	if (buf)
		buf[count] = '\0';
	return buf;
}

/*
 * Reads file and returns contents
 * @param       path    path to file
 * @return      contents of file or NULL if an error occurred
 */
char *file2str(const char *path)
{
	int fd;

	if ((fd = open(path, O_RDONLY)) == -1) {
		perror(path);
		return NULL;
	}

	return fd2str(fd);
}

/*
 * Waits for a file descriptor to change status or unblocked signal
 * @param       fd      file descriptor
 * @param       timeout seconds to wait before timing out or 0 for no timeout
 * @return      1 if descriptor changed status or 0 if timed out or -1 on error
 */
int waitfor(int fd, int timeout)
{
	fd_set rfds;
	struct timeval tv = { timeout, 0 };

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	return select(fd + 1, &rfds, NULL, NULL, (timeout > 0) ? &tv : NULL);
}

/*
 * Concatenates NULL-terminated list of arguments into a single
 * commmand and executes it
 * @param       argv    argument list
 * @param       path    NULL, ">output", or ">>output"
 * @param       timeout seconds to wait before timing out or 0 for no timeout
 * @param       ppid    NULL to wait for child termination or pointer to pid
 * @return      return value of executed command or errno
 */

static void flog(const char *fmt, ...)
{
	if (nvram_matchi("flog_enabled", 1)) {
		char varbuf[256];
		va_list args;

		va_start(args, (char *)fmt);
		vsnprintf(varbuf, sizeof(varbuf), fmt, args);
		va_end(args);
		FILE *fp = fopen("/tmp/syslog.log", "ab");
		fprintf(fp, varbuf);
		fclose(fp);
	}
}

int system2(char *command)
{

#if (!defined(HAVE_X86) && !defined(HAVE_RB600)) || defined(HAVE_WDR4900)	//we must disable this on x86 since nvram is not available at startup

	dd_debug(DEBUG_CONSOLE, "%s:%s", __func__, command);
	if (nvram_match("debug_delay", "1")) {
		sleep(1);
	}
#endif

#ifndef HAVE_SILENCE
	fprintf(stderr, "system: %s\n", command);
#endif
	return system(command);
}

int sysprintf(const char *fmt, ...)
{
	char *varbuf;
	va_list args;

	va_start(args, (char *)fmt);
	vasprintf(&varbuf, fmt, args);
	va_end(args);
	int ret = system2(varbuf);
	free(varbuf);
	return ret;
}

void dd_debug(int target, const char *fmt, ...)
{
	char *varbuf;
	va_list args;
	if (target == DEBUG_CONSOLE && !nvram_match("console_debug", "1"))
		return;
	if (target == DEBUG_HTTPD && !nvram_match("httpd_debug", "1"))
		return;
	if (target == DEBUG_SERVICE && !nvram_match("service_debug", "1"))
		return;

	va_start(args, (char *)fmt);
	vasprintf(&varbuf, fmt, args);
	va_end(args);
	dd_syslog(LOG_DEBUG, varbuf);
	fprintf(stderr, varbuf);
	free(varbuf);
	return;
}

int eval_va(const char *cmd, ...)
{
	const char *s_args[128];
	va_list args;
	va_start(args, (char *)cmd);
	char *next;
	int i;
	s_args[0] = cmd;
	for (i = 0; i < 127; i++) {
		const char *arg = va_arg(args, const char *);
		s_args[i + 1] = arg;
		if (arg == NULL)
			break;
	}
	return _evalpid(s_args, ">/dev/console", 0, NULL);

}

int eval_va_silence(const char *cmd, ...)
{
	const char *s_args[128];
	va_list args;
	va_start(args, (char *)cmd);
	char *next;
	int i;
	s_args[0] = cmd;
	for (i = 0; i < 127; i++) {
		const char *arg = va_arg(args, const char *);
		s_args[i + 1] = arg;
		if (arg == NULL)
			break;
	}
	return _evalpid(s_args, NULL, 0, NULL);

}

// FILE *debugfp=NULL;
int _evalpid(char *const argv[], char *path, int timeout, int *ppid)
{
	pid_t pid;
	int status;
	int fd;
	int flags;
	int sig;

	// if (debugfp==NULL)
	// debugfp = fopen("/tmp/evallog.log","wb");
	// char buf[254] = "";
#if (!defined(HAVE_X86) && !defined(HAVE_RB600)) || defined(HAVE_WDR4900)	//we must disable this on x86 since nvram is not available at startup

	if (nvram_matchi("console_debug", 1)) {
		int i = 0;
		char buf[256] = { 0 };
		if (argv[i])
			while (argv[i] != NULL) {
				fprintf(stderr, "%s ", argv[i]);
				dd_snprintf(buf, sizeof(buf), "%s%s ", buf, argv[i++]);
				flog("%s ", argv[i - 1]);
			}
		dd_syslog(LOG_INFO, "%s:%s", __func__, buf);
		fprintf(stderr, "\n");
		flog("\n");
	}
#ifndef HAVE_SILENCE
	int i = 0;

	while (argv[i] != NULL) {
		fprintf(stderr, "%s ", argv[i++]);
	}
	fprintf(stderr, "\n");

#endif
#endif
#if (!defined(HAVE_X86) && !defined(HAVE_RB600)) || defined(HAVE_WDR4900)	//we must disable this on x86 since nvram is not available at startup

	if (nvram_match("debug_delay", "1")) {
		sleep(1);
	}
#endif

	switch (pid = fork()) {
	case -1:		/* error */
		perror("fork");
		return errno;
	case 0:		/* child */
		/*
		 * Reset signal handlers set for parent process 
		 */
		for (sig = 0; sig < (_NSIG - 1); sig++)
			signal(sig, SIG_DFL);

		/*
		 * Clean up 
		 */
		ioctl(0, TIOCNOTTY, 0);
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		setsid();

		/*
		 * We want to check the board if exist UART? , add by honor
		 * 2003-12-04 
		 */
		if ((fd = open("/dev/console", O_RDWR)) < 0) {
			(void)open("/dev/null", O_RDONLY);
			(void)open("/dev/null", O_WRONLY);
			(void)open("/dev/null", O_WRONLY);
		} else {
			close(fd);
			(void)open("/dev/console", O_RDONLY);
			(void)open("/dev/console", O_WRONLY);
			(void)open("/dev/console", O_WRONLY);
		}

		/*
		 * Redirect stdout to <path> 
		 */
		if (path) {
			flags = O_WRONLY | O_CREAT;
			if (!strncmp(path, ">>", 2)) {
				/*
				 * append to <path> 
				 */
				flags |= O_APPEND;
				path += 2;
			} else if (!strncmp(path, ">", 1)) {
				/*
				 * overwrite <path> 
				 */
				flags |= O_TRUNC;
				path += 1;
			}
			if ((fd = open(path, flags, 0644)) < 0)
				perror(path);
			else {
				dup2(fd, STDOUT_FILENO);
				dup2(fd, STDERR_FILENO);
				close(fd);
			}
		}

		/*
		 * execute command 
		 */
		// for (i = 0; argv[i]; i++)
		// snprintf (buf + strlen (buf), sizeof (buf), "%s ", argv[i]);
		// cprintf("cmd=[%s]\n", buf);
		setenv("PATH", "/sbin:/bin:/usr/sbin:/usr/bin", 1);
		alarm(timeout);
		execvp(argv[0], argv);
		perror(argv[0]);
		exit(errno);
	default:		/* parent */
		if (ppid) {
			*ppid = pid;
			return 0;
		} else {
			waitpid(pid, &status, 0);
			if (WIFEXITED(status))
				return WEXITSTATUS(status);
			else
				return status;
		}
	}
}

int _eval(char *const argv[])
{
	return _evalpid(argv, ">/dev/console", 0, NULL);
}

/*
 * Concatenates NULL-terminated list of arguments into a single
 * commmand and executes it
 * @param       argv    argument list
 * @return      stdout of executed command or NULL if an error occurred
 */
char *_backtick(char *const argv[])
{
	int filedes[2];
	pid_t pid;
	int status;
	char *buf = NULL;

	/*
	 * create pipe 
	 */
	if (pipe(filedes) == -1) {
		perror(argv[0]);
		return NULL;
	}

	switch (pid = fork()) {
	case -1:		/* error */
		return NULL;
	case 0:		/* child */
		close(filedes[0]);	/* close read end of pipe */
		dup2(filedes[1], 1);	/* redirect stdout to write end of
					 * pipe */
		close(filedes[1]);	/* close write end of pipe */
		execvp(argv[0], argv);
		exit(errno);
		break;
	default:		/* parent */
		close(filedes[1]);	/* close write end of pipe */
		buf = fd2str(filedes[0]);
		waitpid(pid, &status, 0);
		break;
	}

	return buf;
}

/*
 * Kills process whose PID is stored in plaintext in pidfile
 * @param       pidfile PID file
 * @return      0 on success and errno on failure
 */
int kill_pidfile(char *pidfile)
{
	FILE *fp = fopen(pidfile, "r");
	char buf[256];

	if (fp && fgets(buf, sizeof(buf), fp)) {
		pid_t pid = strtoul(buf, NULL, 0);

		fclose(fp);
		return kill(pid, SIGTERM);
	} else
		return errno;
}

/*
 * fread() with automatic retry on syscall interrupt
 * @param       ptr     location to store to
 * @param       size    size of each element of data
 * @param       nmemb   number of elements
 * @param       stream  file stream
 * @return      number of items successfully read
 */
int safe_fread(void *ptr, size_t size, size_t nmemb, FILE * stream)
{
	size_t ret = 0;

	do {
		clearerr(stream);
		ret += fread((char *)ptr + (ret * size), size, nmemb - ret, stream);
	}
	while (ret < nmemb && ferror(stream) && errno == EINTR);

	return ret;
}

/*
 * fwrite() with automatic retry on syscall interrupt
 * @param       ptr     location to read from
 * @param       size    size of each element of data
 * @param       nmemb   number of elements
 * @param       stream  file stream
 * @return      number of items successfully written
 */
int safe_fwrite(const void *ptr, size_t size, size_t nmemb, FILE * stream)
{
	size_t ret = 0;

	do {
		clearerr(stream);
		ret += fwrite((char *)ptr + (ret * size), size, nmemb - ret, stream);
	}
	while (ret < nmemb && ferror(stream) && errno == EINTR);

	return ret;
}

/*
 * Convert Ethernet address string representation to binary data
 * @param       a       string in xx:xx:xx:xx:xx:xx notation
 * @param       e       binary data
 * @return      TRUE if conversion was successful and FALSE otherwise
 */
int ether_atoe(const char *a, char *e)
{
	char *c = (char *)a;
	int i = 0;

	bzero(e, ETHER_ADDR_LEN);
	for (;;) {
		e[i++] = (unsigned char)strtoul(c, &c, 16);
		if (!*c++ || i == ETHER_ADDR_LEN)
			break;
	}
	return (i == ETHER_ADDR_LEN);
}

/*
 * Convert Ethernet address binary data to string representation
 * @param       e       binary data
 * @param       a       string in xx:xx:xx:xx:xx:xx notation
 * @return      a
 */
char *ether_etoa(const char *e, char *a)
{
	char *c = a;
	int i;

	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		if (i)
			*c++ = ':';
		c += sprintf(c, "%02X", e[i] & 0xff);
	}
	return a;
}

/*
 * Search a string backwards for a set of characters
 * This is the reverse version of strspn()
 *
 * @param       s       string to search backwards
 * @param       accept  set of chars for which to search
 * @return      number of characters in the trailing segment of s 
 *              which consist only of characters from accept.
 */
static size_t sh_strrspn(const char *s, const char *accept)
{
	const char *p;
	size_t accept_len = strlen(accept);
	int i;

	if (s[0] == '\0')
		return 0;

	p = s + (strlen(s) - 1);
	i = 0;

	do {
		if (memchr(accept, *p, accept_len) == NULL)
			break;
		p--;
		i++;
	}
	while (p != s);

	return i;
}

int get_ifname_unit(const char *ifname, int *unit, int *subunit)
{
	const char digits[] = "0123456789";
	char str[64];
	char *p;
	size_t ifname_len = strlen(ifname);
	size_t len;
	long val;

	if (unit)
		*unit = -1;
	if (subunit)
		*subunit = -1;

	if (ifname_len + 1 > sizeof(str))
		return -1;

	strcpy(str, ifname);

	/*
	 * find the trailing digit chars 
	 */
	len = sh_strrspn(str, digits);

	/*
	 * fail if there were no trailing digits 
	 */
	if (len == 0)
		return -1;

	/*
	 * point to the beginning of the last integer and convert 
	 */
	p = str + (ifname_len - len);
	val = strtol(p, NULL, 10);

	/*
	 * if we are at the beginning of the string, or the previous character is 
	 * not a '.', then we have the unit number and we are done parsing 
	 */
	if (p == str || p[-1] != '.') {
		if (unit)
			*unit = val;
		return 0;
	} else {
		if (subunit)
			*subunit = val;
	}

	/*
	 * chop off the '.NNN' and get the unit number 
	 */
	p--;
	p[0] = '\0';

	/*
	 * find the trailing digit chars 
	 */
	len = sh_strrspn(str, digits);

	/*
	 * fail if there were no trailing digits 
	 */
	if (len == 0)
		return -1;

	/*
	 * point to the beginning of the last integer and convert 
	 */
	p = p - len;
	val = strtol(p, NULL, 10);

	/*
	 * save the unit number 
	 */
	if (unit)
		*unit = val;

	return 0;
}

#define WLMBSS_DEV_NAME	"wlmbss"
#define WL_DEV_NAME "wl"
#define WDS_DEV_NAME	"wds"

#if defined(linux)

/**
	 nvifname_to_osifname() 
	 The intent here is to provide a conversion between the OS interface name
	 and the device name that we keep in NVRAM.  
	 This should eventually be placed in a Linux specific file with other 
	 OS abstraction functions.

	 @param nvifname pointer to ifname to be converted
	 @param osifname_buf storage for the converted osifname
	 @param osifname_buf_len length of storage for osifname_buf
*/
int nvifname_to_osifname(const char *nvifname, char *osifname_buf, int osifname_buf_len)
{
	char varname[NVRAM_MAX_PARAM_LEN];
	char *ptr;

	bzero(osifname_buf, osifname_buf_len);

	/*
	 * Bail if we get a NULL or empty string 
	 */
	if ((!nvifname) || (!*nvifname) || (!osifname_buf)) {
		return -1;
	}

	if (strstr(nvifname, "eth") || strstr(nvifname, ".")) {
		strncpy(osifname_buf, nvifname, osifname_buf_len);
		return 0;
	}

	snprintf(varname, sizeof(varname), "%s_ifname", nvifname);
	ptr = nvram_get(varname);
	if (ptr) {
		/*
		 * Bail if the string is empty 
		 */
		if (!*ptr)
			return -1;
		strncpy(osifname_buf, ptr, osifname_buf_len);
		return 0;
	}

	return -1;
}

/*
 * osifname_to_nvifname()
 * 
 * Convert the OS interface name to the name we use
 * internally(NVRAM,GUI,etc.)
 * 
 * This is the Linux version of this function
 * 
 * @param osifname pointer to osifname to be converted @param nvifname_buf
 * storage for the converted ifname @param nvifname_buf_len length of storage 
 * for nvifname_buf 
 */

int osifname_to_nvifname(const char *osifname, char *nvifname_buf, int nvifname_buf_len)
{
	char varname[NVRAM_MAX_PARAM_LEN];
	int pri, sec;

	/*
	 * Bail if we get a NULL or empty string 
	 */

	if ((!osifname) || (!*osifname) || (!nvifname_buf)) {
		return -1;
	}

	bzero(nvifname_buf, nvifname_buf_len);

	if (strstr(osifname, "wl")) {
		strncpy(nvifname_buf, osifname, nvifname_buf_len);
		return 0;
	}

	/*
	 * look for interface name on the primary interfaces first 
	 */
	for (pri = 0; pri < MAX_NVPARSE; pri++) {
		snprintf(varname, sizeof(varname), "wl%d_ifname", pri);
		if (nvram_match(varname, (char *)osifname)) {
			snprintf(nvifname_buf, nvifname_buf_len, "wl%d", pri);
			return 0;
		}
	}

	/*
	 * look for interface name on the multi-instance interfaces 
	 */
	for (pri = 0; pri < MAX_NVPARSE; pri++)
		for (sec = 0; sec < MAX_NVPARSE; sec++) {
			snprintf(varname, sizeof(varname), "wl%d.%d_ifname", pri, sec);
			if (nvram_match(varname, (char *)osifname)) {
				snprintf(nvifname_buf, nvifname_buf_len, "wl%d.%d", pri, sec);
				return 0;
			}
		}

	return -1;

}

#endif

char *strcat_r(const char *s1, const char *s2, char *buf)
{
	strcpy(buf, s1);
	strcat(buf, s2);
	return buf;
}

int isListed(char *listname, char *value)
{
	char *next, word[32];
	char *list = nvram_get(listname);

	if (!list)
		return 0;
	foreach(word, list, next) {
		if (!strcmp(word, value))
			return 1;
	}
	return 0;
}

void addList(char *listname, char *value)
{
	int listlen = 0;

	if (isListed(listname, value))
		return;
	char *list = nvram_get(listname);
	char *newlist;

	if (list)
		listlen = strlen(list);
	newlist = safe_malloc(strlen(value) + listlen + 2);
	if (list) {
		sprintf(newlist, "%s %s", list, value);
	} else {
		strcpy(newlist, value);
	}
	nvram_set(listname, newlist);
	free(newlist);
}

size_t strlcpy_compat(register char *dst, register const char *src, size_t n)
{
	const char *src0 = src;
	char dummy[1];

	if (!n) {
		dst = dummy;
	} else {
		--n;
	}

	while ((*dst = *src) != 0) {
		if (n) {
			--n;
			++dst;
		}
		++src;
	}

	return src - src0;
}

int f_read_string(const char *path, char *buffer, int max);

char *psname(int pid, char *buffer, int maxlen)
{
	char buf[512];
	char path[64];
	char *p;

	if (maxlen <= 0)
		return NULL;
	*buffer = 0;
	sprintf(path, "/proc/%d/stat", pid);
	if ((f_read_string(path, buf, sizeof(buf)) > 4)
	    && ((p = strrchr(buf, ')')) != NULL)) {
		*p = 0;
		if (((p = strchr(buf, '(')) != NULL) && (atoi(buf) == pid)) {
			strlcpy_compat(buffer, p + 1, maxlen);
		}
	}
	return buffer;
}

int f_exists(const char *path)	// note: anything but a directory
{
	struct stat st;
	bzero(&st, sizeof(struct stat));

	return (stat(path, &st) == 0) && (!S_ISDIR(st.st_mode));
}

int f_read(const char *path, void *buffer, int max)
{
	int f;
	int n;

	if ((f = open(path, O_RDONLY)) < 0)
		return -1;
	n = read(f, buffer, max);
	close(f);
	return n;
}

int f_read_string(const char *path, char *buffer, int max)
{
	if (max <= 0)
		return -1;
	int n = f_read(path, buffer, max - 1);

	buffer[(n > 0) ? n : 0] = 0;
	return n;
}

int wait_file_exists(const char *name, int max, int invert)
{
	while (max-- > 0) {
		if (f_exists(name) ^ invert)
			return 1;
		sleep(1);
	}
	return 0;
}

int check_action(void)
{
	char buf[80] = "";

	if (file_to_buf(ACTION_FILE, buf, sizeof(buf))) {
		if (!strcmp(buf, "ACT_WEB_UPGRADE")) {
			fprintf(stderr, "Upgrading from web (http) now ...\n");
			return ACT_WEB_UPGRADE;
		}
#ifdef HAVE_HTTPS
		else if (!strcmp(buf, "ACT_WEBS_UPGRADE")) {
			fprintf(stderr, "Upgrading from web (https) now ...\n");
			return ACT_WEBS_UPGRADE;
		}
#endif
		else if (!strcmp(buf, "ACT_SW_RESTORE")) {
			fprintf(stderr, "Receiving restore command from web ...\n");
			return ACT_SW_RESTORE;
		} else if (!strcmp(buf, "ACT_HW_RESTORE")) {
			fprintf(stderr, "Receiving restore command from resetbutton ...\n");
			return ACT_HW_RESTORE;
		} else if (!strcmp(buf, "ACT_NVRAM_COMMIT")) {
			fprintf(stderr, "Committing nvram now ...\n");
			return ACT_NVRAM_COMMIT;
		} else if (!strcmp(buf, "ACT_ERASE_NVRAM")) {
			fprintf(stderr, "Erasing nvram now ...\n");
			return ACT_ERASE_NVRAM;
		}
	}
	// fprintf(stderr, "Waiting for upgrading....\n");
	return ACT_IDLE;
}

int file_to_buf(char *path, char *buf, int len)
{
	FILE *fp;

	bzero(buf, len);

	if ((fp = fopen(path, "r"))) {
		fgets(buf, len, fp);
		fclose(fp);
		return 1;
	}

	return 0;
}

int ishexit(char c)
{

	if (strchr("01234567890abcdefABCDEF", c) != (char *)0)
		return 1;

	return 0;
}

static int _pidof(const char *name, pid_t ** pids)
{
	const char *p;
	char *e;
	DIR *dir;
	struct dirent *de;
	pid_t i;
	int count;
	char buf[256];

	count = 0;
	*pids = NULL;
	if ((p = strchr(name, '/')) != NULL)
		name = p + 1;
	if ((dir = opendir("/proc")) != NULL) {
		while ((de = readdir(dir)) != NULL) {
			i = strtol(de->d_name, &e, 10);
			if (*e != 0)
				continue;
			if (strcmp(name, psname(i, buf, sizeof(buf))) == 0) {
				if ((*pids = realloc(*pids, sizeof(pid_t) * (count + 1))) == NULL) {
					return -1;
				}
				(*pids)[count++] = i;
			}
		}
	}
	closedir(dir);
	return count;
}

int pidof(const char *name)
{
	pid_t *pids;
	pid_t p;
	if (!name)
		return -1;
	if (_pidof(name, &pids) > 0) {
		p = *pids;
		free(pids);
		return p;
	}
	return -1;
}

int killall(const char *name, int sig)
{
	pid_t *pids;
	int i;
	int r;

	if ((i = _pidof(name, &pids)) > 0) {
		r = 0;
		do {
			r |= kill(pids[--i], sig);
		}
		while (i > 0);
		free(pids);
		return r;
	}
	return -2;
}

#undef sprintf
#undef snprintf

int dd_snprintf(char *str, int len, const char *fmt, ...)
{
	va_list ap;
	int n;
	char *dest;
	if (len < 1)
		return 0;
	va_start(ap, fmt);
	n = vasprintf(&dest, fmt, ap);
	va_end(ap);
	strncpy(str, dest, len - 1);
	str[len - 1] = '\0';
	free(dest);
	return n;
}

int getMTD(char *name)
{
	char buf[32];
	int device = -1;
	char dev[32];
	char size[32];
	char esize[32];
	char n[32];
	sprintf(buf, "\"%s\"", name);
	FILE *fp = fopen("/proc/mtd", "rb");
	if (!fp)
		return -1;
	while (!feof(fp) && fscanf(fp, "%s %s %s %s", dev, size, esize, n) == 4) {
		if (!strcmp(n, buf)) {
			if (dev[4] == ':') {
				device = dev[3] - '0';
			} else {
				device = 10 + (dev[4] - '0');
			}

			break;
		}
	}
	fclose(fp);
	return device;
}

int dd_sprintf(char *str, const char *fmt, ...)
{
	va_list ap;
	int n;
	char *dest;

	va_start(ap, fmt);
	n = vasprintf(&dest, fmt, ap);
	va_end(ap);
	strcpy(str, dest);
	free(dest);

	return n;
}

static void strcpyto(char *dest, char *src, char *delim)
{
	int cnt = 0;
	int len = strlen(src);
	char *to = strpbrk(src, delim);
	if (to)
		len = to - src;
	memcpy(dest, src, len);
	dest[len] = '\0';
}

char *chomp(char *s)
{
	char *c = (s) + strlen((s)) - 1;
	while ((c > (s)) && (*c == '\n' || *c == '\r' || *c == ' '))
		*c-- = '\0';
	return s;
}

char *foreach_first(char *foreachwordlist, char *word, char *delimiters)
{
	char *next = &foreachwordlist[strspn(foreachwordlist, delimiters)];
	strcpyto(word, next, delimiters);
	next = strpbrk(next, delimiters);
	return next;
}

char *foreach_last(char *next, char *word, char *delimiters)
{
	next = next ? &next[strspn(next, delimiters)] : "";
	strcpyto(word, next, delimiters);
	next = strpbrk(next, delimiters);
	return next;
}

static char *getentrybyidx_d(char *buf, char *list, int idx, char *delimiters_short, char *delimiters)
{
	char *next, word[128];
	if (!list || !buf)
		return NULL;
	int count = 0;
	foreach_delim(word, list, next, !count ? delimiters_short : delimiters) {
		if (count == idx) {
			strcpy(buf, word);
			return buf;
		}
		count++;
	}
	return NULL;
}

char *getentrybyidx(char *buf, char *list, int idx)
{
	return getentrybyidx_d(buf, list, idx, "><:,", "><:-,");
}

#if defined(HAVE_X86) || defined(HAVE_NEWPORT) || defined(HAVE_RB600) || defined(HAVE_EROUTER) && !defined(HAVE_WDR4900)
char *getdisc(void)		// works only for squashfs 
{
#ifdef HAVE_NEWPORT
	return "mmcblk0p1";
#else
	int i;
	static char ret[8];
	char *disks[] = { "sda2", "sdb2", "sdc2", "sdd2", "sde2", "sdf2", "sdg2", "sdh2",
		"sdi2", "mmcblk0p2", "mmcblk0p1"
	};
	for (i = 0; i < 10; i++) {
		char dev[64];

		sprintf(dev, "/dev/%s", disks[i]);
		FILE *in = fopen(dev, "rb");

		if (in == NULL)
			continue;	// no second partition or disc does not
		// exist, skipping
		char buf[4];

		fread(buf, 4, 1, in);
		if (!memcmp(&buf[0], "tqsh", 4)
		    || !memcmp(&buf[0], "hsqt", 4)
		    || !memcmp(&buf[0], "hsqs", 4)) {
			fclose(in);
			// filesystem detected
			bzero(ret, 8);
			if (strlen(disks[i]) == 4)
				strncpy(ret, disks[i], 3);
			else
				strncpy(ret, disks[i], 7);
			return ret;
		}
		fclose(in);
	}
	return NULL;
#endif
}
#endif

int nvram_commit(void)
{
#if defined(HAVE_WZRHPG300NH) || defined(HAVE_WHRHPGN) || defined(HAVE_WZRHPAG300NH) || defined(HAVE_DIR825) || defined(HAVE_TEW632BRP) || defined(HAVE_TG2521) || defined(HAVE_WR1043)  || defined(HAVE_WRT400) || defined(HAVE_WZRHPAG300NH) || defined(HAVE_WZRG450) || defined(HAVE_DANUBE) || defined(HAVE_WR741) || defined(HAVE_NORTHSTAR) || defined(HAVE_DIR615I) || defined(HAVE_WDR4900) || defined(HAVE_VENTANA) || defined(HAVE_UBNTM)
	eval("ledtool", "1");
#elif HAVE_LSX
	//nothing
#elif HAVE_XSCALE
	//nothing
#else
	eval("ledtool", "1");
#endif
	_nvram_commit();
}

#ifdef MEMDEBUG
#define MEMDEBUGSIZE 1024
typedef struct MEMENTRY {
	void *reference;
	int size;
	char func[32];
	int dirty;
};

static unsigned int memdebugpnt = 0;
static struct MEMENTRY mementry[MEMDEBUGSIZE];
void *mymalloc(int size, char *func)
{
	if (memdebugpnt >= MEMDEBUGSIZE)
		return safe_malloc(size);
	mementry[memdebugpnt].size = size;
	mementry[memdebugpnt].dirty = 1;
	void *ref = malloc(size);
	mementry[memdebugpnt].reference = ref;
	strncpy(mementry[memdebugpnt].func, func, 32);
	memdebugpnt++;
	if (memdebugpnt == MEMDEBUGSIZE)
		memdebugpnt = 0;
	return ref;
}

#undef free
void myfree(void *ref)
{
	int i;
	for (i = 0; i < MEMDEBUGSIZE; i++) {
		if (mementry[i].reference == ref && mementry[i].dirty) {
//      fprintf(stderr,"free from %s\n",mementry[i].func);
			mementry[i].dirty = 0;
			break;
		}
	}
	free(ref);
}

void showmemdebugstat(void)
{
	int i;
	for (i = 0; i < MEMDEBUGSIZE; i++) {
		if (mementry[i].dirty) {
			fprintf(stderr, "%s leaks %d bytes\n", mementry[i].func, mementry[i].size);
			mementry[i].dirty = 0;
		}
	}
}

#endif
