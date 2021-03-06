# Don't edit Makefile! Use conf-* for configuration.

SHELL=/bin/sh
GNUMAKEFLAGS += --no-print-directory

CCFLAGS=-Iqlibs/include -DTLS=20160918 -DINET6
LDFLAGS=-Lqlibs
COMPILE=./compile $(CCFLAGS)
LOADBIN=./load
MAKELIB=./makelib
LOAD=$(LOADBIN)

QPRFX = `head -1 autocf-qprfx`
QLIBS = $(LDFLAGS) -lqlibs

DNSLIB = `head -1 resolv.lib`
SSLLIB += `head -1 ssl.lib`
SSLLIB += `head -1 crypto.lib`
SSLLIB += `head -1 dl.lib`

all: qconf obj eqmail man-pages

default:
	make all

begin:
	@printf "***** \033[1mbuilding eQmail\033[0m **********************************************************\n"

clean: TARGETS
	@echo -n Cleaning up sources ...
	@rm -f *.o *.a *.tmp *.lib `cat TARGETS`
	@echo " done!"
	@cd qlibs ; $(MAKE) clean
	@cd man ; $(MAKE) clean

qconf: configure
	@./configure

install: setup   # 'make install' will not work here usually ...

setup:           # ... so we route 'install' to target 'setup'
	@./install

man-pages:
	@echo Creating man pages ...
	@cd man/ ; $(MAKE)

libs:
	@printf  "***** \033[1mbuilding qlibs\033[0m ***********************************************************\n"
	@cd qlibs ; $(MAKE) ; $(MAKE) install

obj: libs begin
	$(COMPILE) buildins.c
	$(COMPILE) commands.c
	$(COMPILE) constmap.c
	$(COMPILE) control.c
	$(COMPILE) hfield.c
	$(COMPILE) maildir.c

auto_break.o:
	$(COMPILE) auto_break.c

auto_patrn.o:
	$(COMPILE) auto_patrn.c

auto_qmail.o:
	$(COMPILE) auto_qmail.c

auto_spawn.o:
	$(COMPILE) auto_spawn.c

#auto_split.o:
#	$(COMPILE) auto_split.c

auto_uids.o:
	$(COMPILE) auto_uids.c

auto_usera.o:
	$(COMPILE) auto_usera.c

bouncesaying:
	$(COMPILE) bouncesaying.c
	$(LOAD) bouncesaying $(QLIBS)

buildins.o: obj

condredirect:
	$(COMPILE) condredirect.c
	$(LOAD) condredirect qmail.o strerr.a fd.a sig.a wait.a \
	seek.a env.a buffer.a error.a str.a fs.a auto_qmail.o alloc.a

mkconfig: hostname
	cat warn-auto.sh mkconfig.sh | sed s}QPRFX}"$(QPRFX)"}g > mkconfig
	chmod 755 mkconfig

date822fmt.o: compile date822fmt.c
	./compile date822fmt.c

datemail: warn-auto.sh datemail.sh
	cat warn-auto.sh datemail.sh \
	| sed s}QPRFX}"$(QPRFX)"}g \
	> datemail
	chmod 755 datemail

datetime.a: compile makelib datetime.c
	./compile datetime.c
	./makelib datetime.a datetime.o

dns.o: compile dns.c
	./compile dns.c

except:
	$(COMPILE) except.c
	$(LOADBIN) except $(QLIBS)

fmtqfn.o: compile fmtqfn.c
	./compile fmtqfn.c

forward:
	$(COMPILE) forward.c
	$(LOADBIN) forward qmail.o auto_qmail.o $(QLIBS)

gfrom.o: compile gfrom.c
	./compile gfrom.c

