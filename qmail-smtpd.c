/*
 *  Revision 20180418, Kai Peter
 *  - renamed old timeout{read,write} functions (conflicts w/ qlibs)
 *  Revision 20171130, Kai Peter
 *  - changed folder name 'control' to 'etc'
 *  Revision 20160708, Kai Peter
 *  - changed return type of 'saferead'/'safewrite' to 'ssize_t'
 *  - replaced readwrite.h and other (temp.) header files by unistd.h
 *  - switched to 'buffer'
 *  Revision 20160705, Kai Peter
 *  - switched to 'buffer' for 'smtpcommands' (see 'commands.c' too)
 *  Revision 20160509, Kai Peter
 *  - added string (argument) to call 'err_unimpl()'
 *  - removed arguments from call 'err_authfail()'
 *  Revision 20160509, Kai Peter
 *  - added 'close.h', base64,h, 'fd.h', 'getpid.h', 'chdir.h', 'sleep.h'
 *  - added declaration of 'spp_rcpt_accepted()'
 *  - changed callings 'die_nomem;' to 'die_nomem();'
 *  Revision 20160504, Kai Peter
 *  - changed return type of main to in
 *  - added parentheses to multiple conditions
 *  - casting some pointers (char *)
 *  - added includes 'exec.h', 'pipe.h'
 */

#include <unistd.h>
#include "sig.h"
#include "buffer.h"
#include "alloc.h"
#include "auto_qmail.h"
#include "control.h"
#include "received.h"
#include "constmap.h"
#include "errmsg.h"
#include "ipme.h"
#include "ip.h"
#include "qmail.h"
#include "str.h"
#include "fmt.h"
#include "scan.h"
#include "byte.h"
#include "case.h"
#include "env.h"
#include "now.h"
#include "rcpthosts.h"
#include "timeoutread.h"
#include "timeoutwrite.h"
#include "commands.h"
#include "wait.h"
#include "qmail-spp.h"
#include "base64.h"
#include "fd.h"

#define WHO "qmail-smtpd"

int spp_val;
extern void spp_rcpt_accepted();

#ifndef CRAM_MD5
#define CRAM_MD5 1
#endif
#define AUTHSLEEP 5
#define SUBMISSION "587"

#define MAXHOPS 100
unsigned int databytes = 0;
int timeout = 1200;

#ifdef TLS
#include <sys/stat.h>
#include "tls.h"
#include "ssl_timeoutio.h"

void tls_init();
int tls_verify();
void tls_nogateway();
int ssl_rfd = -1, ssl_wfd = -1; /* SSL_get_Xfd() are broken */
#endif

ssize_t safewrite(int fd,char *buf,int len)
{
  int r;
#ifdef TLS
  if (ssl && fd == ssl_wfd)
    r = ssl_timeoutwrite(timeout, ssl_rfd, ssl_wfd, ssl, buf, len);
  else
#endif
  r = timeoutwrite_old(timeout,fd,buf,len);
  if (r <= 0) _exit(1);
  return r;
}

char boutbuf[512];
buffer bout = BUFFER_INIT(safewrite,1,boutbuf,sizeof boutbuf);
char berrbuf[512];
buffer berr = BUFFER_INIT(write,2,berrbuf,sizeof berrbuf);

char **childargs;   /* the childargs are a checkpassword tool! */
char bauthbuf[512];
buffer bauth = BUFFER_INIT(safewrite,3,bauthbuf,sizeof(bauthbuf));

void logit(const char* message);
void logit2(const char* message, const char* reason);
void flush() { buffer_flush(&bout); }
void out(char *s) { buffer_puts(&bout,s); }

void die_read() { logit("read failed"); _exit(1); }
void die_alarm() { logit("timeout"); out("451 timeout (#4.4.2)\r\n"); flush(); _exit(1); }
void die_nomem() { out("421 out of memory (#4.3.0)\r\n"); flush(); _exit(1); }
void die_control() { logit("unable to read controls"); out("421 unable to read controls (#4.3.0)\r\n"); flush(); _exit(1); }
void die_ipme() { logit("unable to figure out my IP addresses"); out("421 unable to figure out my IP addresses (#4.3.0)\r\n"); flush(); _exit(1); }
void straynewline() { logit("bad newlines"); out("451 See http://pobox.com/~djb/docs/smtplf.html.\r\n"); flush(); _exit(1); }

void err_size() { out("552 sorry, that message size exceeds my databytes limit (#5.3.4)\r\n"); }
void err_bmf() { out("553 sorry, your envelope sender is in my badmailfrom list (#5.7.1)\r\n"); }
#ifndef TLS
void err_nogateway() { out("553 sorry, that domain isn't in my list of allowed rcpthosts (#5.7.1)\r\n"); }
#else
void err_nogateway()
{
  out("553 sorry, that domain isn't in my list of allowed rcpthosts");
  tls_nogateway();
  out(" (#5.7.1)\r\n");
}
#endif
void err_unimpl(arg) char *arg; { out("502 unimplemented (#5.5.1)\r\n"); }
void err_unrecog() { out("500 unrecognised (#5.5.2)\r\n"); }
void err_syntax() { out("555 syntax error (#5.5.4)\r\n"); }
void err_wantmail() { out("503 MAIL first (#5.5.1)\r\n"); }
void err_wantrcpt() { out("503 RCPT first (#5.5.1)\r\n"); }
void err_noop(arg) char *arg; { out("250 ok\r\n"); }
void err_vrfy(arg) char *arg; { out("252 send some mail, i'll try my best\r\n"); }
void err_qqt() { out("451 qqt failure (#4.3.0)\r\n"); }

