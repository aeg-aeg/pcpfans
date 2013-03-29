#! /bin/sh
#
# Copyright (c) 1995-2000,2003 Silicon Graphics, Inc.  All Rights Reserved.
# 
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.
# 
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
# 
# Example administrative script to check pmlogger instances are alive,
# and restart as required.
#

# Get standard environment
. $PCP_DIR/etc/pcp.env

# Get the portable PCP rc script functions
if [ -f  $PCP_SHARE_DIR/lib/rc-proc.sh ]
then 
    . $PCP_SHARE_DIR/lib/rc-proc.sh
fi

# error messages should go to stderr, not the GUI notifiers
unset PCP_STDERR

# constant setup
#
tmp=`mktemp -d /tmp/pcp.XXXXXXXXX` || exit 1
status=0
echo >$tmp/lock
trap "rm -rf \`[ -f $tmp/lock ] && cat $tmp/lock\` $tmp; exit \$status" 0 1 2 3 15
prog=`basename $0`

# control file for pmlogger administration ... edit the entries in this
# file to reflect your local configuration
#
CONTROL=$PCP_PMLOGGERCONTROL_PATH

# determine real name for localhost
LOCALHOSTNAME=`hostname | sed -e 's/\..*//'`
[ -z "$LOCALHOSTNAME" ] && LOCALHOSTNAME=localhost

# determine path for pwd command to override shell built-in
# (see BugWorks ID #595416).
PWDCMND=`which pwd 2>/dev/null | $PCP_AWK_PROG '
BEGIN	    	{ i = 0 }
/ not in /  	{ i = 1 }
/ aliased to /  { i = 1 }
 	    	{ if ( i == 0 ) print }
'`
if [ -z "$PWDCMND" ]
then
    #  Looks like we have no choice here...
    #  force it to a known IRIX location
    PWDCMND=/bin/pwd
fi
eval $PWDCMND -P >/dev/null 2>&1
[ $? -eq 0 ] && PWDCMND="$PWDCMND -P"

# default location
#
logfile=pmlogger.log


# option parsing
#
SHOWME=false
MV=mv
TERSE=false
VERBOSE=false
VERY_VERBOSE=false
usage="Usage: $prog [-NTV] [-c control]"
while getopts c:NTV? c
do
    case $c
    in
	c)	CONTROL="$OPTARG"
		;;
	N)	SHOWME=true
		MV="echo + mv"
		;;
	T)	TERSE=true
		;;
	V)	if $VERBOSE
		then
		    VERY_VERBOSE=true
		else
		    VERBOSE=true
		fi
		;;
	?)	echo "$usage"
		status=1
		exit
		;;
    esac
done
shift `expr $OPTIND - 1`

if [ $# -ne 0 ]
then
    echo "$usage"
    status=1
    exit
fi

if [ ! -f $CONTROL ]
then
    echo "$prog: Error: cannot find control file ($CONTROL)"
    status=1
    exit
fi

_error()
{
    echo "$prog: [$CONTROL:$line]"
    echo "Error: $1"
    echo "... logging for host \"$host\" unchanged"
    touch $tmp/err
}

_warning()
{
    echo "$prog [$CONTROL:$line]"
    echo "Warning: $1"
}

_message()
{
    case $1
    in
	restart)
	    $PCP_ECHO_PROG $PCP_ECHO_N "Restarting$iam pmlogger for host \"$host\" ...""$PCP_ECHO_C"
	    ;;
    esac
}

_unlock()
{
    rm -f lock
    echo >$tmp/lock
}

_get_ino()
{
    # get inode number for $1
    # throw away stderr (and return '') in case $1 has been removed by now
    #
    stat "$1" 2>/dev/null \
    | sed -n '/Device:[ 	].*[ 	]Inode:/{
s/Device:[ 	].*[ 	]Inode:[ 	]*//
s/[ 	].*//
p
}'
}

_get_logfile()
{
    # looking for -lLOGFILE or -l LOGFILE in args
    #
    want=false
    for a in $args
    do
	if $want
	then
	    logfile="$a"
	    want=false
	    break
	fi
	case "$a"
	in
	    -l)
		want=true
		;;
	    -l*)
		logfile=`echo "$a" | sed -e 's/-l//'`
		break
		;;
	esac
    done
}