hassalen.h: \
trysalen.c compile
	( ./compile trysalen.c >/dev/null 2>&1 \
	&& echo \#define HASSALEN 1 || exit 0 ) > hassalen.h
#	rm -f trysalen.o

headerbody.o: compile headerbody.c
	./compile headerbody.c

hostname:
	$(COMPILE) hostname.c
	$(LOADBIN) hostname $(QLIBS)

ipalloc.o: compile ipalloc.c
	./compile ipalloc.c

ipme.o: compile ipme.c
	./compile ipme.c

ipmeprint: ipme.o ipalloc.o
	$(COMPILE) ipmeprint.c
	$(LOADBIN) ipmeprint ipme.o ipalloc.o $(QLIBS)

eqmail: \
qmail-local qmail-lspawn qmail-getpw qmail-remote qmail-rspawn \
qmail-clean qmail-send qmail-start splogger qmail-queue qmail-inject \
predate datemail mailsubj qmail-newu qmail-print \
qmail-pw2u qmail-qread qmail-qstat qmail-tcpto qmail-tcpok \
qmail-qmqpc qmail-qmqpd qmail-qmtpd \
qmail-smtpd sendmail tcp-env qmail-newmrh mkconfig \
qmail-tcpsrv tcprules \
qreceipt \
forward preline condredirect bouncesaying except maildirmake \
maildir2mbox \
qmail-before ipmeprint \
mkrsadhkeys mksrvrcerts qmail-fixq qmail-shcfg

maildir2mbox:
	$(COMPILE) maildir2mbox.c
	$(LOAD) maildir2mbox  maildir.o prioq.o now.o myctime.o gfrom.o \
	 qlibs/strerr.a datetime.a $(LDFLAGS) -lqlibs

maildirmake:
	$(COMPILE) maildirmake.c
	./load maildirmake strerr.a buffer.a error.a str.a

mailsubj: warn-auto.sh mailsubj.sh
	cat warn-auto.sh mailsubj.sh | sed s}QPRFX}"$(QPRFX)"}g > mailsubj
	chmod 755 mailsubj

myctime.o: compile myctime.c
	./compile myctime.c

newfield.o: compile newfield.c
	./compile newfield.c

now.o: compile now.c
	./compile now.c

predate:
	$(COMPILE) predate.c
	$(LOAD) predate datetime.a strerr.a sig.a fd.a wait.a \
	buffer.a error.a str.a fs.a

preline:
	$(COMPILE) preline.c
	$(LOADBIN) preline $(QLIBS)

prioq.o: compile prioq.c
	./compile prioq.c

qmail-before: qmail-before.sh
	cat warn-auto.sh qmail-before.sh \
	| sed s}QPREFIX}"$(QPRFX)"}g \
	| sed s}CFGFILE}"beforequeue"}g \
	| sed s}PROGRAM}"qmail-queue"}g > qmail-bfque
	cat warn-auto.sh qmail-before.sh \
	| sed s}QPREFIX}"$(QPRFX)"}g \
	| sed s}CFGFILE}"beforemote"}g \
	| sed s}PROGRAM}"qmail-remote"}g > qmail-bfrmt
	chmod 754 qmail-bfque qmail-bfrmt
	chgrp qmail qmail-bfque qmail-bfrmt

qmail-clean: fmtqfn.o buildins.o
	$(COMPILE) qmail-clean.c
	$(LOAD) qmail-clean fmtqfn.o now.o getln.a sig.a stralloc.a \
	alloc.a buffer.a error.a str.a fs.a buildins.o auto_qmail.o

qmail-fixq: qmail-fixq.sh
	sh ./qmail-fixq.sh

qmail-getpw: auto_break.o auto_usera.o
	$(COMPILE) qmail-getpw.c
	$(LOADBIN) qmail-getpw auto_break.o auto_usera.o $(QLIBS)

qmail-inject: compile load qmail-inject.c headerbody.o hfield.o token822.o
#newfield.o quote.o now.o control.o date822fmt.o constmap.o qmail.o \
#case.a fd.a wait.a open.a getln.a sig.a getopt.a datetime.a  \
#env.a stralloc.a alloc.a error.a str.a fs.a auto_qmail.o
	$(COMPILE) qmail-inject.c
	./load qmail-inject headerbody.o hfield.o newfield.o quote.o now.o \
	control.o date822fmt.o constmap.o qmail.o case.a fd.a wait.a open.a \
	getln.a sig.a getopt.a datetime.a token822.o env.a stralloc.a \
	alloc.a buffer.a error.a str.a fs.a auto_qmail.o strerr.a

qmail-local: qmail.o quote.o now.o gfrom.o \
myctime.o datetime.a auto_qmail.o auto_patrn.o
	$(COMPILE) $(CCFLAGS) qmail-local.c
	./load qmail-local qmail.o quote.o now.o gfrom.o myctime.o \
	datetime.a auto_qmail.o auto_patrn.o $(QLIBS) strerr.a

qmail-lspawn: spawn.o auto_qmail.o auto_uids.o auto_spawn.o
	$(COMPILE) qmail-lspawn.c
	$(LOADBIN) qmail-lspawn spawn.o prot.o $(QLIBS) \
	auto_qmail.o auto_uids.o auto_spawn.o

qmail-newmrh:
	$(COMPILE) qmail-newmrh.c
	$(LOAD) qmail-newmrh cdb.a getln.a open.a \
	seek.a case.a stralloc.a alloc.a strerr.a buffer.a \
	error.a str.a auto_qmail.o

qmail-newu:
	$(COMPILE) qmail-newu.c
	$(LOAD) qmail-newu cdb.a getln.a open.a seek.a buffer.a \
	case.a stralloc.a alloc.a error.a str.a auto_qmail.o