int err_child() { out("454 oops, problem with child and I can't auth (#4.3.0)\r\n"); return -1; }
int err_fork() { out("454 oops, child won't start and I can't auth (#4.3.0)\r\n"); return -1; }
int err_pipe() { out("454 oops, unable to open pipe and I can't auth (#4.3.0)\r\n"); return -1; }
int err_write() { out("454 oops, unable to write pipe and I can't auth (#4.3.0)\r\n"); return -1; }
void err_authd() { out("503 you're already authenticated (#5.5.0)\r\n"); }
void err_authmail() { out("503 no auth during mail transaction (#5.5.0)\r\n"); }
int err_noauth() { out("504 auth type unimplemented (#5.5.1)\r\n"); return -1; }
int err_authabrt() { out("501 auth exchange canceled (#5.0.0)\r\n"); return -1; }
int err_input() { out("501 malformed auth input (#5.5.4)\r\n"); return -1; }
void err_authfail() { out("535 authentication failed (#5.7.1)\r\n"); }
void err_submission() { out("530 Authorization required (#5.7.1) \r\n"); }

ssize_t saferead(int fd,char *buf,int len)
{
  int r;
  flush();
#ifdef TLS
  if (ssl && fd == ssl_rfd)
    r = ssl_timeoutread(timeout, ssl_rfd, ssl_wfd, ssl, buf, len);
  else
#endif
  r = timeoutread_old(timeout,fd,buf,len);
  if (r == -1) if (errno == error_timeout) die_alarm();
  if (r <= 0) die_read();
  return r;
}

char inbuf[1024];
buffer bin = BUFFER_INIT(saferead,0,inbuf,sizeof inbuf);
//----

stralloc greeting = {0};

void smtp_greet(char *code) {
  buffer_puts(&bout,code);
  buffer_put(&bout,greeting.s,greeting.len);
}
void smtp_help(char *arg) {
  out("214 eQmail home page: http://openqmail.org\r\n"); }
void smtp_quit(char *arg) {
  smtp_greet("221 "); out("\r\n"); flush(); _exit(0); }

char *protocol;
char *remoteip = "";
char *remotehost;
char *remoteinfo;
char *local;
char *localport;
char *submission;
char *relayclient;

stralloc helohost = {0};
char *fakehelo; /* pointer into helohost, or 0 */

void dohelo(arg) char *arg; {
  if (!stralloc_copys(&helohost,arg)) die_nomem(); 
  if (!stralloc_0(&helohost)) die_nomem(); 
  fakehelo = case_diffs(remotehost,helohost.s) ? helohost.s : 0;
}

int liphostok = 0;      /* local ip host */
stralloc liphost = {0};
int bmfok = 0;
stralloc bmf = {0};
struct constmap mapbmf;

void setup()
{
  char *x;
  unsigned long u;

  if (control_init() == -1) die_control();
  if (control_rldef(&greeting,"etc/smtpgreeting",1,(char *) 0) != 1)
    die_control();
  liphostok = control_rldef(&liphost,"etc/localiphost",1,(char *) 0);
  if (liphostok == -1) die_control();
  if (control_readint(&timeout,"etc/timeoutsmtpd") == -1) die_control();
  if (timeout <= 0) timeout = 1;
  if (rcpthosts_init() == -1) die_control();
  if (spp_init() == -1) die_control();

  bmfok = control_readfile(&bmf,"etc/badmailfrom",0);
  if (bmfok == -1) die_control();
  if (bmfok)
    if (!constmap_init(&mapbmf,bmf.s,bmf.len,0)) die_nomem();

  if (control_readint((int *)&databytes,"etc/databytes") == -1) die_control();
  x = env_get("DATABYTES");
  if (x) { scan_ulong(x,&u); databytes = u; }
  if (!(databytes + 1)) --databytes;

  protocol = "SMTP";
  remoteip = env_get("TCPREMOTEIP");
  if (!remoteip) remoteip = "unknown";
  localport = env_get("TCPLOCALPORT");
  if (!localport) localport = "0";
  local = env_get("TCPLOCALHOST");
  if (!local) local = env_get("TCPLOCALIP");
  if (!local) local = "unknown";
  remotehost = env_get("TCPREMOTEHOST");
  if (!remotehost) remotehost = "unknown";
  remoteinfo = env_get("TCPREMOTEINFO");
  relayclient = env_get("RELAYCLIENT");
  submission = env_get("SUBMISSIONPORT");
  if (!submission) submission = SUBMISSION;

#ifdef TLS
  if (env_get("SMTPS")) { smtps = 1; tls_init(); }
  else
#endif
  dohelo(remotehost);
}

stralloc addr = {0}; /* will be 0-terminated, if addrparse returns 1 */

