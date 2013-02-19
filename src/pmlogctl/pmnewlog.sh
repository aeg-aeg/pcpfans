#! /bin/sh
#
# Copyright (c) 1995-2001,2003 Silicon Graphics, Inc.  All Rights Reserved.
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
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#
# stop and restart a pmlogger instance
#

# Get standard environment
. $PCP_DIR/etc/pcp.env


# error messages should go to stderr, not the GUI notifiers
#
unset PCP_STDERR

tmp=`mktemp -d /tmp/pcp.XXXXXXXXX` || exit 1
status=0
trap "rm -rf $tmp; exit \$status" 0 1 2 3 15
prog=`basename $0`

VERBOSE=false
SHOWME=false
CP=cp
MV=mv
RM=rm
KILL=pmsignal
primary=true
myname="primary pmlogger"
connect=primary
access=""
config=""
saveconfig=""
logfile="pmlogger.log"
namespace=""
args=""
sock_me=""
usage="Usage: $prog [options] archive

options: any combination of pmnewlog and most pmlogger options

pmnewlog options:
  -a accessfile   specify access controls for the new pmlogger
  -C saveconfig   save the configuration of new pmlogger in saveconfig
  -c configfile   file to load configuration from
  -N              perform a dry run (like \`make -n')
  -n pmnsfile     use an alternative PMNS
  -P              execute as primary logger instance
  -p pid          restart non-primary logger with pid
  -s              use pmsocks
  -V              turn on verbose reporting of pmnewlog progress"


_abandon()
{
    echo
    echo "Sorry, but this is fatal.  No new pmlogger instance has been started."
    status=1
    exit
}

_check_pid()
{
    if $SHOWME
    then
	:
    else
	_get_pids_by_name pmlogger | grep "^$1\$"
    fi
}

_check_logfile()
{
    if [ ! -f $logfile ]
    then
	echo "Cannot find pmlogger output file at \"$logfile\""
    else
	echo "Contents of pmlogger output file \"$logfile\" ..."
	cat $logfile
    fi
}

_check_logger()
{
    # wait until pmlogger process starts, or exits
    #
    delay=5
    [ ! -z "$PMCD_CONNECT_TIMEOUT" ] && delay=$PMCD_CONNECT_TIMEOUT
    x=5
    [ ! -z "$PMCD_REQUEST_TIMEOUT" ] && x=$PMCD_REQUEST_TIMEOUT

    # wait for maximum time of a connection and 20 requests
    #
    delay=`expr $delay + 20 \* $x`
    i=0
    while [ $i -lt $delay ]
    do
	$VERBOSE && $PCP_ECHO_PROG $PCP_ECHO_N ".""$PCP_ECHO_C"
	if $SHOWME
	then
	    echo "+ echo 'connect $1' | pmlc ..."
	    $VERBOSE && echo " done"
	    return 0
	elif echo "connect $1" | pmlc 2>&1 | grep "Unable to connect" >/dev/null
	then
            :
        else
            sleep 5
            $VERBOSE && echo " done"
            return 0
        fi

	if _get_pids_by_name pmlogger | grep "^$1\$" >/dev/null
	then
            :
        else
	    $VERBOSE || _message restart
	    echo " process exited!"
	    _check_logfile
	    return 1
	fi
	sleep 5
	i=`expr $i + 5`
    done
    $VERBOSE || _message restart
    echo " timed out waiting!"
    sed -e 's/^/	/' $tmp/out
    _check_logfile
    return 1
}

_message()
{
    case $1
    in
	looking)
	    $PCP_ECHO_PROG $PCP_ECHO_N "Looking for $myname ...""$PCP_ECHO_C"
	    ;;
	get_host)
	    $PCP_ECHO_PROG $PCP_ECHO_N "Getting logged host name from $myname ...""$PCP_ECHO_C"
	    ;;
	get_state)
	    $PCP_ECHO_PROG $PCP_ECHO_N "Contacting $myname to get logging state ...""$PCP_ECHO_C"
	    ;;
	restart)
	    $PCP_ECHO_PROG $PCP_ECHO_N "Waiting for new pmlogger to start ..""$PCP_ECHO_C"
	    ;;
    esac
}

_do_cmd()
{
    if $SHOWME
    then
	echo "+ $1"
    else
	eval $1
    fi
}

# option parsing
#
# pmlogger, without -V version which is redefined as -V (verbose)
# and -s exit_size which is redefined as -s (pmsocks), and ignore
# [ -h host ] and [ -x fd ] as they make no sense in the argument
# part of the pmlogger control file for a long-running pmlogger.
#

while getopts "a:C:c:D:Ll:Nm:n:Pp:rst:T:Vv:" c
do
    case $c
    in

# pmnewlog options and flags
#

	a)	access="$OPTARG"
		if [ ! -f $access ]
		then
		    echo "$prog: Error: cannot find accessfile ($access)"
		    _abandon
		fi
		;;

	C)	saveconfig="$OPTARG"
		;;

	N)	SHOWME=true
		CP="echo + cp"
		MV="echo + mv"
		RM="echo + rm"
		KILL="echo + kill"
		;;

	p)	pid=$OPTARG
		primary=false
		myname="pmlogger (process $pid)"
		connect=$pid
		;;

	s)	if which pmsocks >/dev/null 2>&1
                then
                    sock_me="pmsocks "
                else
                    echo "$prog: Warning: no pmsocks available, would run without"
                    sock_me=""
                fi
		;;

	V)	VERBOSE=true
		;;

# pmlogger options and flags that need special handling
#

	c)	config="$OPTARG"
		if [ ! -f $config ]
		then
		    if [ -f $PCP_SYSCONF_DIR/pmlogger/$config ]
		    then
			config="$PCP_SYSCONF_DIR/pmlogger/$config"
		    else
			echo "$prog: Error: cannot find configfile ($config)"
			_abandon
		    fi
		fi
		;;

	l)	logfile="$OPTARG"
		;;

	n)	namespace="-n $OPTARG"
		args="$args-$c $OPTARG "
		;;

	P)	primary=true
		myname="primary pmlogger"
		;;

# pmlogger flags passed through
#

	L|r)	
		args="$args-$c "
		;;

	D|m|t|T|v)
		args="$args-$c $OPTARG "
		;;

# oops
#
	
	\?)	echo "$usage"
		_abandon
		;;
    esac
done
shift `expr $OPTIND - 1`

if [ $# -ne 1 ]
then
    echo "$usage"
    echo
    echo "Not enough arguments"
    _abandon
fi

# initial sanity checking for new archive name
#
archive=$1

# check that designated pmlogger is really running
#
$VERBOSE && _message looking
$PCP_PS_PROG $PCP_PS_ALL_FLAGS \
| if $primary
then
    grep 'pmlogger .*-P' | grep -v grep
else
    $PCP_AWK_PROG '$2 == '"$pid"' && /pmlogger/ { print }'
fi >$tmp/out

if [ -s $tmp/out ]
then
    $VERBOSE && echo " found"
    $VERBOSE && cat $tmp/out
    pid=`$PCP_AWK_PROG '{ print $2 }' <$tmp/out`
else
    if $VERBOSE
    then
	:
    else
	_message looking
	echo
    fi
    echo "$prog: Error: process not found"
    _abandon
fi

# pass primary/not primary down
#
$primary && args="$args-P "

# pass logfile option down
#
args="$args-l $logfile "

# if not a primary pmlogger, get name of pmcd host pmlogger is connected to
#
if $primary
then
    host=localhost
else
    # start critical section ... no interrupts due to pmlogger SIGPIPE
    # bug in PCP 1.1
    #
    trap "echo; echo $prog:' Interrupt! ... I am talking to pmlogger, please wait ...'" 1 2 3 15
    $VERBOSE && _message get_host

    _do_cmd "( echo 'connect $connect' ; echo status ) | pmlc 2>$tmp/err >$tmp/out"

    # end critical section
    #
    trap "rm -rf $tmp; exit \$status" 0 1 2 3 15

    if $SHOWME || [ ! -s $tmp/err ]
    then
	$VERBOSE && echo " done"
    else
	if grep "Unable to connect" $tmp/err >/dev/null
	then
	    $VERBOSE || _message get_host
	    echo " failed to connect"
	    echo
	    sed -e 's/^/	/' $tmp/err
	    _abandon
	else
	    $VERBOSE || _message get_host
	    echo
	    echo "$prog: Warning: errors from talking to $myname via pmlc"
	    sed -e 's/^/	/' $tmp/err
	    echo
	    echo "continuing ..."
	fi
    fi

    if $SHOWME
    then
	host=somehost
    else
	host=`sed -n -e '/^pmlogger/s/.* from host //p' <$tmp/out`
	if [ "X$host" = X ]
	then
	    echo "$prog: Error: failed to get host name from $myname"
	    echo "This is what was collected from $myname."
	    echo
	    sed -e 's/^/	/' $tmp/out
	    _abandon
	fi
	args="$args-h $host "
    fi
fi

# extract/construct config file if required
#
if [ "X$config" = X ]
then
    # start critical section ... no interrupts due to pmlogger SIGPIPE
    # bug in PCP 1.1
    #
    trap "echo; echo $prog:' Interrupt! ... I am talking to pmlogger, please wait ...'" 1 2 3 15
    $VERBOSE && _message get_state

    # iterate over top-level names in pmns, and query pmlc for
    # current configuration ... note exclusion of "proc" metrics
    # ... others may be excluded in a similar fashion
    #
    if $SHOWME
    then
	echo "+ ( echo 'connect $connect'; echo 'query ...'; ... ) | pmlc $namespace | $PCP_AWK_PROG ..."
    else
	( echo "connect $connect" ; for top in `pminfo $namespace \
					| sed -e 's/\..*//' -e '/^proc$/d' \
					| sort -u`
	    do
		echo "query $top"
	    done \
	) \
	| pmlc $namespace 2>$tmp/err \
	| $PCP_AWK_PROG >$tmp/out '
/^[^ ]/						{ metric = $1; next }
$1 == "mand" || ( $1 == "adv" && $2 == "on" ) 	{ print $0 " " metric }'
    fi

    # end critical section
    #
    trap "rm -rf $tmp; exit \$status" 0 1 2 3 15

    if $SHOWME || [ ! -s $tmp/err ]
    then
	$VERBOSE && echo " done"
    else
	if grep "Unable to connect" $tmp/err >/dev/null
	then
	    $VERBOSE || _message get_state
	    echo " failed to connect"
	    echo
	    sed -e 's/^/	/' $tmp/err
	    _abandon
	else
	    $VERBOSE || _message get_state
	    echo
	    echo "$prog: Warning: errors from talking to $myname via pmlc"
	    sed -e 's/^/	/' $tmp/err
	    echo
	    echo "continuing ..."
	fi
    fi

    if [ ! -s $tmp/out ]
    then
	if $SHOWME
	then
	    :
	else
	    echo "$prog: Error: failed to collect configuration info from $myname"
	    echo "Most likely this pmlogger instance is inactive."
	    _abandon
	fi
    fi

    # convert to a pmlogger config file
    #
    if $SHOWME
    then
	echo "+ create new pmlogger config file ..."
    else
	sed <$tmp/out >$tmp/config \
	    -e 's/ on  nl/ on/' \
	    -e 's/ off nl/ off/'\
	    -e 's/  *mand  *\(o[nf]*\) /log mandatory \1 /' \
	    -e 's/  *adv  *on /log advisory on/' \
	    -e 's/\[[0-9][0-9]* or /[/' \
	    -e 's/\(\[[^]]*]\) \([^ ]*\)/\2 \1/' \
	    -e 's/   */ /g'

	if [ ! -s $tmp/config ]
	then
	    echo "$prog: Error: failed to generate a pmlogger configuration file for pmlogger"
	    echo "This is what was collected from $myname."
	    echo
	    sed -e 's/^/	/' $tmp/out
	    _abandon
	fi
    fi
    config=$tmp/config
fi

# optionally append access control specifications
#
if [ "X$access" != X ]
then
    if grep '\[access]' $config >/dev/null
    then
	echo "$prog: Error: pmlogger configuration file already contains an"
	echo "	access control section, specifications from \"$access\" cannot"
	echo "	be applied."
	_abandon
    fi
    cat $access >>$config
fi

# add config file to the args, save config file if -C
#
args="$args-c $config "
if [ "X$saveconfig" != X ]
then
    if eval $CP $config $saveconfig
    then
	echo "New pmlogger configuration file saved as $saveconfig"
    else
	echo "$prog: Warning: unable to save configuration file as $saveconfig"
    fi
fi

# kill off existing pmlogger
#
$VERBOSE && $PCP_ECHO_PROG $PCP_ECHO_N "Terminating $myname ...""$PCP_ECHO_C"
for sig in USR1 TERM KILL
do
    $VERBOSE && $PCP_ECHO_PROG $PCP_ECHO_N " SIG$sig ...""$PCP_ECHO_C"
    eval $KILL -s $sig $pid
    sleep 5
    [ "`_check_pid $pid`" = "" ] && break
done

if [ "`_check_pid $pid`" = "" ]
then
    $VERBOSE && echo " done"
else
    echo " failed!"
    _abandon
fi

# the archive folio Latest is for the most recent archive in this directory
#
dir=`dirname $archive`
eval $RM -f $dir/Latest

# clean up port-map, just in case
#
PM_LOG_PORT_DIR=$PCP_TMP_DIR/pmlogger
eval $RM -f $PM_LOG_PORT_DIR/$pid
$primary && eval $RM -f $PM_LOG_PORT_DIR/primary

# finally do it, ...
#
cd $dir
$SHOWME && echo "+ cd $dir"
[ "X$dir" = X. ] && dir=`pwd`
archive=`basename $archive`

# handle duplicates/aliases (happens when pmlogger is restarted
# within a minute and basename is the same)
#
suff=''
for file in $archive.*
do
    [ "$file" = "$archive"'.*' ] && continue
    # we have a clash! ... find a new -number suffix for the
    # existing files ... we are going to keep $archive for the
    # new pmlogger below
    #
    if [ -z "$suff" ]
    then
	for xx in 0 1 2 3 4 5 6 7 8 9
	do
	    for yy in 0 1 2 3 4 5 6 7 8 9
	    do
		[ "`echo $archive-${xx}${yy}.*`" != "$archive-${xx}${yy}.*" ] && continue
		suff=${xx}$yy
		break
	    done
	    [ ! -z "$suff" ] && break
	done
	if [ -z "$suff" ]
	then
	    echo "$prog: Error: unable to break duplicate clash for archive basename \"$archive\""
	    _abandon
	fi
	$VERBOSE && echo "Duplicate archive basename ... rename $archive.* files to $archive-$suff.*"
    fi
    eval $MV -f $file `echo $file | sed -e "s/$archive/&-$suff/"`
done

$VERBOSE && echo "Launching new pmlogger in directory \"$dir\" as ..."
[ -f $logfile ] && eval $MV -f $logfile $logfile.prior
$VERBOSE && echo "${sock_me}pmlogger $args$archive"

if $SHOWME
then
    echo "+ ${sock_me}pmlogger $args$archive &"
    echo "+ ... assume pid is 12345"
    new_pid=12345
else
    ${sock_me}pmlogger $args$archive &
    new_pid=$!
fi

# stall a bit ...
#
STALL_TIME=10
sleep $STALL_TIME

$VERBOSE && _message restart
if _check_logger $new_pid || $SHOWME
then
    $VERBOSE && echo "New pmlogger status ..."
    $VERBOSE && _do_cmd "( echo 'connect $new_pid'; echo status ) | pmlc"

    # make the "Latest" archive folio
    #
    i=0
    failed=true
    WAIT_TIME=10
    while [ $i -lt $WAIT_TIME ]
    do
	if $SHOWME || [ -f $archive.0 -a -f $archive.meta -a $archive.index ]
	then
	    _do_cmd "mkaf $archive.0 >Latest" 2>$tmp/err
	    if [ -s $tmp/err ]
	    then
		# errors from mkaf typically result from race conditions
		# at the start of pmlogger, e.g.
		# Warning: cannot extract hostname from archive "..." ...
		#
		# simply keep trying
		:
	    else
		failed=false
		break
	    fi
	fi
	sleep 1
	i=`expr $i + 1`
    done

    if $failed
    then
	ELAPSED=`expr $STALL_TIME + $WAIT_TIME`
	echo "Warning: pmlogger [pid=$new_pid host=$host] failed to create archive files within $ELAPSED seconds"
	if [ -f $tmp/err ]
	then
	    echo "Warnings/errors from mkaf ..."
	    cat $tmp/err
	fi
    fi
else
    _abandon
fi

exit