qmail-print: buildins.o
	$(COMPILE) qmail-print.c
	$(LOAD) qmail-print buildins.o $(LDFLAGS) -lqlibs

qmail-pw2u:
	$(COMPILE) qmail-pw2u.c
	$(LOAD) qmail-pw2u constmap.o control.o open.a getln.a \
	case.a getopt.a stralloc.a alloc.a buffer.a error.a str.a \
	fs.a auto_usera.o auto_break.o auto_qmail.o

qmail-qmqpc: obj auto_qmail.o
	$(COMPILE) qmail-qmqpc.c
	$(LOADBIN) qmail-qmqpc control.o auto_qmail.o $(QLIBS)

qmail-qmqpd: received.o now.o date822fmt.o qmail.o auto_qmail.o
	$(COMPILE) qmail-qmqpd.c
	$(LOADBIN) qmail-qmqpd received.o now.o date822fmt.o datetime.a \
	qmail.o auto_qmail.o $(QLIBS)

qmail-qmtpd: rcpthosts.o control.o constmap.o received.o \
date822fmt.o now.o qmail.o cdb.a fd.a wait.a datetime.a open.a \
getln.a sig.a case.a env.a stralloc.a alloc.a error.a \
str.a fs.a auto_qmail.o
	$(COMPILE) qmail-qmtpd.c
	./load qmail-qmtpd rcpthosts.o control.o constmap.o \
	received.o date822fmt.o now.o qmail.o cdb.a fd.a wait.a \
	datetime.a open.a getln.a sig.a case.a env.a stralloc.a \
	alloc.a error.a str.a fs.a auto_qmail.o buffer.a

qmail-qread: fmtqfn.o readsubdir.o date822fmt.o datetime.a auto_qmail.o
	$(COMPILE) qmail-qread.c
	$(LOADBIN) qmail-qread fmtqfn.o readsubdir.o date822fmt.o \
	datetime.a $(QLIBS) auto_qmail.o buildins.o

qmail-qstat: warn-auto.sh qmail-qstat.sh
	cat warn-auto.sh qmail-qstat.sh | sed s}QPRFX}"$(QPRFX)"}g > qmail-qstat
	chmod 755 qmail-qstat

qmail-queue: trigger.o buildins.o fmtqfn.o now.o date822fmt.o datetime.a
	$(COMPILE) qmail-queue.c
	$(LOADBIN) qmail-queue trigger.o fmtqfn.o now.o date822fmt.o \
	datetime.a $(QLIBS) buildins.o auto_uids.o

qmail-remote: control.o constmap.o \
timeoutread.o timeoutwrite.o timeoutconn.o tcpto.o now.o dns.o ipalloc.o ipme.o quote.o \
auto_qmail.o tls.o ssl_timeoutio.o
	$(COMPILE) qmail-remote.c
	./load qmail-remote control.o constmap.o tcpto.o now.o dns.o \
	timeoutread.o timeoutwrite.o timeoutconn.o \
	tls.o ssl_timeoutio.o $(SSLLIB) ipalloc.o ipme.o quote.o \
	auto_qmail.o $(DNSLIB) $(QLIBS)

qmail-rspawn: spawn.o tcpto_clean.o now.o
	$(COMPILE) qmail-rspawn.c
	$(LOADBIN) qmail-rspawn spawn.o tcpto_clean.o now.o $(QLIBS) \
	auto_qmail.o auto_uids.o auto_spawn.o

qmail-send: obj date822fmt.o newfield.o prioq.o qsutil.o readsubdir.o trigger.o
	$(COMPILE) qmail-send.c
	./load qmail-send qsutil.o control.o constmap.o newfield.o \
	prioq.o trigger.o fmtqfn.o quote.o now.o readsubdir.o \
	qmail.o date822fmt.o datetime.a auto_qmail.o $(QLIBS) buildins.o

qmail-shcfg: qmail-shcfg.sh warn-auto.sh
	@echo Creating $@
	@cat warn-auto.sh qmail-shcfg.sh | sed s}QPRFX}"$(QPRFX)"}g > qmail-shcfg
	@chmod 0755 qmail-shcfg

qmail-spp.o:
	$(COMPILE) qmail-spp.c

qmail-smtpd: rcpthosts.o commands.o \
timeoutread.o timeoutwrite.o ipme.o ipalloc.o control.o constmap.o \
received.o date822fmt.o now.o qmail.o cdb.a datetime.a \
tls.o ssl_timeoutio.o qmail-spp.o
	$(COMPILE) qmail-smtpd.c
	$(LOADBIN) qmail-smtpd rcpthosts.o commands.o timeoutread.o \
	timeoutwrite.o ipme.o ipalloc.o control.o constmap.o \
	tls.o ssl_timeoutio.o $(SSLLIB) \
	received.o date822fmt.o now.o qmail.o cdb.a fd.a wait.a \
	datetime.a qmail-spp.o auto_qmail.o $(QLIBS)