int addrparse(arg)
char *arg;
{
  int i;
  char ch;
  char terminator;
  struct ip_address ip;
  int flagesc;
  int flagquoted;
 
  terminator = '>';
  i = str_chr(arg,'<');
  if (arg[i])
    arg += i + 1;
  else { /* partner should go read rfc 821 */
    terminator = ' ';
    arg += str_chr(arg,':');
    if (*arg == ':') ++arg;
    while (*arg == ' ') ++arg;
  }

  /* strip source route */
  if (*arg == '@') while (*arg) if (*arg++ == ':') break;

  if (!stralloc_copys(&addr,"")) die_nomem();
  flagesc = 0;
  flagquoted = 0;
  for (i = 0;(ch = arg[i]);++i) { /* copy arg to addr, stripping quotes */
    if (flagesc) {
      if (!stralloc_append(&addr,&ch)) die_nomem();
      flagesc = 0;
    }
    else {
      if (!flagquoted && (ch == terminator)) break;
      switch(ch) {
        case '\\': flagesc = 1; break;
        case '"': flagquoted = !flagquoted; break;
        default: if (!stralloc_append(&addr,&ch)) die_nomem();
      }
    }
  }
  /* could check for termination failure here, but why bother? */
  if (!stralloc_append(&addr,"")) die_nomem();

  /* if address is in "etc/localiphost" */
  if (liphostok) {
    i = byte_rchr(addr.s,addr.len,'@');
    if (i < addr.len) /* if not, partner should go read rfc 821 */
      if (addr.s[i + 1] == '[')
//        if (!addr.s[i + 1 + ip_scanbracket(addr.s + i + 1,&ip)])
        if (!addr.s[i + 1 + ip46_scanbracket(addr.s + i + 1,(char *)&ip)])  // new function in ip.c
/* Kai: todo: check for IPv6 too (works with IPv4 now only!) */
/* --> but: a ip6_scanbracket function is overhead */
          if ( (ipme_is(&ip)) || (ipme_is6(&ip)) ) {
            addr.len = i + 1;
            if (!stralloc_cat(&addr,&liphost)) die_nomem();
            if (!stralloc_0(&addr)) die_nomem();
          }
  }

  if (addr.len > 900) return 0;
  return 1;
}

int bmfcheck()
{
  int j;
  if (!bmfok) return 0;
  if (constmap(&mapbmf,addr.s,addr.len - 1)) return 1;
  j = byte_rchr(addr.s,addr.len,'@');
  if (j < addr.len) {
    if (constmap(&mapbmf,addr.s + j,addr.len - j - 1)) return 1;
    for (j++; j < addr.len; j++)
      if (addr.s[j] == '.') {
	if(constmap(&mapbmf,addr.s + j,addr.len - j - 1)) return 1;
      }
  }
  return 0;
}

int addrallowed()
{
  int r;
  r = rcpthosts(addr.s,str_len(addr.s));
  if (r == -1) die_control();
#ifdef TLS
  if (r == 0) if (tls_verify()) r = -2;
#endif
  return r;
}

int flagauth = 0;
int seenmail = 0;
int flagbarf; /* defined if seenmail */
int allowed;
int flagsize;
stralloc mailfrom = {0};
stralloc rcptto = {0};
stralloc fuser = {0};
stralloc mfparms = {0};
stralloc log_buf = {0};

void logit(message) const char* message;
{
  if (!stralloc_copys(&log_buf, "qmail-smtpd: ")) die_nomem();
  if (!stralloc_cats(&log_buf, message)) die_nomem();
  if (!stralloc_catb(&log_buf, ": ", 2)) die_nomem();
  if (mailfrom.s) {
      if (!stralloc_catb(&log_buf, mailfrom.s, mailfrom.len-1)) die_nomem();
  } else
      if (!stralloc_catb(&log_buf, "(null)", 6)) die_nomem();
  if (!stralloc_catb(&log_buf, " from ", 6)) die_nomem();
  if (!stralloc_cats(&log_buf, remoteip)) die_nomem();
  if (!stralloc_catb(&log_buf, " to ", 4)) die_nomem();
  if (addr.s) {
      if (!stralloc_catb(&log_buf, addr.s, addr.len-1)) die_nomem();
  } else
      if (!stralloc_catb(&log_buf, "(null)", 6)) die_nomem();
  if (!stralloc_catb(&log_buf, " helo ", 6)) die_nomem();
  if (helohost.s) {
      if (!stralloc_catb(&log_buf, helohost.s, helohost.len-1)) die_nomem();
  } else
      if (!stralloc_catb(&log_buf, "(null)", 6)) die_nomem();
  if (!stralloc_catb(&log_buf, "\n", 1)) die_nomem();
//  substdio_putflush(&sserr, log_buf);
    buffer_putflush(&berr,log_buf.s,log_buf.len);
}

void logit2(message, reason)
const char* message;
const char* reason;
{
  if (!stralloc_copys(&log_buf,"qmail-smtpd: ")) die_nomem();
  if (!stralloc_cats(&log_buf, message)) die_nomem();
  if (!stralloc_cats(&log_buf, " (")) die_nomem();
  if (!stralloc_cats(&log_buf, reason)) die_nomem();
  if (!stralloc_cats(&log_buf, "): ")) die_nomem();
  if (mailfrom.s) {
     if (!stralloc_catb(&log_buf, mailfrom.s, mailfrom.len-1)) die_nomem();
  } else
     if (!stralloc_catb(&log_buf, "(null)", 6)) die_nomem();
  if (!stralloc_cats(&log_buf," from ")) die_nomem();
  if (!stralloc_cats(&log_buf, remoteip)) die_nomem();
  if (!stralloc_cats(&log_buf, " to ")) die_nomem();
  if (addr.s) {
     if (!stralloc_catb(&log_buf, addr.s, addr.len-1)) die_nomem();
  } else
     if (!stralloc_catb(&log_buf, "(null)", 6)) die_nomem();
  if (!stralloc_cats(&log_buf, " helo ")) die_nomem();
  if (helohost.s) {
     if (!stralloc_catb(&log_buf, helohost.s, helohost.len-1)) die_nomem();
  } else
     if (!stralloc_catb(&log_buf, "(null)", 6)) die_nomem();
  if (!stralloc_catb(&log_buf, "\n", 1)) die_nomem();
//  substdio_putflush(&sserr, log_buf);
    buffer_putflush(&berr,log_buf.s,log_buf.len);
}


int mailfrom_size(char *arg) {
  unsigned long r;
  unsigned long sizebytes = 0;

  scan_ulong(arg,&r);
  sizebytes = r;
  if (databytes) if (sizebytes > databytes) return 1;
  return 0;
}

