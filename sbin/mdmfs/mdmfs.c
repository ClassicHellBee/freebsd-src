/*
 * Copyright (c) 2001 Dima Dorfman.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * mdmfs (md/MFS) is a wrapper around mdconfig(8),
 * newfs(8), and mount(8) that mimics the command line option set of
 * the deprecated mount_mfs(8).
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mdioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <grp.h>
#include <paths.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum { false, true } bool;

struct mtpt_info {
	uid_t		 mi_uid;
	bool		 mi_have_uid;
	gid_t		 mi_gid;
	bool		 mi_have_gid;
	mode_t		 mi_mode;
	bool		 mi_have_mode;
};

static	bool compat;		/* Full compatibility with mount_mfs? */
static	bool debug;		/* Emit debugging information? */
static	bool loudsubs;		/* Suppress output from helper programs? */
static	bool norun;		/* Actually run the helper programs? */
static	int unit;      		/* The unit we're working with. */
static	const char *mdname;	/* Name of memory disk device (e.g., "md"). */
static	size_t mdnamelen;	/* Length of mdname. */

static void	 argappend(char **, const char *, ...) __printflike(2, 3);
static void	 debugprintf(const char *, ...) __printflike(1, 2);
static void	 do_mdconfig_attach(const char *, const enum md_types);
static void	 do_mdconfig_attach_au(const char *, const enum md_types);
static void	 do_mdconfig_detach(void);
static void	 do_mount(const char *, const char *);
static void	 do_mtptsetup(const char *, struct mtpt_info *);
static void	 do_newfs(const char *);
static void	 extract_ugid(const char *, struct mtpt_info *);
static int	 run(int *, const char *, ...) __printflike(2, 3);
static void	 usage(void);

