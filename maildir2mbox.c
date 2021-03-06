/*
 *  Revision 20180115, Kai Peter
 *  - prevented 'misleading-indentation' warning (gcc-6)
 *  - replaced 'ssin/ssout' by 'bin/bout' (substdio --> buffer)
 *  Revision 20160712, Kai Peter
 *  - switched to 'buffer'
 *  Revision 20160503, Kai Peter
 *  - added 'rename.h'
 *  - changed return type of main to int
 */
#include <unistd.h>
#include <sys/stat.h>
#include "prioq.h"
#include "env.h"
#include "stralloc.h"
#include "buffer.h"
#include "getln.h"
#include "error.h"
#include "open.h"
#include "lock.h"
#include "gfrom.h"
#include "str.h"
#include "myctime.h"
#include "maildir.h"
#include "rename.h"

char *mbox;
char *mboxtmp;

stralloc filenames = {0};
prioq pq = {0};
prioq pq2 = {0};

stralloc line = {0};

stralloc ufline = {0};

char inbuf[BUFFER_INSIZE];
char outbuf[BUFFER_OUTSIZE];

#define FATL "maildir2mbox: fatal: "
#define WARNING "maildir2mbox: warning: "

void die_nomem() { strerr_die2x(111,FATL,"out of memory"); }

int main()
{
  buffer bin;
  buffer bout;
  struct prioq_elt pe;
  int fdoldmbox;
  int fdnewmbox;
  int fd;
  int match;
  int fdlock;

  umask(077);

  mbox = env_get("MAIL");
  if (!mbox) strerr_die2x(111,FATL,"MAIL not set");
  mboxtmp = env_get("MAILTMP");
  if (!mboxtmp) strerr_die2x(111,FATL,"MAILTMP not set");

  if (maildir_chdir() == -1)
    strerr_die1(111,FATAL,&maildir_chdir_err);
  maildir_clean(&filenames);
  if (maildir_scan(&pq,&filenames,1,1) == -1)
    strerr_die1(111,FATAL,&maildir_scan_err);

  if (!prioq_min(&pq,&pe)) _exit(0); /* nothing new */

  fdlock = open_append(mbox);
  if (fdlock == -1)
    strerr_die4sys(111,FATL,"unable to lock ",mbox,": ");
  if (lock_ex(fdlock) == -1)
    strerr_die4sys(111,FATL,"unable to lock ",mbox,": ");

  fdoldmbox = open_read(mbox);
  if (fdoldmbox == -1)
    strerr_die4sys(111,FATL,"unable to read ",mbox,": ");

  fdnewmbox = open_trunc(mboxtmp);
  if (fdnewmbox == -1)
    strerr_die4sys(111,FATL,"unable to create ",mboxtmp,": ");

  buffer_init(&bin,read,fdoldmbox,inbuf,sizeof(inbuf));
  buffer_init(&bout,write,fdnewmbox,outbuf,sizeof(outbuf));

  switch(buffer_copy(&bout,&bin))
  {
    case -2: strerr_die4sys(111,FATL,"unable to read ",mbox,": ");
    case -3: strerr_die4sys(111,FATL,"unable to write to ",mboxtmp,": ");
  }

  while (prioq_min(&pq,&pe))
  {
    prioq_delmin(&pq);
    if (!prioq_insert(&pq2,&pe)) die_nomem();

    fd = open_read(filenames.s + pe.id);
    if (fd == -1)
      strerr_die4sys(111,FATL,"unable to read $MAILDIR/",filenames.s + pe.id,": ");
    buffer_init(&bin,read,fd,inbuf,sizeof(inbuf));

    if (getln(&bin,&line,&match,'\n') != 0)
      strerr_die4sys(111,FATL,"unable to read $MAILDIR/",filenames.s + pe.id,": ");

    if (!stralloc_copys(&ufline,"From XXX ")) die_nomem();
    if (match)
      if (stralloc_starts(&line,"Return-Path: <"))
      {
        if (line.s[14] == '>')
    {
          if (!stralloc_copys(&ufline,"From MAILER-DAEMON ")) die_nomem();
    }
        else
        {
      int i;
         if (!stralloc_ready(&ufline,line.len)) die_nomem();
         if (!stralloc_copys(&ufline,"From ")) die_nomem();
      for (i = 14;i < line.len - 2;++i)
        if ((line.s[i] == ' ') || (line.s[i] == '\t'))
          ufline.s[ufline.len++] = '-';
        else {
          ufline.s[ufline.len++] = line.s[i];
          if (!stralloc_cats(&ufline," ")) die_nomem();
          }
        }
      }
    if (!stralloc_cats(&ufline,myctime(pe.dt))) die_nomem();
    if (buffer_put(&bout,ufline.s,ufline.len) == -1)
      strerr_die4sys(111,FATAL,"unable to write to ",mboxtmp,": ");

    while (match && line.len)
    {
      if (gfrom(line.s,line.len))
        if (buffer_puts(&bout,">") == -1)
          strerr_die4sys(111,FATL,"unable to write to ",mboxtmp,": ");
      if (buffer_put(&bout,line.s,line.len) == -1)
        strerr_die4sys(111,FATL,"unable to write to ",mboxtmp,": ");
      if (!match)
      {
        if (buffer_puts(&bout,"\n") == -1)
          strerr_die4sys(111,FATL,"unable to write to ",mboxtmp,": ");
        break;
      }
      if (getln(&bin,&line,&match,'\n') != 0)
        strerr_die4sys(111,FATL,"unable to read $MAILDIR/",filenames.s + pe.id,": ");
    }
    if (buffer_puts(&bout,"\n"))
      strerr_die4sys(111,FATL,"unable to write to ",mboxtmp,": ");

   close(fd);
  }

  if (buffer_flush(&bout) == -1)
    strerr_die4sys(111,FATL,"unable to write to ",mboxtmp,": ");
  if (fsync(fdnewmbox) == -1)
    strerr_die4sys(111,FATL,"unable to write to ",mboxtmp,": ");
  if (close(fdnewmbox) == -1) /* NFS dorks */
    strerr_die4sys(111,FATL,"unable to write to ",mboxtmp,": ");
  if (rename(mboxtmp,mbox) == -1)
    strerr_die6(111,FATL,"unable to move ",mboxtmp," to ",mbox,": ",&strerr_sys);

  while (prioq_min(&pq2,&pe))
  {
    prioq_delmin(&pq2);
    if (unlink(filenames.s + pe.id) == -1)
      strerr_warn4(WARNING,"$MAILDIR/",filenames.s + pe.id," will be delivered twice; unable to unlink: ",&strerr_sys);
  }

  return(0);
}