void mailfrom_auth(char *arg,int len) {
  if (!stralloc_copys(&fuser,"")) die_nomem();
  if (case_starts(arg,"<>")) { if (!stralloc_cats(&fuser,"unknown")) die_nomem(); }
  else 
    while (len) {
      if (*arg == '+') {
        if (case_starts(arg,"+3D")) { arg=arg+2; len=len-2; if (!stralloc_cats(&fuser,"=")) die_nomem(); }
        if (case_starts(arg,"+2B")) { arg=arg+2; len=len-2; if (!stralloc_cats(&fuser,"+")) die_nomem(); }
      }
      else
        if (!stralloc_catb(&fuser,arg,1)) die_nomem();
      arg++; len--;
    }
  if(!stralloc_0(&fuser)) die_nomem();
  if (!remoteinfo) {
    remoteinfo = fuser.s;
    if (!env_unset("TCPREMOTEINFO")) die_read();
    if (!env_put("TCPREMOTEINFO",remoteinfo)) die_nomem();
  }
}

void mailfrom_parms(arg) char *arg;
{
  int i;
  int len;

    len = str_len(arg);
    if (!stralloc_copys(&mfparms,"")) die_nomem();
    i = byte_chr(arg,len,'>');
    if (i > 4 && i < len) {
      while (len) {
        arg++; len--; 
        if (*arg == ' ' || *arg == '\0' ) {
           if (case_starts(mfparms.s,"SIZE=")) if (mailfrom_size(mfparms.s+5)) { flagsize = 1; return; }
           if (case_starts(mfparms.s,"AUTH=")) mailfrom_auth(mfparms.s+5,mfparms.len-5);  
           if (!stralloc_copys(&mfparms,"")) die_nomem();
        }
        else
          if (!stralloc_catb(&mfparms,arg,1)) die_nomem();
      }
    }
}

void smtp_helo(arg) char *arg;
{
  if(!spp_helo(arg)) return;
  smtp_greet("250 "); out("\r\n");
  seenmail = 0; dohelo(arg);
}
/* ESMTP extensions are published here */
void smtp_ehlo(arg) char *arg;
{
#ifdef TLS
  struct stat st;
#endif
  char size[FMT_ULONG];
  if(!spp_helo(arg)) return;
  smtp_greet("250-"); 
#ifdef TLS
  if (!ssl && (stat("etc/servercert.pem",&st) == 0))
    out("\r\n250-STARTTLS");
#endif
  size[fmt_ulong(size,(unsigned int) databytes)] = 0;
  out("\r\n250-PIPELINING\r\n250-8BITMIME\r\n");
  out("250-SIZE "); out(size); out("\r\n");
  /* offer auth only if a checkpasswordtool is given */
  if (*childargs) {
#ifdef CRAM_MD5
    out("250 AUTH LOGIN PLAIN CRAM-MD5\r\n");
#else
    out("250 AUTH LOGIN PLAIN\r\n");
#endif
  }
  seenmail = 0; dohelo(arg);
}
void smtp_rset(arg) char *arg;
{
  spp_rset();
  seenmail = 0;
  out("250 flushed\r\n");
}
void smtp_mail(arg) char *arg;
{
  if (str_equal(localport,submission)) 
    if (!flagauth) { err_submission(); return; }
  if (!addrparse(arg)) { err_syntax(); return; }
  flagsize = 0;
  mailfrom_parms(arg);
  if (flagsize) { err_size(); return; }
  if (!(spp_val = spp_mail())) return;
  if (spp_val == 1)
  flagbarf = bmfcheck();
  seenmail = 1;
  if (!stralloc_copys(&rcptto,"")) die_nomem();
  if (!stralloc_copys(&mailfrom,addr.s)) die_nomem();
  if (!stralloc_0(&mailfrom)) die_nomem();
  out("250 ok\r\n");
}
void smtp_rcpt(arg) char *arg; {
  if (!seenmail) { err_wantmail(); return; }
  if (!addrparse(arg)) { err_syntax(); return; }
  if (flagbarf) { err_bmf(); return; }
  if (!relayclient) allowed = addrallowed();
  else allowed = 1;
  if (flagbarf) { logit("badmailfrom"); err_bmf(); return; }
  if (relayclient) {
    --addr.len;
    if (!stralloc_cats(&addr,relayclient)) die_nomem();
    if (!stralloc_0(&addr)) die_nomem();
  }
  else if (spp_val == 1) {
    if (!allowed) { err_nogateway(); return; }
  }
  spp_rcpt_accepted();
  if (!stralloc_cats(&rcptto,"T")) die_nomem();
  if (!stralloc_cats(&rcptto,addr.s)) die_nomem();
  if (!stralloc_0(&rcptto)) die_nomem();
  out("250 ok\r\n");
}

#ifdef TLS
void flush_io() { bin.p = 0; flush(); }
#endif

struct qmail qqt;
unsigned int bytestooverflow = 0;

void put(ch)
char *ch;
{
  if (bytestooverflow)
    if (!--bytestooverflow)
      qmail_fail(&qqt);
  qmail_put(&qqt,ch,1);
}

