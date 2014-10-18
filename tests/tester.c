/* 
 * Copyright (c) 2010-2012 Joshua R. Elsasser.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef OS_TYPE_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* XXX dependencies from this file are hardcoded in build.mk */
#include "util.h"

#define UPART_PATH	"../upart"
#define TESTDIR_PATH	"tests"
#define TESTINDEX_PATH	"index.txt"

#ifdef OS_TYPE_WINDOWS
#define RMFILE_DISPLAY	"\tdel"
#define DIRSEP_DISPLAY	"\\"
#else
#define RMFILE_DISPLAY	"rm -f"
#define DIRSEP_DISPLAY	"/"
#endif

#ifndef NITEMS
#define NITEMS(a)		(sizeof(a) / sizeof((a)[0]))
#endif

#ifdef OS_TYPE_WINDOWS
/* grumble grumble */
#define strdup _strdup
#endif

void	 cleanfiles(FILE *);
void	 regenfiles(FILE *);
void	 testfiles(FILE *);
char	*nextname(const char *, FILE *);
char	*strjoin(const char *, ...) ATTR_SENTINEL(0);
int	 checkexitval(const char *, int, const char *);
int	 checkfiles(const char *, char *, char *, int);
char	*appendname(char *, const char *);

char	*getmyname(char *);
void	 fail(const char *, ...) ATTR_PRINTF(1, 2);
void	 changedir(void);
void	 rmfile(const char *);
int	 diff(char *, char *);
int	 runtest(char *, char *, const char *, const char *);
off_t	 filesize(const char *);
FILE	*maybefopen(const char *, const char *);

#ifdef OS_TYPE_UNIX
int	runprog(const char *, char *const *, const char *, const char *, int);
#endif


static char * const flags[] = { "", "-v", "-vv" };

char *myname;
int verbose;

int
main(int argc, char *argv[])
{
	void (*mode)(FILE *);
	FILE *idx;
	int opt;

	myname = getmyname(argv[0]);

	mode = testfiles;
	while ((opt = getopt(argc, argv, "chrtv")) != -1) {
		switch (opt) {
		case 'c':
			mode = cleanfiles;
			break;
		case 'r':
			mode = regenfiles;
			break;
		case 't':
			mode = testfiles;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			printf("usage: %s [-chr] -f path -u path\n"
			    "  -c       clean test output\n"
			    "  -h       show this message and exit.\n"
			    "  -r       regenerate test output.\n"
			    "  -t       run tests (the default)\n"
			    "  -v       verbose mode\n",
			    myname);
			exit(EXIT_FAILURE);
		}
	}

	changedir();

	if ((idx = fopen(TESTINDEX_PATH, "r")) == NULL)
		fail("failed to open %s for reading", TESTINDEX_PATH);

	(*mode)(idx);

	return (0);
}

void
cleanfiles(FILE *idx)
{
	char *name, *outfile, *errfile;
	size_t i;

	while ((name = nextname(TESTINDEX_PATH, idx)) != NULL) {
		printf("%s", RMFILE_DISPLAY);
		for (i = 0; i < NITEMS(flags); i++) {
			outfile = strjoin("test-", name, flags[i], ".out", (void *)NULL);
			errfile = strjoin("test-", name, flags[i], ".err", (void *)NULL);
			printf(" %s%s%s %s%s%s",
			    TESTDIR_PATH, DIRSEP_DISPLAY, outfile,
			    TESTDIR_PATH, DIRSEP_DISPLAY, errfile);
			rmfile(outfile);
			rmfile(errfile);
			free(outfile);
			free(errfile);
		}
		printf("\n");
	}
}

void
regenfiles(FILE *idx)
{
	char *imgfile, *outfile, *errfile, *exitfile;
	int exitval;
	char *name;
	size_t i;

	while ((name = nextname(TESTINDEX_PATH, idx)) != NULL) {
		for (i = 0; i < NITEMS(flags); i++) {
			imgfile = strjoin(name, ".img", (void *)NULL);
			outfile = strjoin(name, flags[i], ".out", (void *)NULL);
			errfile = strjoin(name, flags[i], ".err", (void *)NULL);
			exitfile = strjoin(name, flags[i], ".exit", (void *)NULL);

			rmfile(outfile);
			rmfile(errfile);
			rmfile(exitfile);
			exitval = runtest(flags[i], imgfile, outfile, errfile);

			if (filesize(outfile) == 0)
				rmfile(outfile);
			if (filesize(errfile) == 0)
				rmfile(errfile);
			if (exitval != 0) {
				FILE *eh;

				if ((eh = fopen(exitfile, "w")) == NULL)
					fail("failed to open %s for writing",
					    exitfile);
				fprintf(eh, "%d\n", exitval);
				fclose(eh);
			}

			free(imgfile);
			free(outfile);
			free(errfile);
			free(exitfile);
		}
	}
}

