/*
 *  Revision 20160510, Kai Peter
 *  - removed unused vars: 'i' (324), fqdn (383), txt (45)
 *  Revision 20160509, Kai Peter
 *  - added some parentheses
 *  - removed unused var 'len' in function iaafmt6()
 */
#include <stdio.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <sys/socket.h>
#include <resolv.h>
#include <errno.h>
extern int res_query();
extern int res_search();
#include "ip.h"
#include "ipalloc.h"
#include "fmt.h"
#include "alloc.h"
#include "str.h"
#include "stralloc.h"
#include "dns.h"
#include "case.h"

static unsigned short getshort(c) unsigned char *c;
{ unsigned short u; u = c[0]; return (u << 8) + c[1]; }

static struct { unsigned char *buf; } response;
static int responsebuflen = 0;
static int responselen;
static unsigned char *responseend;
static unsigned char *responsepos;
static u_long saveresoptions;

static int numanswers;
static char name[MAXDNAME];
static struct ip_address ip;
#ifdef INET6
static struct ip6_address ip6;
#endif
//static stralloc txt = {0};
unsigned short pref;

static stralloc glue = {0};

static int (*lookup)() = res_query;

static int resolve(domain,type)
stralloc *domain;
int type;
{
 int n;
 int i;

 errno = 0;
 if (!stralloc_copy(&glue,domain)) return DNS_MEM;
 if (!stralloc_0(&glue)) return DNS_MEM;
 if (!responsebuflen)
  {
  if ((response.buf = (unsigned char *)alloc(PACKETSZ+1)))
   responsebuflen = PACKETSZ+1;
  else return DNS_MEM;
  }

 responselen = lookup(glue.s,C_IN,type,response.buf,responsebuflen);
 if ((responselen >= responsebuflen) ||
     (responselen > 0 && (((HEADER *)response.buf)->tc)))
  {
   if (responsebuflen < 65536)
   {
    if ((alloc_re(&response.buf, responsebuflen, 65536)))
     responsebuflen = 65536;
    else return DNS_MEM;
    saveresoptions = _res.options;
    _res.options |= RES_USEVC;
    responselen = lookup(glue.s,C_IN,type,response.buf,responsebuflen);
    _res.options = saveresoptions;
   }
  }
 if (responselen <= 0)
  {
   if (errno == ECONNREFUSED) return DNS_SOFT;
   if (h_errno == TRY_AGAIN) return DNS_SOFT;
   return DNS_HARD;
  }
 responseend = response.buf + responselen;
 responsepos = response.buf + sizeof(HEADER);
 n = ntohs(((HEADER *)response.buf)->qdcount);
 while (n-- > 0)
  {
   i = dn_expand(response.buf,responseend,responsepos,name,MAXDNAME);
   if (i < 0) return DNS_SOFT;
   responsepos += i;
   i = responseend - responsepos;
   if (i < QFIXEDSZ) return DNS_SOFT;
   responsepos += QFIXEDSZ;
  }
 numanswers = ntohs(((HEADER *)response.buf)->ancount);
 return 0;
}

static int findname(wanttype)
int wanttype;
{
 unsigned short rrtype;
 unsigned short rrdlen;
 int i;

 if (numanswers <= 0) return 2;
 --numanswers;
 if (responsepos == responseend) return DNS_SOFT;

 i = dn_expand(response.buf,responseend,responsepos,name,MAXDNAME);
 if (i < 0) return DNS_SOFT;
 responsepos += i;

 i = responseend - responsepos;
 if (i < 4 + 3 * 2) return DNS_SOFT;

 rrtype = getshort(responsepos);
 rrdlen = getshort(responsepos + 8);
 responsepos += 10;

 if (rrtype == wanttype)
  {
   if (dn_expand(response.buf,responseend,responsepos,name,MAXDNAME) < 0)
     return DNS_SOFT;
   responsepos += rrdlen;
   return 1;
  }

 responsepos += rrdlen;
 return 0;
}