void blast(hops)
int *hops;
{
  char ch;
  int state;
  int flaginheader;
  int pos; /* number of bytes since most recent \n, if fih */
  int flagmaybex; /* 1 if this line might match RECEIVED, if fih */
  int flagmaybey; /* 1 if this line might match \r\n, if fih */
  int flagmaybez; /* 1 if this line might match DELIVERED, if fih */

  state = 1;
  *hops = 0;
  flaginheader = 1;
  pos = 0; flagmaybex = flagmaybey = flagmaybez = 1;
  for (;;) {
    buffer_get(&bin,&ch,1);
    if (flaginheader) {
      if (pos < 9) {
        if (ch != "delivered"[pos]) if (ch != "DELIVERED"[pos]) flagmaybez = 0;
        if (flagmaybez) if (pos == 8) ++*hops;
        if (pos < 8)
          if (ch != "received"[pos]) if (ch != "RECEIVED"[pos]) flagmaybex = 0;
        if (flagmaybex) if (pos == 7) ++*hops;
        if (pos < 2) if (ch != "\r\n"[pos]) flagmaybey = 0;
        if (flagmaybey) if (pos == 1) flaginheader = 0;
	++pos;
      }
      if (ch == '\n') { pos = 0; flagmaybex = flagmaybey = flagmaybez = 1; }
    }
    switch(state) {
      case 0:
        if (ch == '\n') straynewline();
        if (ch == '\r') { state = 4; continue; }
        break;
      case 1: /* \r\n */
        if (ch == '\n') straynewline();
        if (ch == '.') { state = 2; continue; }
        if (ch == '\r') { state = 4; continue; }
        state = 0;
        break;
      case 2: /* \r\n + . */
        if (ch == '\n') straynewline();
        if (ch == '\r') { state = 3; continue; }
        state = 0;
        break;
      case 3: /* \r\n + .\r */
        if (ch == '\n') return;
        put(".");
        put("\r");
        if (ch == '\r') { state = 4; continue; }
        state = 0;
        break;
      case 4: /* + \r */
        if (ch == '\n') { state = 1; break; }
        if (ch != '\r') { put("\r"); state = 0; }
    }
    put(&ch);
  }
}

char accept_buf[FMT_ULONG];
void acceptmessage(qp) unsigned long qp;
{
  datetime_sec when;
  when = now();
  out("250 ok ");
  accept_buf[fmt_ulong(accept_buf,(unsigned long) when)] = 0;
  out(accept_buf);
  out(" qp ");
  accept_buf[fmt_ulong(accept_buf,qp)] = 0;
  out(accept_buf);
  out("\r\n");
}

void smtp_data(char *arg) {
  int hops;
  unsigned long qp;
  char *qqx;

  if (!seenmail) { err_wantmail(); return; }
  if (!rcptto.len) { err_wantrcpt(); return; }
  if (!spp_data()) return;
  seenmail = 0;
  if (databytes) bytestooverflow = databytes + 1;
  if (qmail_open(&qqt) == -1) { err_qqt(); return; }
  qp = qmail_qp(&qqt);
  out("354 go ahead\r\n");

  received(&qqt,protocol,local,remoteip,remotehost,remoteinfo,fakehelo);
  qmail_put(&qqt,sppheaders.s,sppheaders.len); /* set in qmail-spp.c */
  spp_rset();
  blast(&hops);
  hops = (hops >= MAXHOPS);
  if (hops) qmail_fail(&qqt);
  qmail_from(&qqt,mailfrom.s);
  qmail_put(&qqt,rcptto.s,rcptto.len);

  qqx = qmail_close(&qqt);

  if (!*qqx) { acceptmessage(qp); logit("message accepted"); return; }
  if (hops) {
     out("554 too many hops, this message is looping (#5.4.6)\r\n");
     logit("message looping");
     return;
   }
   if (databytes) if (!bytestooverflow) {
     out("552 sorry, that message size exceeds my databytes limit (#5.3.4)\r\n");
     logit("message too big");
     return;
   }
   if (*qqx == 'D') {
     out("554 ");
     logit2("message rejected", qqx + 1);
   } else {
     out("451 ");
     logit2("message delayed", qqx + 1);
   }

  out(qqx + 1);
  out("\r\n");
}

char unique[FMT_ULONG + FMT_ULONG + 3];
static stralloc authin = {0};   /* input from SMTP client */
static stralloc user = {0};     /* authorization user-id */
static stralloc pass = {0};     /* plain passwd or digest */
static stralloc resp = {0};     /* b64 response */
#ifdef CRAM_MD5
static stralloc chal = {0};     /* plain challenge */
static stralloc slop = {0};     /* b64 challenge */
#endif

int authgetl(void) {
  int i;

  if (!stralloc_copys(&authin,"")) die_nomem();
  for (;;) {
    if (!stralloc_readyplus(&authin,1)) die_nomem(); /* XXX */
//    i = substdio_get(&ssin,authin.s + authin.len,1);
    i = buffer_get(&bin,authin.s + authin.len,1);
    if (i != 1) die_read();
    if (authin.s[authin.len] == '\n') break;
    ++authin.len;
  }

  if (authin.len > 0) if (authin.s[authin.len - 1] == '\r') --authin.len;
  authin.s[authin.len] = 0;
  if (*authin.s == '*' && *(authin.s + 1) == 0) { return err_authabrt(); }
  if (authin.len == 0) { return err_input(); }
  return authin.len;
}