_check_logfile()
{
    if [ ! -f $logfile ]
    then
	echo "$prog: Error: cannot find pmlogger output file at \"$logfile\""
	if $TERSE
	then
	    :
	else
	    logdir=`dirname $logfile`
	    echo "Directory (`cd $logdir; $PWDCMND`) contents:"
	    LC_TIME=POSIX ls -la $logdir
	fi
    else
	echo "Contents of pmlogger output file \"$logfile\" ..."
	cat $logfile
    fi
}

_check_logger()
{
    $VERBOSE && $PCP_ECHO_PROG $PCP_ECHO_N " [process $1] ""$PCP_ECHO_C"

    # wait until pmlogger process starts, or exits
    #
    delay=5
    [ ! -z "$PMCD_CONNECT_TIMEOUT" ] && delay=$PMCD_CONNECT_TIMEOUT
    x=5
    [ ! -z "$PMCD_REQUEST_TIMEOUT" ] && x=$PMCD_REQUEST_TIMEOUT

    # wait for maximum time of a connection and 20 requests
    #
    delay=`expr \( $delay + 20 \* $x \) \* 10`	# tenths of a second
    while [ $delay -gt 0 ]
    do
	if [ -f $logfile ]
	then
	    # $logfile was previously removed, if it has appeared again
	    # then we know pmlogger has started ... if not just sleep and
	    # try again
	    #
	    if echo "connect $1" | pmlc 2>&1 | grep "Unable to connect" >/dev/null
	    then
		:
	    else
		$VERBOSE && echo " done"
		return 0
	    fi

	    _plist=`_get_pids_by_name pmlogger`
	    _found=false
	    for _p in `echo $_plist`
	    do
		[ $_p -eq $1 ] && _found=true
	    done

	    if $_found
	    then
		# process still here, just not accepting pmlc connections
		# yet, try again
		:
	    else
		$VERBOSE || _message restart
		echo " process exited!"
		if $TERSE
		then
		    :
		else
		    echo "$prog: Error: failed to restart pmlogger"
		    echo "Current pmlogger processes:"
		    $PCP_PS_PROG $PCP_PS_ALL_FLAGS | tee $tmp/tmp | sed -n -e 1p
		    for _p in `echo $_plist`
		    do
			sed -n -e "/^[ ]*[^ ]* [ ]*$_p /p" < $tmp/tmp
		    done 
		    echo
		fi
		_check_logfile
		return 1
	    fi
	fi
	pmsleep 0.1
	delay=`expr $delay - 1`
	$VERBOSE && [ `expr $delay % 10` -eq 0 ] && \
			$PCP_ECHO_PROG $PCP_ECHO_N ".""$PCP_ECHO_C"
    done
    $VERBOSE || _message restart
    echo " timed out waiting!"
    if $TERSE
    then
	:
    else
	sed -e 's/^/	/' $tmp/out
    fi
    _check_logfile
    return 1
}

# note on control file format version
#  1.0 was shipped as part of PCPWEB beta, and did not include the
#	socks field [this is the default for backwards compatibility]
#  1.1 is the first production release, and the version is set in
#	the control file with a $version=1.1 line (see below)
#
version=''

echo >$tmp/dir
rm -f $tmp/err
line=0
cat $CONTROL \
 | sed -e "s/LOCALHOSTNAME/$LOCALHOSTNAME/g" \
       -e "s;PCP_LOG_DIR;$PCP_LOG_DIR;g" \
 | while read host primary socks dir args