int
main(int argc, char **argv)
{
	struct mtpt_info mi;		/* Mountpoint info. */
	char *mdconfig_arg, *newfs_arg,	/* Args to helper programs. */
	    *mount_arg;
	enum md_types mdtype;		/* The type of our memory disk. */
	bool have_mdtype;
	bool detach, softdep, autounit;
	char *mtpoint, *unitstr;
	char *p;
	int ch;
	void *set;

	/* Misc. initialization. */
	(void)memset(&mi, '\0', sizeof(mi));
	detach = true;
	softdep = true;
	autounit = false;
	have_mdtype = false;
	mdname = MD_NAME;
	mdnamelen = strlen(mdname);
	/*
	 * Can't set these to NULL.  They may be passed to the
	 * respective programs without modification.  I.e., we may not
	 * receive any command-line options which will caused them to
	 * be modified.
	 */
	mdconfig_arg = strdup("");
	newfs_arg = strdup("");
	mount_arg = strdup("");

	/* If we were started as mount_mfs or mfs, imply -C. */
	if (strcmp(getprogname(), "mount_mfs") == 0 ||
	    strcmp(getprogname(), "mfs") == 0)
		compat = true;

	while ((ch = getopt(argc, argv,
	    "a:b:Cc:Dd:e:F:f:hi:LlMm:Nn:O:o:p:Ss:t:Uv:w:X")) != -1)
		switch (ch) {
		case 'a':
			argappend(&newfs_arg, "-a %s", optarg);
			break;
		case 'b':
			argappend(&newfs_arg, "-b %s", optarg);
			break;
		case 'C':
			if (compat)
				usage();
			compat = true;
			break;
		case 'c':
			argappend(&newfs_arg, "-c %s", optarg);
			break;
		case 'D':
			if (compat)
				usage();
			detach = false;
			break;
		case 'd':
			argappend(&newfs_arg, "-d %s", optarg);
			break;
		case 'e':
			argappend(&newfs_arg, "-e %s", optarg);
			break;
		case 'F':
			if (have_mdtype)
				usage();
			mdtype = MD_VNODE;
			have_mdtype = true;
			argappend(&mdconfig_arg, "-f %s", optarg);
			break;
		case 'f':
			argappend(&newfs_arg, "-f %s", optarg);
			break;
		case 'h':
			usage();
			break;
		case 'i':
			argappend(&newfs_arg, "-i %s", optarg);
			break;
		case 'L':
			if (compat)
				usage();
			loudsubs = true;
			break;
		case 'l':
			argappend(&newfs_arg, "-l");
			break;
		case 'M':
			if (have_mdtype)
				usage();
			mdtype = MD_MALLOC;
			have_mdtype = true;
			break;
		case 'm':
			argappend(&newfs_arg, "-m %s", optarg);
			break;
		case 'N':
			if (compat)
				usage();
			norun = true;
			break;
		case 'n':
			argappend(&newfs_arg, "-n %s", optarg);
			break;
		case 'O':
			argappend(&newfs_arg, "-o %s", optarg);
			break;
		case 'o':
			argappend(&mount_arg, "-o %s", optarg);
			break;
		case 'p':
			if (compat)
				usage();
			if ((set = setmode(optarg)) == NULL)
				usage();
			mi.mi_mode = getmode(set, S_IRWXU | S_IRWXG | S_IRWXO);
			mi.mi_have_mode = true;
			free(set);
			break;
		case 'S':
			if (compat)
				usage();
			softdep = false;
			break;
		case 's':
			argappend(&mdconfig_arg, "-s %s", optarg);
			break;
		case 'U':
			softdep = true;
			break;
		case 'v':
			argappend(&newfs_arg, "-O %s", optarg);
			break;
		case 'w':
			if (compat)
				usage();
			extract_ugid(optarg, &mi);
			break;
		case 'X':
			if (compat)
				usage();
			debug = true;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (argc < 2)
		usage();

	/* Make compatibility assumptions. */
	if (compat) {
		mi.mi_mode = 01777;
		mi.mi_have_mode = true;
	}

	/* Derive 'unit' (global). */
	unitstr = argv[0];
	if (strncmp(unitstr, "/dev/", 5) == 0)
		unitstr += 5;
	if (strncmp(unitstr, mdname, mdnamelen) == 0)
		unitstr += mdnamelen;
	if (*unitstr == '\0') {
		autounit = true;
		unit = -1;
	} else {
		unit = strtoul(unitstr, &p, 10);
		if (unit == (unsigned)ULONG_MAX || *p != '\0')
			errx(1, "bad device unit: %s", unitstr);
	}

	mtpoint = argv[1];
	if (!have_mdtype)
		mdtype = MD_SWAP;
	if (softdep)
		argappend(&newfs_arg, "-U");

	/* Do the work. */
	if (detach && !autounit)
		do_mdconfig_detach();
	if (autounit)
		do_mdconfig_attach_au(mdconfig_arg, mdtype);
	else
		do_mdconfig_attach(mdconfig_arg, mdtype);
	do_newfs(newfs_arg);
	do_mount(mount_arg, mtpoint);
	do_mtptsetup(mtpoint, &mi);

	return (0);
}

/*
 * Append the expansion of 'fmt' to the buffer pointed to by '*dstp';
 * reallocate as required.
 */
static void
argappend(char **dstp, const char *fmt, ...)
{
	char *old, *new;
	va_list ap;
	
	old = *dstp;
	assert(old != NULL);

	va_start(ap, fmt);
	if (vasprintf(&new, fmt,ap) == -1)
		errx(1, "vasprintf");
	va_end(ap);

	*dstp = new;
	if (asprintf(&new, "%s %s", old, new) == -1)
		errx(1, "asprintf");
	free(*dstp);
	free(old);

	*dstp = new;
}

/*
 * If run-time debugging is enabled, print the expansion of 'fmt'.
 * Otherwise, do nothing.
 */
static void
debugprintf(const char *fmt, ...)
{
	va_list ap;

	if (!debug)
		return;
	fprintf(stderr, "DEBUG: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	fflush(stderr);
}

/*
 * Attach a memory disk with a known unit.
 */
static void
do_mdconfig_attach(const char *args, const enum md_types mdtype)
{
	int rv;
	const char *ta;		/* Type arg. */

	switch (mdtype) {
	case MD_SWAP:
		ta = "-t swap";
		break;
	case MD_VNODE:
		ta = "-t vnode";
		break;
	case MD_MALLOC:
		ta = "-t malloc";
		break;
	default:
		abort();
	}
	rv = run(NULL, "%s -a %s%s -u %s%d", _PATH_MDCONFIG, ta, args,
	    mdname, unit);
	if (rv)
		errx(1, "mdconfig (attach) exited with error code %d", rv);
}

/*
 * Attach a memory disk with an unknown unit; use autounit.
 */
static void
do_mdconfig_attach_au(const char *args, const enum md_types mdtype)
{
	const char *ta;		/* Type arg. */
	char *linep, *linebuf; 	/* Line pointer, line buffer. */
	int fd;			/* Standard output of mdconfig invocation. */
	FILE *sfd;
	int rv;
	char *p;
	size_t linelen;

	switch (mdtype) {
	case MD_SWAP:
		ta = "-t swap";
		break;
	case MD_VNODE:
		ta = "-t vnode";
		break;
	case MD_MALLOC:
		ta = "-t malloc";
		break;
	default:
		abort();
	}
	rv = run(&fd, "%s -a %s%s", _PATH_MDCONFIG, ta, args);
	if (rv)
		errx(1, "mdconfig (attach) exited with error code %d", rv);

	/* Receive the unit number. */
	if (norun) {	/* Since we didn't run, we can't read.  Fake it. */
		unit = -1;
		return;
	}
	sfd = fdopen(fd, "r");
	if (sfd == NULL)
		err(1, "fdopen");
	linep = fgetln(sfd, &linelen);
	if (linep == NULL && linelen < mdnamelen + 1)
		errx(1, "unexpected output from mdconfig (attach)");
	/* If the output format changes, we want to know about it. */
	assert(strncmp(linep, mdname, mdnamelen) == 0);
	linebuf = malloc(linelen - mdnamelen + 1);
	assert(linebuf != NULL);
	/* Can't use strlcpy because linep is not NULL-terminated. */
	strncpy(linebuf, linep + mdnamelen, linelen);
	linebuf[linelen] = '\0';
	unit = strtoul(linebuf, &p, 10);
	if (unit == (unsigned)ULONG_MAX || *p != '\n')
		errx(1, "unexpected output from mdconfig (attach)");

	fclose(sfd);
	close(fd);
}

/*
 * Detach a memory disk.
 */
static void
do_mdconfig_detach(void)
{
	int rv;

	rv = run(NULL, "%s -d -u %s%d", _PATH_MDCONFIG, mdname, unit);
	if (rv && debug)	/* This is allowed to fail. */
		warnx("mdconfig (detach) exited with error code %d (ignored)",
		      rv);
}

/*
 * Mount the configured memory disk.
 */
static void
do_mount(const char *args, const char *mtpoint)
{
	int rv;

	rv = run(NULL, "%s%s /dev/%s%d %s", _PATH_MOUNT, args,
	    mdname, unit, mtpoint);
	if (rv)
		errx(1, "mount exited with error code %d", rv);
}

/*
 * Various configuration of the mountpoint.  Mostly, enact 'mip'.
 */
static void
do_mtptsetup(const char *mtpoint, struct mtpt_info *mip)
{

	if (mip->mi_have_mode) {
		debugprintf("changing mode of %s to %o.", mtpoint,
		    mip->mi_mode);
		if (!norun)
			if (chmod(mtpoint, mip->mi_mode) == -1)
				err(1, "chmod: %s", mtpoint);
	}
	/*
	 * We have to do these separately because the user may have
	 * only specified one of them.
	 */
	if (mip->mi_have_uid) {
		debugprintf("changing owner (user) or %s to %u.", mtpoint,
		    mip->mi_uid);
		if (!norun)
			if (chown(mtpoint, mip->mi_uid, -1) == -1)
				err(1, "chown %s to %u (user)", mtpoint,
				    mip->mi_uid);
	}
	if (mip->mi_have_gid) {
		debugprintf("changing owner (group) or %s to %u.", mtpoint,
		    mip->mi_gid);
		if (!norun)
			if (chown(mtpoint, -1, mip->mi_gid) == -1)
				err(1, "chown %s to %u (group)", mtpoint,
				    mip->mi_gid);
	}
}

/*
 * Put a file system on the memory disk.
 */
static void
do_newfs(const char *args)
{
	int rv;

	rv = run(NULL, "%s%s /dev/%s%d", _PATH_NEWFS, args, mdname, unit);
	if (rv)
		errx(1, "newfs exited with error code %d", rv);
}

/*
 * 'str' should be a user and group name similar to the last argument
 * to chown(1); i.e., a user, followed by a colon, followed by a
 * group.  The user and group in 'str' may be either a [ug]id or a
 * name.  Upon return, the uid and gid fields in 'mip' will contain
 * the uid and gid of the user and group name in 'str', respectively.
 *
 * In other words, this derives a user and group id from a string
 * formatted like the last argument to chown(1).
 */
static void
extract_ugid(const char *str, struct mtpt_info *mip)
{
	char *ug;			/* Writable 'str'. */
	char *user, *group;		/* Result of extracton. */
	struct passwd *pw;
	struct group *gr;
	char *p;
	uid_t *uid;
	gid_t *gid;

	uid = &mip->mi_uid;
	gid = &mip->mi_gid;
	mip->mi_have_uid = mip->mi_have_gid = false;

	/* Extract the user and group from 'str'.  Format above. */
	ug = strdup(str);
	assert(ug != NULL);
	group = ug;
	user = strsep(&group, ":");
	if (user == NULL || group == NULL || *user == '\0' || *group == '\0')
		usage();

	/* Derive uid. */
	*uid = strtoul(user, &p, 10);
	if (*uid == (uid_t)ULONG_MAX)
		usage();
	if (*p != '\0') {
		pw = getpwnam(user);
		if (pw == NULL)
			errx(1, "invalid user: %s", user);
		*uid = pw->pw_uid;
		mip->mi_have_uid = true;
	}

	/* Derive gid. */
	*gid = strtoul(group, &p, 10);
	if (*gid == (gid_t)ULONG_MAX)
		usage();
	if (*p != '\0') {
		gr = getgrnam(group);
		if (gr == NULL)
			errx(1, "invalid group: %s", group);
		*gid = gr->gr_gid;
		mip->mi_have_gid = true;
	}

	free(ug);
	/*
	 * At this point we don't support only a username or only a
	 * group name.  do_mtptsetup already does, so when this
	 * feature is desired, this is the only routine that needs to
	 * be changed.
	 */
	assert(mip->mi_have_uid);
	assert(mip->mi_have_gid);
}

/*
 * Run a process with command name and arguments pointed to by the
 * formatted string 'cmdline'.  Since system(3) is not used, the first
 * space-delimited token of 'cmdline' must be the full pathname of the
 * program to run.  The return value is the return code of the process
 * spawned.  If 'ofd' is non-NULL, it is set to the standard output of
 * the program spawned (i.e., you can read from ofd and get the output
 * of the program).
 */
static int
run(int *ofd, const char *cmdline, ...)
{
	char **argv, **argvp;		/* Result of splitting 'cmd'. */
	int argc;
	char *cmd;			/* Expansion of 'cmdline'. */
	int pid, status;		/* Child info. */
	int pfd[2];			/* Pipe to the child. */
	int nfd;			/* Null (/dev/null) file descriptor. */
	bool dup2dn;			/* Dup /dev/null to stdout? */
	va_list ap;
	char *p;
	int rv, i;

	dup2dn = true;
	va_start(ap, cmdline);
	rv = vasprintf(&cmd, cmdline, ap);
	if (rv == -1)
		err(1, "vasprintf");
	va_end(ap);

	/* Split up 'cmd' into 'argv' for use with execve. */
	for (argc = 1, p = cmd; (p = strchr(p, ' ')) != NULL; p++)
		argc++;		/* 'argc' generation loop. */
	argv = (char **)malloc(sizeof(*argv) * (argc + 1));
	assert(argv != NULL);
	for (p = cmd, argvp = argv; (*argvp = strsep(&p, " ")) != NULL;)
		if (**argv != '\0')
			if (++argvp >= &argv[argc]) {
				*argvp = NULL;
				break;
			}
	assert(*argv);

	/* Make sure the above loop works as expected. */
	if (debug) {
		/*
		 * We can't, but should, use debugprintf here.  First,
		 * it appends a trailing newline to the output, and
		 * second it prepends "DEBUG: " to the output.  The
		 * former is a problem for this would-be first call,
		 * and the latter for the would-be call inside the
		 * loop.
		 */
		(void)fprintf(stderr, "DEBUG: running:");
		/* Should be equivilent to 'cmd' (before strsep, of course). */
		for (i = 0; argv[i] != NULL; i++)
			(void)fprintf(stderr, " %s", argv[i]);
		(void)fprintf(stderr, "\n");
	}

	/* Create a pipe if necessary and fork the helper program. */
	if (ofd != NULL) {
		if (pipe(&pfd[0]) == -1)
			err(1, "pipe");
		*ofd = pfd[0];
		dup2dn = false;
	}
	pid = fork();
	switch (pid) {
	case 0:
		/* XXX can we call err() in here? */
		if (norun)
			_exit(0);
		if (ofd != NULL)
			if (dup2(pfd[1], STDOUT_FILENO) < 0)
				err(1, "dup2");
		if (!loudsubs) {
			nfd = open(_PATH_DEVNULL, O_RDWR);
			if (nfd == -1)
				err(1, "open: %s", _PATH_DEVNULL);
			if (dup2(nfd, STDIN_FILENO) < 0)
				err(1, "dup2");
			if (dup2dn)
				if (dup2(nfd, STDOUT_FILENO) < 0)
				   err(1, "dup2");
			if (dup2(nfd, STDERR_FILENO) < 0)
				err(1, "dup2");
		}

		(void)execv(argv[0], argv);
		warn("exec: %s", argv[0]);
		_exit(-1);
	case -1:
		err(1, "fork");
	}

	free(cmd);
	free(argv);
	while (waitpid(pid, &status, 0) != pid)
		;
	return (WEXITSTATUS(status));
}

static void
usage(void)
{
	const char *name;

	if (compat)
		name = getprogname();
	else
		name = "mdmfs";
	if (!compat)
		fprintf(stderr,
"usage: %s [-DLlMNSUX] [-a maxcontig [-b block-size] [-c cylinders]\n"
"\t[-d rotdelay] [-e maxbpg] [-F file] [-f frag-size] [-i bytes]\n"
"\t[-m percent-free] [-n rotational-positions] [-O optimization]\n"
"\t[-o mount-options] [-p permissions] [-s size] [-w user:group]\n"
"\tmd-device mount-point\n", name);
	fprintf(stderr,
"usage: %s -C [-lNU] [-a maxcontig] [-b block-size] [-c cylinders]\n"
"\t[-d rotdelay] [-e maxbpg] [-F file] [-f frag-size] [-i bytes]\n"
"\t[-m percent-free] [-n rotational-positions] [-O optimization]\n"
"\t[-o mount-options] [-s size] md-device mount-point\n", name);
	exit(1);
}