int authenticate(void)
{
  int child;
  int wstat;
  int pi[2];

  if (!stralloc_0(&user)) die_nomem();
  if (!stralloc_0(&pass)) die_nomem();
#ifdef CRAM_MD5
  if (!stralloc_0(&chal)) die_nomem();
#endif

  if (pipe(pi) == -1) return err_pipe();
  switch(child = fork()) {
    case -1:
      return err_fork();
    case 0:
      close(pi[1]);
      if(fd_copy(3,pi[0]) == -1) return err_pipe();
      sig_pipedefault();
        execvp(*childargs, childargs);
      _exit(1);
  }
  close(pi[0]);

  buffer_init(&bauth,write,pi[1],bauthbuf,sizeof bauthbuf);
  if (buffer_put(&bauth,user.s,user.len) == -1) return err_write();
  if (buffer_put(&bauth,pass.s,pass.len) == -1) return err_write();
#ifdef CRAM_MD5
  if (buffer_put(&bauth,chal.s,chal.len) == -1) return err_write();
#endif
  if (buffer_flush(&bauth) == -1) return err_write();

  close(pi[1]);
#ifdef CRAM_MD5
  if (!stralloc_copys(&chal,"")) die_nomem();
  if (!stralloc_copys(&slop,"")) die_nomem();
#endif
  byte_zero(bauthbuf,sizeof bauthbuf);
  if (wait_pid(&wstat,child) == -1) return err_child();
  if (wait_crashed(wstat)) return err_child();
  if (wait_exitcode(wstat)) { sleep(AUTHSLEEP); return 1; } /* no */
  return 0; /* yes */
}

int auth_login(char *arg)
{
  int r;

  if (*arg) {
    if ((r = b64decode(arg,str_len(arg),&user) == 1)) return err_input();
  }
  else {
//  out("334 VXNlcm5hbWU6\r\n"); flush();       /* Username: */
    out("334 Username:\r\n"); flush();       	/* Username: */
    if (authgetl() < 0) return -1;
    if ((r = b64decode(authin.s,authin.len,&user) == 1)) return err_input();
  }
  if (r == -1) die_nomem();
//  out("334 UGFzc3dvcmQ6\r\n"); flush();         /* Password: */
    out("334 Password:\r\n"); flush();         	/* Password: */

  if (authgetl() < 0) return -1;
  if ((r = b64decode(authin.s,authin.len,&pass)) == 1) return err_input();
  if (r == -1) die_nomem();

  if (!user.len || !pass.len) return err_input();
  return authenticate();
}

int auth_plain(char *arg)
{
  int r, id = 0;

  if (*arg) {
    if ((r = b64decode(arg,str_len(arg),&resp) == 1)) return err_input();
  }
  else {
    out("334 \r\n"); flush();
    if (authgetl() < 0) return -1;
    if ((r = b64decode(authin.s,authin.len,&resp) == 1)) return err_input();
  }
  if (r == -1 || !stralloc_0(&resp)) die_nomem();
  while (resp.s[id]) id++;                       /* "authorize-id\0userid\0passwd\0" */

  if (resp.len > id + 1)
    if (!stralloc_copys(&user,resp.s + id + 1)) die_nomem();
  if (resp.len > id + user.len + 2)
    if (!stralloc_copys(&pass,resp.s + id + user.len + 2)) die_nomem();

  if (!user.len || !pass.len) return err_input();
  return authenticate();
}

#ifdef CRAM_MD5
int auth_cram()
{
  int i, r;
  char *s;

  s = unique;                                           /* generate challenge */
  s += fmt_uint(s,getpid());
  *s++ = '.';
  s += fmt_ulong(s,(unsigned long) now());
  *s++ = '@';
  *s++ = 0;
  if (!stralloc_copys(&chal,"<")) die_nomem();
  if (!stralloc_cats(&chal,unique)) die_nomem();
  if (!stralloc_cats(&chal,local)) die_nomem();
  if (!stralloc_cats(&chal,">")) die_nomem();
  if (b64encode(&chal,&slop) < 0) die_nomem();
  if (!stralloc_0(&slop)) die_nomem();

  out("334 ");                                          /* "334 base64_challenge \r\n" */
  out(slop.s);
  out("\r\n");
  flush();

  if (authgetl() < 0) return -1;                        /* got response */
  if ((r = b64decode(authin.s,authin.len,&resp) == 1)) return err_input();
  if (r == -1 || !stralloc_0(&resp)) die_nomem();

  i = str_chr(resp.s,' ');
  s = resp.s + i;
  while (*s == ' ') ++s;
  resp.s[i] = 0;
  if (!stralloc_copys(&user,resp.s)) die_nomem();       /* userid */
  if (!stralloc_copys(&pass,s)) die_nomem();            /* digest */

  if (!user.len || !pass.len) return err_input();
  return authenticate();
}
#endif

struct authcmd {
  char *text;
  int (*fun)();
} authcmds[] = {
  { "login",auth_login }
,  { "plain",auth_plain }
#ifdef CRAM_MD5
, { "cram-md5",auth_cram }
#endif
, { 0,err_noauth }
};

void smtp_auth(char *arg)
{
  int i;
  char *cmd = arg;

/* tls required patch */
#ifdef TLS
  if (strcmp(arg,"cram-md5") != 0) {
	char *tlsrequired = env_get("TLSREQUIRED");
	if (tlsrequired && (strcmp(tlsrequired, "1")) == 0) {
  	  if (!ssl) { 
  		out("538 AUTH PLAIN/LOGIN not available without TLS (#5.3.3)\r\n");
    	flush(); /*die_read();*/ return;	/* don't die */
      }
	}
  }
#endif		/* END tls required patch */
  if (!*childargs) { out("503 auth not available (#5.3.3)\r\n"); return; }
  if (flagauth) { err_authd(); return; }
  if (seenmail) { err_authmail(); return; }

  if (!stralloc_copys(&user,"")) die_nomem();
  if (!stralloc_copys(&pass,"")) die_nomem();
  if (!stralloc_copys(&resp,"")) die_nomem();
#ifdef CRAM_MD5
  if (!stralloc_copys(&chal,"")) die_nomem();
#endif

  i = str_chr(cmd,' ');
  arg = cmd + i;
  while (*arg == ' ') ++arg;
  cmd[i] = 0;

  for (i = 0;authcmds[i].text;++i)
    if (case_equals(authcmds[i].text,cmd)) break;

  switch (authcmds[i].fun(arg)) {
    case 0:
      if (!spp_auth(authcmds[i].text, user.s)) return;
      flagauth = 1;
      protocol = "ESMTPA";
      relayclient = "";
      remoteinfo = user.s;
      if (!env_unset("TCPREMOTEINFO")) die_read();
      if (!env_put("TCPREMOTEINFO",remoteinfo)) die_nomem();
      if (!env_put("RELAYCLIENT",relayclient)) die_nomem();
      out("235 ok, ");
      out(remoteinfo);
      out(", go ahead (#2.0.0)\r\n");
      break;
    case 1:
//      err_authfail(user.s,authcmds[i].text);
      err_authfail();
  }
}