qmail-start: prot.o fd.a auto_uids.o
	$(COMPILE) qmail-start.c
	$(LOADBIN) qmail-start prot.o fd.a auto_uids.o

qmail-tcpok:
	$(COMPILE) qmail-tcpok.c
	$(LOADBIN) qmail-tcpok buildins.o $(QLIBS)

qmail-tcpto: now.o auto_qmail.o
	$(COMPILE) qmail-tcpto.c
	$(LOADBIN) qmail-tcpto now.o $(QLIBS) auto_qmail.o

qmail.o:
	$(COMPILE) qmail.c

qreceipt: headerbody.o hfield.o quote.o token822.o qmail.o auto_qmail.o
	$(COMPILE) qreceipt.c
	$(LOADBIN) qreceipt headerbody.o hfield.o quote.o token822.o \
	qmail.o auto_qmail.o $(QLIBS)

qsutil.o: compile qsutil.c
	./compile qsutil.c

quote.o: compile quote.c
	./compile quote.c

rcpthosts.o: compile rcpthosts.c 
	./compile rcpthosts.c

readsubdir.o: compile readsubdir.c
	./compile readsubdir.c

received.o: compile received.c
	./compile received.c

remoteinfo.o: compile remoteinfo.c
	./compile remoteinfo.c

sendmail:
	$(COMPILE) sendmail.c
	$(LOADBIN) sendmail auto_qmail.o $(QLIBS)

spawn.o:
	$(COMPILE) spawn.c

splogger:
	$(COMPILE) splogger.c
	$(LOADBIN) splogger $(QLIBS)

tcp-env: compile load tcp-env.c dns.o remoteinfo.o timeoutread.o \
timeoutwrite.o timeoutconn.o ip.a ipalloc.o case.a ndelay.a sig.a \
env.a stralloc.a alloc.a substdio.a error.a str.a fs.a hassalen.h
	./compile tcp-env.c
	./load tcp-env dns.o remoteinfo.o timeoutread.o \
	timeoutwrite.o timeoutconn.o ip.a ipalloc.o case.a ndelay.a \
	sig.a env.a getopt.a stralloc.a alloc.a error.a \
	str.a fs.a $(DNSLIB) buffer.a \
	qlibs/qlibs.a substdio.a

tcpto.o:
	$(COMPILE) tcpto.c

tcpto_clean.o: compile tcpto_clean.c
	./compile tcpto_clean.c

timeoutconn.o: compile timeoutconn.c
	./compile timeoutconn.c

timeoutread.o: compile timeoutread.c
	./compile timeoutread.c

timeoutwrite.o: compile timeoutwrite.c
	./compile timeoutwrite.c

tls.o: compile tls.c
	./compile tls.c

ssl_timeoutio.o: compile ssl_timeoutio.c
	./compile ssl_timeoutio.c

token822.o: compile token822.c
	./compile token822.c

trigger.o:
	$(COMPILE) trigger.c

mkrsadhkeys: qmail-print
	@cat warn-auto.sh mkrsadhkeys.sh \
	| sed s}QPRFX}"$(QPRFX)"}g \
	| sed s}UID}"`head -1 pemid.tmp`"}g \
	| sed s}GID}"`head -2 pemid.tmp | tail -1`"}g \
	> $@
	@echo creating $@
	@chmod 755 $@

mksrvrcerts: qmail-print
	@cat warn-auto.sh mksrvrcerts.sh \
	| sed s}OPENSSLBIN}"`head -1 ssl.bin`"}g \
	| sed s}QPRFX}"$(QPRFX)"}g \
	| sed s}UID}"`head -1 pemid.tmp`"}g \
	| sed s}GID}"`head -2 pemid.tmp | tail -1`"}g \
	> $@
	@echo creating $@
	@chmod 755 $@

#*****************************************************************
# qmail-tcpsrv
remoteinfo6.o:
	$(COMPILE) remoteinfo6.c

rules.o:
	$(COMPILE) rules.c

tcprules: rules.o
	$(COMPILE) tcprules.c
	$(LOADBIN) tcprules rules.o strerr.a $(QLIBS)

qmail-tcpsrv: rules.o remoteinfo6.o
	$(COMPILE) qmail-tcpsrv.c
	$(LOADBIN) qmail-tcpsrv rules.o remoteinfo6.o strerr.a $(QLIBS)