void
testfiles(FILE *idx)
{
	char *bad, *name, *fullname, *imgfile, *outfile, *errfile, *exitfile;
	char *newoutfile, *newerrfile;
	int exitval, failed, failures, testcount;
	size_t i;

	printf("running tests...\n");

	bad = NULL;
	testcount = 0;
	failures = 0;
	while ((name = nextname(TESTINDEX_PATH, idx)) != NULL) {
		for (i = 0; i < NITEMS(flags); i++) {
			fullname = strjoin(name, flags[i], (void *)NULL);
			imgfile = strjoin(name, ".img", (void *)NULL);
			outfile = strjoin(fullname, ".out", (void *)NULL);
			errfile = strjoin(fullname, ".err", (void *)NULL);
			exitfile = strjoin(fullname, ".exit", (void *)NULL);
			newoutfile = strjoin("test-", fullname, ".out", (void *)NULL);
			newerrfile = strjoin("test-", fullname, ".err", (void *)NULL);

			rmfile(newoutfile);
			rmfile(newerrfile);
			exitval = runtest(flags[i], imgfile,
			    newoutfile, newerrfile);
			failed = 0;

			if (!checkexitval(fullname, exitval, exitfile))
				failed = 1;
			if (!checkfiles(fullname, outfile, newoutfile, 0))
				failed = 1;
			if (!checkfiles(fullname, errfile, newerrfile, 1))
				failed = 1;

			testcount++;
			if (failed) {
				failures++;
				bad = appendname(bad, fullname);
			}

			free(fullname);
			free(imgfile);
			free(outfile);
			free(errfile);
			free(exitfile);
			free(newoutfile);
			free(newerrfile);
		}
	}

	printf("%d tests failed out of %d total.\n", failures, testcount);
	if (failures != 0) {
		printf("The following tests failed:\n  %s\n", bad);
		exit(EXIT_FAILURE);
	}
}

char *
nextname(const char *filename, FILE *fh)
{
	static char buf[MAXPATHLEN+1];
	size_t off;
	int ch;

	off = 0;
	while ((ch = getc(fh)) != EOF) {
		if (ch == '\n') {
			if (off != 0)
				break;
		} else {
			buf[off] = ch;
			off++;
		}
	}

	if (ferror(fh))
		fail("failed to read from %s", filename);
	else if (off != 0) {
		buf[off] = '\0';
		return (buf);
	}
	return (NULL);
}

char *
strjoin(const char *first, ...)
{
	const char *arg;
	va_list ap;
	size_t len, i;
	char *str;

	va_start(ap, first);
	len = 0;
	for (arg = first; arg != NULL; arg = va_arg(ap, const char *))
		len += strlen(arg);
	va_end(ap);

	if ((str = malloc(len + 1)) == NULL)
		fail("failed to allocate memory");

	va_start(ap, first);
	len = 0;
	for (arg = first; arg != NULL; arg = va_arg(ap, const char *)) {
		for (i = 0; arg[i] != '\0'; i++)
			str[len+i] = arg[i];
		len += i;
	}
	str[len] = '\0';
	va_end(ap);

	return (str);
}

int
checkexitval(const char *name, int got, const char *file)
{
	char buf[4];
	size_t len;
	FILE *fh;
	int want;

	if ((fh = maybefopen(file, "r")) == NULL)
		want = 0;
	else {
		len = fread(buf, 1, sizeof(buf) - 1, fh);
		buf[len] = '\0';
		if (ferror(fh))
			fail("failed to read from %s", file);
		fclose(fh);
		want = atoi(buf);
	}

	if (want == got)
		return (1);
	else {
		printf("%s: expected exit status %d but got %d\n",
		    name, want, got);
		return (0);
	}
}

int
checkfiles(const char *name, char *want, char *got, int err)
{
	FILE *wh, *gh;
	int wch, gch;

	wh = maybefopen(want, "r");
	gh = maybefopen(got, "r");

	if (gh == NULL) {
		fprintf(stderr, "error: %s does not exist\n", got);
		exit(EXIT_FAILURE);
	}

	if (wh == NULL) {
		fclose(gh);
		if (filesize(got) == 0)
			return (1);
		goto mismatch;
	}

	do {
		wch = getc(wh);
		gch = getc(gh);
	} while (wch == gch && wch != EOF);
	fclose(wh);
	fclose(gh);

	if (wch == gch)
		return (1);
mismatch:
	printf("%s: standard %s does not match expected\n",
	    name, (err ? "error" : "output"));
	if (verbose)
		diff(want, got);
	return (0);
}