#ifdef TLS
stralloc proto = {0};
int ssl_verified = 0;
const char *ssl_verify_err = 0;

void smtp_tls(char *arg)
{
  if (ssl) err_unimpl("unimplemented");
  else if (*arg) out("501 Syntax error (no parameters allowed) (#5.5.4)\r\n");
  else tls_init();
}

RSA *tmp_rsa_cb(SSL *ssl, int export, int keylen)
{
  if (!export) keylen = 2048;
  if (keylen == 2048) {
    FILE *in = fopen("etc/rsa2048.pem", "r");
    if (in) {
      RSA *rsa = PEM_read_RSAPrivateKey(in, NULL, NULL, NULL);
      fclose(in);
      if (rsa) return rsa;
    }
  }
  return RSA_generate_key(keylen, RSA_F4, NULL, NULL);
}

DH *tmp_dh_cb(SSL *ssl, int export, int keylen)
{
  if (!export) keylen = 2048;
  if (keylen == 2048) {
    FILE *in = fopen("etc/dh2048.pem", "r");
    if (in) {
      DH *dh = PEM_read_DHparams(in, NULL, NULL, NULL);
      fclose(in);
      if (dh) return dh;
    }
  }
  return DH_generate_parameters(keylen, DH_GENERATOR_2, NULL, NULL);
} 

/* don't want to fail handshake if cert isn't verifiable */
int verify_cb(int preverify_ok, X509_STORE_CTX *ctx) { return 1; }

void tls_nogateway()
{
  /* there may be cases when relayclient is set */
  if (!ssl || relayclient) return;
  out("; no valid cert for gatewaying");
  if (ssl_verify_err) { out(": "); out((char *)ssl_verify_err); }
}
void tls_out(const char *s1, const char *s2)
{
  out("454 TLS "); out((char *)s1);
  if (s2) { out(": "); out((char *)s2); }
  out(" (#4.3.0)\r\n"); flush();
}
void tls_err(const char *s) { tls_out(s, ssl_error()); if (smtps) die_read(); }

# define CLIENTCA "etc/clientca.pem"
# define CLIENTCRL "etc/clientcrl.pem"
# define SERVERCERT "etc/servercert.pem"

int tls_verify()
{
  stralloc clients = {0};
  struct constmap mapclients;

  if (!ssl || relayclient || ssl_verified) return 0;
  ssl_verified = 1; /* don't do this twice */

  /* request client cert to see if it can be verified by one of our CAs
   * and the associated email address matches an entry in tlsclients */
  switch (control_readfile(&clients, "etc/tlsclients", 0))
  {
  case 1:
    if (constmap_init(&mapclients, clients.s, clients.len, 0)) {
      /* if CLIENTCA contains all the standard root certificates, a
       * 0.9.6b client might fail with SSL_R_EXCESSIVE_MESSAGE_SIZE;
       * it is probably due to 0.9.6b supporting only 8k key exchange
       * data while the 0.9.6c release increases that limit to 100k */
      STACK_OF(X509_NAME) *sk = SSL_load_client_CA_file(CLIENTCA);
      if (sk) {
        SSL_set_client_CA_list(ssl, sk);
        SSL_set_verify(ssl, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, NULL);
        break;
      }
      constmap_free(&mapclients);
    }
  case 0: alloc_free(clients.s); return 0;
  case -1: die_control();
  }

  if (ssl_timeoutrehandshake(timeout, ssl_rfd, ssl_wfd, ssl) <= 0) {
    const char *err = ssl_error_str();
    tls_out("rehandshake failed", err); die_read();
  }

  do { /* one iteration */
    X509 *peercert;
    X509_NAME *subj;
    stralloc email = {0};

    int n = SSL_get_verify_result(ssl);
    if (n != X509_V_OK)
      { ssl_verify_err = X509_verify_cert_error_string(n); break; }
    peercert = SSL_get_peer_certificate(ssl);
    if (!peercert) break;

    subj = X509_get_subject_name(peercert);
    n = X509_NAME_get_index_by_NID(subj, NID_pkcs9_emailAddress, -1);
    if (n >= 0) {
      const ASN1_STRING *s = X509_NAME_get_entry(subj, n)->value;
      if (s) { email.len = s->length; email.s = (char *)s->data; }
    }

    if (email.len <= 0)
      ssl_verify_err = "contains no email address";
    else if (!constmap(&mapclients, email.s, email.len))
      ssl_verify_err = "email address not in my list of tlsclients";
    else {
      /* add the cert email to the proto if it helped allow relaying */
      --proto.len;
      if (!stralloc_cats(&proto, "\n  (cert ") /* continuation line */
        || !stralloc_catb(&proto, email.s, email.len)
        || !stralloc_cats(&proto, ")")
        || !stralloc_0(&proto)) die_nomem();
      protocol = proto.s;
      relayclient = "";
      /* also inform qmail-queue */
      if (!env_puts("RELAYCLIENT=")) die_nomem();
    }

    X509_free(peercert);
  } while (0);
  constmap_free(&mapclients); alloc_free(clients.s);

  /* we are not going to need this anymore: free the memory */
  SSL_set_client_CA_list(ssl, NULL);
  SSL_set_verify(ssl, SSL_VERIFY_NONE, NULL);

  return relayclient ? 1 : 0;
}