static int findip(wanttype)
int wanttype;
{
 unsigned short rrtype;
 unsigned short rrdlen;
 int i;

 if (numanswers <= 0) return 2;
 --numanswers;
 if (responsepos == responseend) return DNS_SOFT;

 i = dn_expand(response.buf,responseend,responsepos,name,MAXDNAME);
 if (i < 0) return DNS_SOFT;
 responsepos += i;

 i = responseend - responsepos;
 if (i < 4 + 3 * 2) return DNS_SOFT;
   
 rrtype = getshort(responsepos);
 rrdlen = getshort(responsepos + 8);
 responsepos += 10;

 if (rrtype == wanttype)
  {
   if (rrdlen < 4)
     return DNS_SOFT;
   ip.d[0] = responsepos[0];
   ip.d[1] = responsepos[1];
   ip.d[2] = responsepos[2];
   ip.d[3] = responsepos[3];
   responsepos += rrdlen;
   return 1;
  }

 responsepos += rrdlen;
 return 0;
}

#ifdef INET6
static int findip6(wanttype)
int wanttype;
{
  unsigned short rrtype;
  unsigned short rrdlen;
  int i;

  if (numanswers <= 0) return 2;
  --numanswers;
  if (responsepos == responseend) return DNS_SOFT;

  i = dn_expand(response.buf,responseend,responsepos,name,MAXDNAME);
  if (i < 0) return DNS_SOFT;
  responsepos += i;

  i = responseend - responsepos;
  if (i < 4 + 3 * 2) return DNS_SOFT;

  rrtype = getshort(responsepos);
  rrdlen = getshort(responsepos + 8);
  responsepos += 10;

  if (rrtype == wanttype)
  {
	if (rrdlen < 16)
	  return DNS_SOFT;
	byte_copy(&ip6.d, 16, &responsepos[0]);
	responsepos += rrdlen;
	return 1;
  }

  responsepos += rrdlen;
  return 0;
}
#endif

static int findmx(wanttype)
int wanttype;
{
 unsigned short rrtype;
 unsigned short rrdlen;
 int i;

 if (numanswers <= 0) return 2;
 --numanswers;
 if (responsepos == responseend) return DNS_SOFT;

 i = dn_expand(response.buf,responseend,responsepos,name,MAXDNAME);
 if (i < 0) return DNS_SOFT;
 responsepos += i;

 i = responseend - responsepos;
 if (i < 4 + 3 * 2) return DNS_SOFT;
   
 rrtype = getshort(responsepos);
 rrdlen = getshort(responsepos + 8);
 responsepos += 10;

 if (rrtype == wanttype)
  {
   if (rrdlen < 3)
     return DNS_SOFT;
   pref = (responsepos[0] << 8) + responsepos[1];
   if (dn_expand(response.buf,responseend,responsepos + 2,name,MAXDNAME) < 0)
     return DNS_SOFT;
   responsepos += rrdlen;
   return 1;
  }

 responsepos += rrdlen;
 return 0;
}

void dns_init(flagsearch)
int flagsearch;
{
 res_init();
 if (flagsearch) lookup = res_search;
}

int dns_cname(sa)
stralloc *sa;
{
 int r;
 int loop;
 for (loop = 0;loop < 10;++loop)
  {
   if (!sa->len) return loop;
   if (sa->s[sa->len - 1] == ']') return loop;
   if (sa->s[sa->len - 1] == '.') { --sa->len; continue; }
   switch(resolve(sa,T_CNAME))
    {
     case DNS_MEM: return DNS_MEM;
     case DNS_SOFT: return DNS_SOFT;
     case DNS_HARD: return loop;
     default:
       while ((r = findname(T_CNAME)) != 2)
	{
	 if (r == DNS_SOFT) return DNS_SOFT;
	 if (r == 1)
	  {
	   if (!stralloc_copys(sa,name)) return DNS_MEM;
	   break;
	  }
	}
       if (r == 2) return loop;
    }
  }
 return DNS_HARD; /* alias loop */
}

#define FMT_IAA 40

static int iaafmt(s,ip)
char *s;
struct ip_address *ip;
{
 unsigned int i;
 unsigned int len;
 len = 0;
 i = fmt_ulong(s,(unsigned long) ip->d[3]); len += i; if (s) s += i;
 i = fmt_str(s,"."); len += i; if (s) s += i;
 i = fmt_ulong(s,(unsigned long) ip->d[2]); len += i; if (s) s += i;
 i = fmt_str(s,"."); len += i; if (s) s += i;
 i = fmt_ulong(s,(unsigned long) ip->d[1]); len += i; if (s) s += i;
 i = fmt_str(s,"."); len += i; if (s) s += i;
 i = fmt_ulong(s,(unsigned long) ip->d[0]); len += i; if (s) s += i;
 i = fmt_str(s,".in-addr.arpa."); len += i; if (s) s += i;
 return len;
}