do
    line=`expr $line + 1`
    $VERY_VERBOSE && echo "[control:$line] host=\"$host\" primary=\"$primary\" socks=\"$socks\" dir=\"$dir\" args=\"$args\""
    case "$host"
    in
	\#*|'')	# comment or empty
		continue
		;;
	\$*)	# in-line variable assignment
		$SHOWME && echo "# $host $primary $socks $dir $args"
		cmd=`echo "$host $primary $socks $dir $args" \
		     | sed -n \
			 -e "/='/s/\(='[^']*'\).*/\1/" \
			 -e '/="/s/\(="[^"]*"\).*/\1/' \
			 -e '/=[^"'"'"']/s/[;&<>|].*$//' \
			 -e '/^\\$[A-Za-z][A-Za-z0-9_]*=/{
s/^\\$//
s/^\([A-Za-z][A-Za-z0-9_]*\)=/export \1; \1=/p
}'`
		if [ -z "$cmd" ]
		then
		    # in-line command, not a variable assignment
		    _warning "in-line command is not a variable assignment, line ignored"
		else
		    case "$cmd"
		    in
			'export PATH;'*)
			    _warning "cannot change \$PATH, line ignored"
			    ;;
			'export IFS;'*)
			    _warning "cannot change \$IFS, line ignored"
			    ;;
			*)
			    $SHOWME && echo "+ $cmd"
			    eval $cmd
			    ;;
		    esac
		fi
		continue
		;;
    esac

    if [ -z "$version" -o "$version" = "1.0" ]
    then
	if [ -z "$version" ]
	then
	    echo "$prog: Warning: processing default version 1.0 control format"
	    version=1.0
	fi
	args="$dir $args"
	dir="$socks"
	socks=n
    fi

    if [ -z "$primary" -o -z "$socks" -o -z "$dir" -o -z "$args" ]
    then
	_error "insufficient fields in control file record"
	continue
    fi

    if $VERY_VERBOSE
    then
	pflag=''
	[ $primary = y ] && pflag=' -P'
	echo "Check pmlogger$pflag -h $host ... in $dir ..."
    fi

    # make sure output directory exists
    #
    if [ ! -d $dir ]
    then
	mkdir -p $dir >$tmp/err 2>&1
	if [ ! -d $dir ]
	then
	    cat $tmp/err
	    _error "cannot create directory ($dir) for PCP archive files"
	else
	    _warning "creating directory ($dir) for PCP archive files"
	fi
	chown pcp:pcp $dir 2>/dev/null
    fi

    [ ! -d $dir ] && continue

    # check for directory duplicate entries
    #
    if [ "`grep $dir $tmp/dir`" = "$dir" ]
    then
	_error "Cannot start more than one pmlogger instance for archive directory \"$dir\""
	continue
    else
	echo "$dir" >>$tmp/dir
    fi

    cd $dir
    dir=`$PWDCMND`
    $SHOWME && echo "+ cd $dir"

    # ensure pcp user will be able write there
    #
    ( id pcp && chown -R pcp:pcp $dir ) >/dev/null 2>&1
    if [ ! -w $dir ]
    then
        echo "$prog: Warning: no write access in $dir, skip lock file processing"
    else
	# demand mutual exclusion
	#
	rm -f $tmp/stamp $tmp/out
	delay=200	# tenths of a second
	while [ $delay -gt 0 ]
	do
	    if pmlock -v lock >>$tmp/out 2>&1
	    then
		echo $dir/lock >$tmp/lock
		break
	    else
		[ -f $tmp/stamp ] || touch -t `pmdate -30M %Y%m%d%H%M` $tmp/stamp
		if [ -z "`find lock -newer $tmp/stamp -print 2>/dev/null`" ]
		then
		    if [ -f lock ]
		    then
			echo "$prog: Warning: removing lock file older than 30 minutes"
			LC_TIME=POSIX ls -l $dir/lock
			rm -f lock
		    else
			# there is a small timing window here where pmlock
			# might fail, but the lock file has been removed by
			# the time we get here, so just keep trying
			#
			:
		    fi
		fi
	    fi
	    pmsleep 0.1
	    delay=`expr $delay - 1`
	done

	if [ $delay -eq 0 ]
	then
	    # failed to gain mutex lock
	    #
	    if [ -f lock ]
            then
                echo "$prog: Warning: is another PCP cron job running concurrently?"
		LC_TIME=POSIX ls -l $dir/lock
	    else
		echo "$prog: `cat $tmp/out`"
	    fi
	    _warning "failed to acquire exclusive lock ($dir/lock) ..."
	    continue
	fi
    fi
    
    pid=''
    if [ "X$primary" = Xy ]
    then
        if [ "X$host" != "X$LOCALHOSTNAME" -a "X$host" != "X`pmhostname`" ]
	then
	    _error "\"primary\" only allowed for $LOCALHOSTNAME (localhost, not $host)"
	    _unlock
	    continue
	fi

	if is_chkconfig_on pmlogger
	then
	    :
	else
	    _error "primary logging disabled via chkconfig for $host"
	    _unlock
	    continue
	fi

	if test -f $PCP_TMP_DIR/pmlogger/primary
	then
	    if $VERY_VERBOSE
	    then 
		_host=`sed -n 2p <$PCP_TMP_DIR/pmlogger/primary`
		_arch=`sed -n 3p <$PCP_TMP_DIR/pmlogger/primary`
		$PCP_ECHO_PROG $PCP_ECHO_N "... try $PCP_TMP_DIR/pmlogger/primary: host=$_host arch=$_arch""$PCP_ECHO_C"
	    fi
	    primary_inode=`_get_ino $PCP_TMP_DIR/pmlogger/primary`
	    $VERY_VERBOSE && echo primary_inode=$primary_inode
	    pid=''
	    for file in $PCP_TMP_DIR/pmlogger/*
	    do
		case "$file"
		in
		    */primary|*\*)
			;;
		    */[0-9]*)
			inode=`_get_ino "$file"`
			$VERY_VERBOSE && echo $file inode=$inode
			if [ "$primary_inode" = "$inode" ]
			then
			    pid="`echo $file | sed -e 's/.*\/\([^/]*\)$/\1/'`"
			    break
			fi
			;;
		esac
	    done
	    if [ -z "$pid" ]
	    then
		if $VERY_VERBOSE
		then
		    echo "primary pmlogger process pid not found"
		    ls -l $PCP_TMP_DIR/pmlogger
		fi
	    else
		if _get_pids_by_name pmlogger | grep "^$pid\$" >/dev/null
		then
		    $VERY_VERBOSE && echo "primary pmlogger process $pid identified, OK"
		else
		    $VERY_VERBOSE && echo "primary pmlogger process $pid not running"
		    pid=''
		fi
	    fi
	fi
    else
	fqdn=`pmhostname $host`
	for log in $PCP_TMP_DIR/pmlogger/[0-9]*
        do
	    [ "$log" = "$PCP_TMP_DIR/pmlogger/[0-9]*" ] && continue
	    if $VERY_VERBOSE
	    then 
		_host=`sed -n 2p <$PCP_TMP_DIR/pmlogger/primary`
		_arch=`sed -n 3p <$PCP_TMP_DIR/pmlogger/primary`
		$PCP_ECHO_PROG $PCP_ECHO_N "... try $log fqdn=$fqdn host=$_host arch=$_arch: ""$PCP_ECHO_C"
	    fi
	    # throw away stderr in case $log has been removed by now
	    match=`sed -e '3s/\/[0-9][0-9][0-9][0-9][0-9.]*$//' $log 2>/dev/null \
                   | $PCP_AWK_PROG '
BEGIN							{ m = 0 }
NR == 2	&& $1 == "'$fqdn'"				{ m = 1; next }
NR == 2	&& "'$fqdn'" == "'$host'" &&
	( $1 ~ /^'$host'\./ || $1 ~ /^'$host'$/ )	{ m = 1; next }
NR == 3 && m == 1 && $0 == "'$dir'"			{ m = 2; next }
END							{ print m }'`
	    $VERY_VERBOSE && $PCP_ECHO_PROG $PCP_ECHO_N "match=$match ""$PCP_ECHO_C"
	    if [ "$match" = 2 ]
	    then
		pid=`echo $log | sed -e 's,.*/,,'`
		if _get_pids_by_name pmlogger | grep "^$pid\$" >/dev/null
		then
		    $VERY_VERBOSE && echo "pmlogger process $pid identified, OK"
		    break
		fi
		$VERY_VERBOSE && echo "pmlogger process $pid not running, skip"
		pid=''
	    elif [ "$match" = 0 ]
	    then
		$VERY_VERBOSE && echo "different host, skip"
	    elif [ "$match" = 1 ]
	    then
		$VERY_VERBOSE && echo "different directory, skip"
	    fi
	done
    fi

    if [ -z "$pid" ]
    then
	rm -f Latest

	if [ "X$primary" = Xy ]
	then
	    args="-P $args"
	    iam=" primary"
	    # clean up port-map, just in case
	    #
	    PM_LOG_PORT_DIR=$PCP_TMP_DIR/pmlogger
	    rm -f $PM_LOG_PORT_DIR/primary
	else
	    args="-h $host $args"
	    iam=""
	fi

	# each new log started is named yyyymmdd.hh.mm
	#
	LOGNAME=`date "+%Y%m%d.%H.%M"`

	# handle duplicates/aliases (happens when pmlogger is restarted
	# within a minute and LOGNAME is the same)
	#
	suff=''
	for file in $LOGNAME.*
	do
	    [ "$file" = "$LOGNAME"'.*' ] && continue
	    # we have a clash! ... find a new -number suffix for the
	    # existing files ... we are going to keep $LOGNAME for the
	    # new pmlogger below
	    #
	    if [ -z "$suff" ]
	    then
		for xx in 0 1 2 3 4 5 6 7 8 9
		do
		    for yy in 0 1 2 3 4 5 6 7 8 9
		    do
			[ "`echo $LOGNAME-${xx}${yy}.*`" != "$LOGNAME-${xx}${yy}.*" ] && continue
			suff=${xx}${yy}
			break
		    done
		    [ ! -z "$suff" ] && break
		done
		if [ -z "$suff" ]
		then
		    _error "unable to break duplicate clash for archive basename $LOGNAME"
		fi
		$VERBOSE && echo "Duplicate archive basename ... rename $LOGNAME.* files to $LOGNAME-$suff.*"
	    fi
	    eval $MV -f $file `echo $file | sed -e "s/$LOGNAME/&-$suff/"`
	done

	$VERBOSE && _message restart

	sock_me=''
        if [ "$socks" = y ]
        then
            # only check for pmsocks if it's specified in the control file
            have_pmsocks=false
            if which pmsocks >/dev/null 2>&1
            then
                # check if pmsocks has been set up correctly
                if pmsocks ls >/dev/null 2>&1
                then
                    have_pmsocks=true
                fi
            fi

            if $have_pmsocks
            then
                sock_me="pmsocks "
            else
                echo "$prog: Warning: no pmsocks available, would run without"
                sock_me=""
            fi
        fi

	_get_logfile
	if [ -f $logfile ]
	then
	    $VERBOSE && $SHOWME && echo
	    eval $MV -f $logfile $logfile.prior
	fi
	args="$args -m pmlogger_check"
	if $SHOWME
	then
	    echo
	    echo "+ ${sock_me}pmlogger $args $LOGNAME"
	    _unlock
	    continue
	else
	    ${sock_me}pmlogger $args $LOGNAME >$tmp/out 2>&1 &
	    pid=$!
	fi

	# wait for pmlogger to get started, and check on its health
	_check_logger $pid

	# the archive folio Latest is for the most recent archive in
	# this directory
	#
	if [ -f $LOGNAME.0 ] 
	then
	    $VERBOSE && echo "Latest folio created for $LOGNAME"
            mkaf $LOGNAME.0 >Latest
            ( id pcp && chown pcp:pcp Latest ) >/dev/null 2>&1
	else
	    logdir=`dirname $LOGNAME`
	    if $TERSE
	    then
		echo "$prog: Error: archive file `cd $logdir; $PWDCMND`/$LOGNAME.0 missing"
	    else
		echo "$prog: Error: archive file $LOGNAME.0 missing"
		echo "Directory (`cd $logdir; $PWDCMND`) contents:"
		LC_TIME=POSIX ls -la $logdir
	    fi
	fi
    fi

    _unlock

done

[ -f $tmp/err ] && status=1
exit