char *
appendname(char *str, const char *name)
{
	size_t len1, len2;

	if (str == NULL) {
		if ((str = strdup(name)) == NULL)
			fail("failed to allocate memory");
	} else {
		len1 = strlen(str);
		len2 = strlen(name);
		if ((str = realloc(str, len1 + 1 + len2 + 1)) == NULL)
			fail("failed to allocate memory");
		str[len1] = ' ';
		memcpy(str + len1 + 1, name, len2 + 1);
	}
	return (str);
}

#ifdef OS_TYPE_UNIX

char *
getmyname(char *argv0)
{
	char *name;

	if ((name = strrchr(argv0, '/')) != NULL && *(++name) != '\0')
		return (name);
	else
		return (argv0);
}

void
fail(const char *format, ...)
{
	char buf[1024];
	va_list ap;
	ssize_t stupid_fucking_nanny_compiler;
	int off;

	off = snprintf(buf, sizeof(buf), "%s: ", myname);
	if (off > 0 && off < sizeof(buf)) {
		va_start(ap, format);
		off += vsnprintf(buf + off, sizeof(buf) - off, format, ap);
		va_end(ap);
	}
	if (off > 0 && off < sizeof(buf))
		snprintf(buf + off, sizeof(buf) - off, ": %s",
		    strerror(errno));
	off = strlen(buf);
	buf[off] = '\n';
	stupid_fucking_nanny_compiler = write(STDERR_FILENO, buf, off + 1);
	exit(EXIT_FAILURE);
}

void
changedir(void)
{
	if (chdir(TESTDIR_PATH) != 0)
		fail("failed to change directory to %s", TESTDIR_PATH);
}

void
rmfile(const char *path)
{
	if (unlink(path) != 0 && errno != ENOENT)
		fail("failed to remove %s", path);
}

int
diff(char *old, char *new)
{
	char *argv[] = { "diff", "-u", NULL, NULL, NULL };

	argv[2] = old;
	argv[3] = new;

	return (runprog("diff", argv, NULL, NULL, 1));
}

int
runtest(char *flags, char *img, const char *out, const char *err)
{
	char *argv[4];
	int i;

	i = 0;
	argv[i++] = UPART_PATH;
	if (flags && flags[0] != '\0')
		argv[i++] = flags;
	argv[i++] = img;
	argv[i++] = NULL;

	return (runprog(UPART_PATH, argv, out, err, 0));
}