int dns_ptr(sa,ip)
stralloc *sa;
struct ip_address *ip;
{
 int r;

 if (!stralloc_ready(sa,iaafmt((char *) 0,ip))) return DNS_MEM;
 sa->len = iaafmt(sa->s,ip);
 switch(resolve(sa,T_PTR))
  {
   case DNS_MEM: return DNS_MEM;
   case DNS_SOFT: return DNS_SOFT;
   case DNS_HARD: return DNS_HARD;
  }
 while ((r = findname(T_PTR)) != 2)
  {
   if (r == DNS_SOFT) return DNS_SOFT;
   if (r == 1)
    {
     if (!stralloc_copys(sa,name)) return DNS_MEM;
     return 0;
    }
  }
 return DNS_HARD;
}

#ifdef INET6
static int iaafmt6(s,ip)
char *s;
struct ip6_address *ip;
{
//  unsigned int i;
  int j;
//  unsigned int len;
  static char data[] = "0123456789abcdef";
//  len = 0;

  if (s) {
	for (j = 15; j >= 0; j--) {
	  *s++ = data[ip->d[j] & 0x0f];
	  *s++ = '.';
	  *s++ = data[(ip->d[j] >> 4) & 0x0f];
	  *s++ = '.';
	}
	str_copy(s, "ip6.int");
  }
  return 71;
  /* 1.2.3.4.5.6.7.8.9.a.b.c.d.e.f.1.2.3.4.5.6.7.8.9.a.b.c.d.e.f.ip6.int */
}

int dns_ptr6(sa,ip)
stralloc *sa;
struct ip6_address *ip;
{
  int r;

  if (!stralloc_ready(sa,iaafmt6((char *) 0,ip))) return DNS_MEM;
  sa->len = iaafmt6(sa->s,ip);
  switch(resolve(sa,T_PTR))
  {
	case DNS_MEM: return DNS_MEM;
	case DNS_SOFT: return DNS_SOFT;
	case DNS_HARD: return DNS_HARD;
  }
  while ((r = findname(T_PTR)) != 2)
  {
	if (r == DNS_SOFT) return DNS_SOFT;
	if (r == 1)
	{
	  if (!stralloc_copys(sa,name)) return DNS_MEM;
	  return 0;
	}
  }
  return DNS_HARD;
}
#endif

static int dns_ipplus(ia,sa,pref)
ipalloc *ia;
stralloc *sa;
int pref;
{
  int r;
  struct ip_mx ix = {0};
  int err4 = 0, err6 = 0;
// Kai: gcc warning: unused variable
//#ifdef TLS
//  stralloc fqdn = {0};
//#endif

 if (!stralloc_copy(&glue,sa)) return DNS_MEM;
 if (!stralloc_0(&glue)) return DNS_MEM;
 if (glue.s[0]) {
	ix.af = AF_INET;
//	if (!glue.s[ip_scan(glue.s,&ix.addr.ip)] || !glue.s[ip_scanbracket(glue.s,&ix.addr.ip)])
	if (!glue.s[ip4_scan(glue.s,(char *)&ix.addr.ip)] || !glue.s[ip_scanbracket(glue.s,(char *)&ix.addr.ip)])
    {
	ix.af = AF_INET;
	if (!ipalloc_append(ia,&ix)) return DNS_MEM;
	return 0;
    }
 }

#ifdef INET6
 switch(resolve(sa,T_AAAA))
   {
   case DNS_MEM: err6 = DNS_MEM; break;
   case DNS_SOFT: err6 = DNS_SOFT; break;
   case DNS_HARD: err6 = DNS_HARD; break;
   default:
     while ((r = findip6(T_AAAA)) != 2)
       {
         ix.af = AF_INET6;
         ix.addr.ip6 = ip6;
         ix.pref = pref;
   if (r == DNS_SOFT) { err6 = DNS_SOFT; break; }
   if (r == 1)
     if (!ipalloc_append(ia,&ix)) { err6 = DNS_MEM; break; }
       }
     break;
   }
#endif

 switch(resolve(sa,T_A))
 {
	case DNS_MEM: err4 = DNS_MEM; break;
	case DNS_SOFT: err4 = DNS_SOFT; break;
	case DNS_HARD: err4 = DNS_HARD; break;
	default:
  while ((r = findip(T_A)) != 2)
  {
	ix.af = AF_INET;
	ix.addr.ip = ip;
	ix.pref = pref;
	if (r == DNS_SOFT) { err4 = DNS_SOFT; break; }
	if (r == 1) {
#ifdef IX_FQDN
     ix.fqdn = glue.s;
#endif
	if (!ipalloc_append(ia,&ix)) { err4 = DNS_MEM; break; }
	}
  }
  break;
 }
#ifdef IX_FQDN
 glue.s = 0;
#endif
#ifdef INET6
  if (err4 != 0 && err6 != 0) {
	return err4;
  }
  return 0;
#else
  return err4;
#endif
//  }
}
int dns_ip(ia,sa)
ipalloc *ia;
stralloc *sa;
{
 if (!ipalloc_readyplus(ia,0)) return DNS_MEM;
 ia->len = 0;
 return dns_ipplus(ia,sa,0);
}

