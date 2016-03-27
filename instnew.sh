#!/bin/sh
# eQmail-1.10 install script

QMAILDIR=`head -1 conf-qmail`/test

# man pages
mkdir -p $QMAILDIR/man/{man1,man5,man7,man8}
for i in 1 5 7 8 ; do
cp man/$i/* $QMAILDIR/man/man$i
done

chmod -R 0644 $QMAILDIR/man
chmod 0755 $QMAILDIR/man $QMAILDIR/man/{man1,man5,man7,man8}

# doc
mkdir -p $QMAILDIR/doc
cp doc/FAQ      $QMAILDIR/doc
cp doc/INSTAL*  $QMAILDIR/doc
cp doc/PIC.*    $QMAILDIR/doc
#cp README* $QMAILDIR/doc
cp doc/REMOVE.* $QMAILDIR/doc
cp doc/SENDMAIL $QMAILDIR/doc
cp doc/TEST.*   $QMAILDIR/doc
cp doc/UPGRADE  $QMAILDIR/doc

chmod 0644 $QMAILDIR/doc/*

# top level
#QMAILDIR=$QMAILDIR/test
mkdir -p $QMAILDIR/{alias,bin,control,queue,users}
chmod 02755        $QMAILDIR/alias
chown alias.qmail $QMAILDIR/alias
chown root.qmail  $QMAILDIR/{bin,control,users}

# queue
mkdir -p $QMAILDIR/queue/{bounce,info,intd,local,lock,mess,pid,remote,todo}
i=0
until [ $i -eq `head -1 conf-split` ]
do
  mkdir -p $QMAILDIR/queue/{info,intd,local,mess,remote,todo}/$i
  let i=$i+1
done
# set all:
chown -R qmailq.qmail $QMAILDIR/queue
chmod -R 0750         $QMAILDIR/queue
# set new: bounce, info, local, remote
chown -R qmails.qmail $QMAILDIR/queue/{bounce,info,local,remote}
chmod -R 0700         $QMAILDIR/queue/{info,intd,local,pid,remote}

# special files in 'lock'
dd if=/dev/zero of=$QMAILDIR/queue/lock/tcpto bs=2048 count=1
chown qmailr.qmail $QMAILDIR/queue/lock/tcpto
chmod 0644         $QMAILDIR/queue/lock/tcpto

dd if=/dev/zero of=$QMAILDIR/queue/lock/sendmutex bs=8 count=0
chown qmails.qmail $QMAILDIR/queue/lock/sendmutex
chmod 0600         $QMAILDIR/queue/lock/sendmutex

mkfifo -m 0622     $QMAILDIR/queue/lock/trigger
chown qmails.qmail $QMAILDIR/queue/lock/trigger

# bin
cp bouncesaying condredirect datemail elq except forward maildir2mbox \
   maildirmake maildirwatch mailsubj pinq predate qail qbiff   $QMAILDIR/bin
cp qmail-{bfrmt,clean,getpw,inject,local,lspawn,newmrh,newu} \
   qmail-{pw2u,qmqpc,qmqpd,qmtpd,qread,qstat,queue,remote} \
   qmail-{rspawn,send,showctl,smtpd,start,tcpok,tcpto}         $QMAILDIR/bin
cp qreceipt qsmhook sendmail splogger tcp-env update_tmprsadh  $QMAILDIR/bin

chown root.qmail $QMAILDIR/bin/*
chmod 0755       $QMAILDIR/bin/*

chmod 0711 $QMAILDIR/bin/qmail-{getpw,local,remote,rspawn,clean,send,pw2u} splogger
chmod 0700 $QMAILDIR/bin/qmail-{lspawn,start,newu,newmrh}

#  c(auto_qmail,"bin","qmail-queue",auto_uidq,auto_gidq,04711);
chown qmailq.qmail $QMAILDIR/bin/qmail-queue
chmod 04711        $QMAILDIR/bin/qmail-queue

#  c(auto_qmail,"bin","update_tmprsadh",auto_uido,auto_gidq,0755);