int
runprog(const char *prog, char *const argv[],
    const char *out, const char *err, int search)
{
	int ifd, ofd, efd, status;
	char errbuf[64];
	pid_t pid;

	/* shut up a spurious compiler warning */
	ofd = efd = -1;

	if ((ifd = open("/dev/null", O_RDONLY, 0)) == -1)
		fail("failed to open %s for reading", "/dev/null");
	if (out && (ofd = open(out, O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1)
		fail("failed to open %s for writing", out);
	if (err && (efd = open(err, O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1)
		fail("failed to open %s for writing", err);

	if ((pid = fork()) == -1)
		fail("failed to fork");

	if (pid != 0) {
		close(ifd);
		if (out)
			close(ofd);
		if (err)
			close(efd);
		if (waitpid(pid, &status, 0) == -1)
			fail("waitpid() failed");
		return (WEXITSTATUS(status));
	}

	if (dup2(ifd, STDIN_FILENO) == -1 ||
	    (out && dup2(ofd, STDOUT_FILENO) == -1) ||
	    (err && dup2(efd, STDERR_FILENO) == -1)) {
		perror("dup2() failed");
		_exit(EXIT_FAILURE);
	}
	close(ifd);
	if (out)
		close(ofd);
	if (err)
		close(efd);

	if (search)
		execvp(prog, argv);
	else
		execv(prog, argv);
	snprintf(errbuf, sizeof(errbuf), "failed to execute %s", prog);
	perror(errbuf);
	_exit(EXIT_FAILURE);
}

off_t
filesize(const char *path)
{
	struct stat sb;

	if (stat(path, &sb) != 0)
		fail("stat() failed for %s", path);

	return (sb.st_size);
}

FILE *
maybefopen(const char *path, const char *mode)
{
	FILE *fh;

	if ((fh = fopen(path, mode)) == NULL && errno != ENOENT)
		fail("failed to open %s for %s",
		    path, (*mode == 'r' ? "reading" :
			((*mode == 'w' || *mode == 'a') ? "writing" :
			    "???")));
	return (fh);
}

#endif /* OS_TYPE_UNIX */

#ifdef OS_TYPE_WINDOWS

char *
getmyname(char *argv0)
{
	char *fwd, *back;

	if ((fwd = strrchr(argv0, '/')) != NULL)
		fwd++;
	if ((back = strrchr(argv0, '\\')) != NULL)
		back++;
	if (fwd != NULL && fwd[0] != '\0' && fwd > back)
		return (fwd);
	else if (back != NULL && back[0] != '\0')
		return (back);
	else
		return (argv0);
}

void
fail(const char *format, ...)
{
	char buf[1024];
	va_list ap;
	DWORD err;
	int off;

	err = GetLastError();
	off = snprintf(buf, sizeof(buf), "%s: ", myname);
	if (off > 0 && off < sizeof(buf)) {
		va_start(ap, format);
		off += vsnprintf(buf + off, sizeof(buf) - off, format, ap);
		va_end(ap);
	}
	if (off > 0 && off < sizeof(buf))
		buf[off++] = ':';
	if (off > 0 && off < sizeof(buf))
		buf[off++] = ' ';
	if (off > 0 && off < sizeof(buf))
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |
		    FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0,
		    buf + off, sizeof(buf) - off, NULL);
	off = strlen(buf);
	buf[off] = '\n';
	WriteFile(GetStdHandle(STD_ERROR_HANDLE), buf, off + 1, NULL, NULL);
	exit(EXIT_FAILURE);
}

void
changedir(void)
{
	if (!SetCurrentDirectory(TESTDIR_PATH))
		fail("failed to change directory to %s", TESTDIR_PATH);
}

void
rmfile(const char *path)
{
	if (!DeleteFile(path) && GetLastError() != ERROR_FILE_NOT_FOUND)
		fail("failed to remove %s", path);
}

int
diff(char *old, char *new)
{
	return (0);
}

int
runtest(const char *args, const char *img, const char *out, const char *err)
{
	SECURITY_ATTRIBUTES sa;
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	HANDLE nh, oh, eh;
	DWORD ecode;
	char *cmd;

	/* XXX should do escaping here */
	cmd = strjoin(UPART_PATH, " ", args, " ", img, (void *)NULL);

	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	if ((nh = CreateFile("NUL", GENERIC_READ|GENERIC_WRITE,
		    FILE_SHARE_READ|FILE_SHARE_WRITE, &sa,
		    OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE)
		fail("failed to open NUL device");
	if ((oh = CreateFile(out, GENERIC_WRITE, FILE_SHARE_READ, &sa,
		    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL))
	    == INVALID_HANDLE_VALUE)
		fail("failed to open %s for writing", out);
	if ((eh = CreateFile(err, GENERIC_WRITE, FILE_SHARE_READ, &sa,
		    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL))
	    == INVALID_HANDLE_VALUE)
		fail("failed to open %s for writing", err);

	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = nh;
	si.hStdOutput = oh;
	si.hStdError = eh;

	if (!CreateProcess(NULL, cmd, NULL, NULL, 1, 0, NULL, NULL, &si, &pi))
		fail("failed to create process %s", cmd);

	if (WaitForSingleObject(pi.hProcess, INFINITE) == WAIT_FAILED)
		fail("failed to wait for child process to finish");

	if (!GetExitCodeProcess(pi.hProcess, &ecode))
		fail("failed to get child process exit code");

	free(cmd);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	CloseHandle(nh);
	CloseHandle(eh);
	CloseHandle(oh);

	return (ecode);
}

off_t
filesize(const char *path)
{
	WIN32_FILE_ATTRIBUTE_DATA fi;

	if (GetFileAttributesEx(path, GetFileExInfoStandard, &fi) == 0)
		fail("failed to get file attributes for %s", path);

	return ((off_t)fi.nFileSizeHigh << 32 | fi.nFileSizeLow);
}

FILE *
maybefopen(const char *path, const char *mode)
{
	FILE *fh;

	if ((fh = fopen(path, mode)) == NULL &&
	    GetLastError() != ERROR_FILE_NOT_FOUND)
		fail("failed to open %s for %s",
		    path, (*mode == 'r' ? "reading" :
			((*mode == 'w' || *mode == 'a') ? "writing" :
			    "???")));
	return (fh);
}

#endif /* OS_TYPE_WINDOWS */