int dns_mxip(ia,sa,random)
ipalloc *ia;
stralloc *sa;
unsigned long random;
{
 int r;
 struct mx { stralloc sa; unsigned short p; } *mx;
 struct ip_mx ix = {0};
 int nummx;
 int i;
 int j;
 int flagsoft;

 if (!ipalloc_readyplus(ia,0)) return DNS_MEM;
 ia->len = 0;

 if (!stralloc_copy(&glue,sa)) return DNS_MEM;
 if (!stralloc_0(&glue)) return DNS_MEM;
 if (glue.s[0]) {
    ix.pref = 0;
//	if (!glue.s[ip_scan(glue.s,&ix.addr.ip)] || !glue.s[ip_scanbracket(glue.s,&ix.addr.ip)])
	if (!glue.s[ip4_scan(glue.s,(char *)&ix.addr.ip)] || !glue.s[ip_scanbracket(glue.s,(char *)&ix.addr.ip)])
    {
#ifdef TLS
  	  ix.fqdn = NULL;
#endif
	  ix.af = AF_INET;
     if (!ipalloc_append(ia,&ix)) return DNS_MEM;
     return 0;
    }
 }

 switch(resolve(sa,T_MX))
  {
   case DNS_MEM: return DNS_MEM;
   case DNS_SOFT: return DNS_SOFT;
   case DNS_HARD: return dns_ip(ia,sa);
  }

 mx = (struct mx *) alloc(numanswers * sizeof(struct mx));
 if (!mx) return DNS_MEM;
 nummx = 0;

 while ((r = findmx(T_MX)) != 2)
  {
   if (r == DNS_SOFT) { alloc_free(mx); return DNS_SOFT; }
   if (r == 1)
    {
     mx[nummx].p = pref;
     mx[nummx].sa.s = 0;
     if (!stralloc_copys(&mx[nummx].sa,name))
      {
       while (nummx > 0) alloc_free(mx[--nummx].sa.s);
       alloc_free(mx); return DNS_MEM;
      }
     ++nummx;
    }
  }

 if (!nummx) return dns_ip(ia,sa); /* e.g., CNAME -> A */

 flagsoft = 0;
 while (nummx > 0)
  {
   unsigned long numsame;

   i = 0;
   numsame = 1;
   for (j = 1;j < nummx;++j)
     if (mx[j].p < mx[i].p)
      {
       i = j;
       numsame = 1;
      }
     else if (mx[j].p == mx[i].p)
      {
       ++numsame;
       random = random * 69069 + 1;
       if ((random / 2) < (2147483647 / numsame))
         i = j;
      }

   switch(dns_ipplus(ia,&mx[i].sa,mx[i].p))
    {
     case DNS_MEM: case DNS_SOFT:
       flagsoft = 1; break;
    }

   alloc_free(mx[i].sa.s);
   mx[i] = mx[--nummx];
  }

 alloc_free(mx);
 return flagsoft;
}
