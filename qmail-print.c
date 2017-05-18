/*
 *  Revision 20170518, Kai Peter
 *  - new file
 */
#include <unistd.h>
#include "buffer.h"
#include "fmt.h"
#include "str.h"
#include "stralloc.h"
#include "error.h"
#include "buildins.h"

#define WHO "qmail-print: "

int main()
{
  char strnum[FMT_ULONG];

  buffer_puts(buffer_1,"\033[1mCompiled-in values:\033[0m\n\n");

  buffer_puts(buffer_1,"    qmail home directory: ");
  buffer_puts(buffer_1,qmailhome);
  buffer_puts(buffer_1,"\n");

  buffer_puts(buffer_1,"user extension delimiter: '");
  buffer_puts(buffer_1,break_delim);
  buffer_puts(buffer_1,"'\n");

  buffer_puts(buffer_1,"   paternalism (decimal): ");
  buffer_put(buffer_1,strnum,fmt_ulong(strnum,(unsigned long) paternalism));
  buffer_puts(buffer_1,"\n");

  buffer_puts(buffer_1,"silent concurrency limit: ");
  buffer_put(buffer_1,strnum,fmt_ulong(strnum,(unsigned long) spawn_limit));
  buffer_puts(buffer_1,"\n");

  if(str_equal(queuedir,"")) {
    stralloc sa = {0};
    if(!stralloc_copys(&sa,qmailhome)) err_sys(errno);
    if(!stralloc_catb(&sa,"/queue",10)) err_sys(errno);
    if(!stralloc_0(&sa)) err_sys(errno);
    queuedir = sa.s;
  }
  buffer_puts(buffer_1,"  default queue location: ");
  buffer_puts(buffer_1,queuedir);
  buffer_puts(buffer_1,"\n");

  buffer_puts(buffer_1,"queue subdirectory split: ");
  buffer_put(buffer_1,strnum,fmt_ulong(strnum,(unsigned long) queue_split));
  buffer_puts(buffer_1,"\n\n");

  buffer_puts(buffer_1,"user and group names: \n");
  buffer_puts(buffer_1,"      alias user: ");
  buffer_puts(buffer_1,username_alias);
  buffer_puts(buffer_1,"\n");
  buffer_puts(buffer_1,"     daemon user: ");
  buffer_puts(buffer_1,username_daemon);
  buffer_puts(buffer_1,"\n");
  buffer_puts(buffer_1,"        log user: ");
  buffer_puts(buffer_1,username_log);
  buffer_puts(buffer_1,"\n");
  buffer_puts(buffer_1,"       misc user: ");
  buffer_puts(buffer_1,username_misc);
  buffer_puts(buffer_1,"\n");
  buffer_puts(buffer_1,"     passwd user: ");
  buffer_puts(buffer_1,username_passwd);
  buffer_puts(buffer_1,"\n");
  buffer_puts(buffer_1,"      queue user: ");
  buffer_puts(buffer_1,username_queue);
  buffer_puts(buffer_1,"\n");
  buffer_puts(buffer_1,"     remote user: ");
  buffer_puts(buffer_1,username_remote);
  buffer_puts(buffer_1,"\n");
  buffer_puts(buffer_1,"       send user: ");
  buffer_puts(buffer_1,username_send);
  buffer_puts(buffer_1,"\n\n");
  buffer_puts(buffer_1,"  standard group: ");
  buffer_puts(buffer_1,grpname_qmail);
  buffer_puts(buffer_1,"\n");
  buffer_puts(buffer_1,"   process group: ");
  buffer_puts(buffer_1,grpname_nofiles);
  buffer_puts(buffer_1,"\n\n");

  char *tls = "No";
  char *ipv = "No";
#ifdef TLS
  tls = "Yes";
#else
#endif
#ifdef INET6
  ipv = "Yes";
#endif
  buffer_puts(buffer_1,"supports TLS/SSL: ");
  buffer_puts(buffer_1,tls);
  buffer_puts(buffer_1,"\n");
  buffer_puts(buffer_1,"support for IPv6: ");
  buffer_puts(buffer_1,ipv);
  buffer_puts(buffer_1,"\n\n");

  buffer_flush(buffer_1);
  _exit(0);
}