void tls_init()
{
  SSL *myssl;
  SSL_CTX *ctx;
  const char *ciphers;
  stralloc saciphers = {0};
  X509_STORE *store;
  X509_LOOKUP *lookup;

  SSL_library_init();

  /* a new SSL context with the bare minimum of options */
  ctx = SSL_CTX_new(SSLv23_server_method());
  if (!ctx) { tls_err("unable to initialize ctx"); return; }

  /* POODLE vulnerability */
  SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
  if (!SSL_CTX_use_certificate_chain_file(ctx, SERVERCERT))
    { SSL_CTX_free(ctx); tls_err("missing certificate"); return; }
  SSL_CTX_load_verify_locations(ctx, CLIENTCA, NULL);

#if OPENSSL_VERSION_NUMBER >= 0x00907000L
  /* crl checking */
  store = SSL_CTX_get_cert_store(ctx);
  if ((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file())) &&
      (X509_load_crl_file(lookup, CLIENTCRL, X509_FILETYPE_PEM) == 1))
    X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK |
                                X509_V_FLAG_CRL_CHECK_ALL);
#endif

#if OPENSSL_VERSION_NUMBER >= 0x10002000L
  /* support ECDH */
  SSL_CTX_set_ecdh_auto(ctx,1);
#endif

  /* set the callback here; SSL_set_verify didn't work before 0.9.6c */
  SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, verify_cb);

  /* a new SSL object, with the rest added to it directly to avoid copying */
  myssl = SSL_new(ctx);
  SSL_CTX_free(ctx);
  if (!myssl) { tls_err("unable to initialize ssl"); return; }

  /* this will also check whether public and private keys match */
  if (!SSL_use_RSAPrivateKey_file(myssl, SERVERCERT, SSL_FILETYPE_PEM))
    { SSL_free(myssl); tls_err("no valid RSA private key"); return; }

  ciphers = env_get("TLSCIPHERS");
  if (!ciphers) {
    if (control_readfile(&saciphers, "etc/tlsserverciphers", 0) == -1)
      { SSL_free(myssl); die_control(); }
    if (saciphers.len) { /* convert all '\0's except the last one to ':' */
      int i;
      for (i = 0; i < saciphers.len - 1; ++i)
        if (!saciphers.s[i]) saciphers.s[i] = ':';
      ciphers = saciphers.s;
    }
  }
  if (!ciphers || !*ciphers) ciphers = "DEFAULT";
  SSL_set_cipher_list(myssl, ciphers);
  alloc_free(saciphers.s);

  SSL_set_tmp_rsa_callback(myssl, tmp_rsa_cb);
  SSL_set_tmp_dh_callback(myssl, tmp_dh_cb);
//  SSL_set_rfd(myssl, ssl_rfd = substdio_fileno(&ssin));
//  SSL_set_wfd(myssl, ssl_wfd = substdio_fileno(&ssout));

// Kai: 'define' is from substdio.h: (not in buffer.h)
// #define substdio_fileno(s) ((s)->fd)
#define buffer_fileno(s) ((s)->fd)
  SSL_set_rfd(myssl, ssl_rfd = buffer_fileno(&bin));
  SSL_set_wfd(myssl, ssl_wfd = buffer_fileno(&bout));

  if (!smtps) { out("220 ready for tls\r\n"); flush(); }

  if (ssl_timeoutaccept(timeout, ssl_rfd, ssl_wfd, myssl) <= 0) {
    /* neither cleartext nor any other response here is part of a standard */
    const char *err = ssl_error_str();
    ssl_free(myssl); tls_out("connection failed", err); die_read();
  }
  ssl = myssl;

  /* populate the protocol string, used in Received */
  if (!stralloc_copys(&proto, "ESMTPS (")
    || !stralloc_cats(&proto, SSL_get_cipher(ssl))
    || !stralloc_cats(&proto, " encrypted)")) die_nomem();
  if (!stralloc_0(&proto)) die_nomem();
  protocol = proto.s;

  /* have to discard the pre-STARTTLS HELO/EHLO argument, if any */
  dohelo(remotehost);
}

# undef SERVERCERT
# undef CLIENTCA

#endif

struct commands smtpcommands[] = {
  { "rcpt", smtp_rcpt, 0 }
, { "mail", smtp_mail, 0 }
, { "data", smtp_data, flush }
, { "auth", smtp_auth, flush }
, { "quit", smtp_quit, flush }
, { "helo", smtp_helo, flush }
, { "ehlo", smtp_ehlo, flush }
, { "rset", smtp_rset, 0 }
, { "help", smtp_help, flush }
#ifdef TLS
, { "starttls", smtp_tls, flush_io }
#endif
, { "noop", err_noop, flush }
, { "vrfy", err_vrfy, flush }
, { 0, err_unrecog, flush }
} ;

int main(int argc,char **argv)
{
  childargs = argv + 1;
  sig_pipeignore();
  if (chdir(auto_qmail) == -1) die_control();
  setup();
  if (ipme_init() != 1) die_ipme();
  if (spp_connect()) {
    smtp_greet("220 ");
    out(" ESMTP\r\n");
  }
  if (commands(&bin,smtpcommands) == 0) die_read();
  die_nomem();
  return(0);  /* never reached */
}
