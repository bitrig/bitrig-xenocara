XCOMM!/bin/sh
XCOMM
XCOMM	Do the equivalent of the 'makedepend' program, but do it right.
XCOMM
XCOMM	Usage:
XCOMM
XCOMM	makedepend [cpp-flags] [-w width] [-s magic-string] [-f makefile]
XCOMM	  [-o object-suffix] [-v] [-a] [-cc compiler] [-d dependencyflag]
XCOMM
XCOMM	Notes:
XCOMM
XCOMM	The C compiler used can be overridden with the environment
XCOMM	variable "CC" or the command line flag -cc.
XCOMM
XCOMM	The "-v" switch of the "makedepend" program is not supported.
XCOMM
XCOMM
XCOMM	This script should
XCOMM	work on both USG and BSD systems.  However, when System V.4 comes out,
XCOMM	USG users will probably have to change "silent" to "-s" instead of
XCOMM	"-" (at least, that is what the documentation implies).
XCOMM

CC=PREPROC

silent='-'

TMP=`pwd`/.mdep$$

rm -rf ${TMP}
if ! mkdir -p ${TMP}; then
  echo "$0: cannot create ${TMP}, exit." >&2
fi

CPPCMD=${TMP}/a
DEPENDLINES=${TMP}/b
TMPMAKEFILE=${TMP}/c
MAGICLINE=${TMP}/d
ARGS=${TMP}/e

trap "rm -rf ${TMP}; exit 1" 1 2 15
trap "rm -rf ${TMP}; exit 0" 1 2 13

echo " \c" > $CPPCMD
if [ `wc -c < $CPPCMD` -eq 1 ]
then
    c="\c"
    n=
else
    c=
    n="-n"
fi

echo $n "$c" >$ARGS

files=
makefile=
magic_string='# DO NOT DELETE'
objsuffix='.o'
width=78
endmarker=""
verbose=n
append=n
compilerlistsdepends=n

while [ $# != 0 ]
do
    if [ "$endmarker"x != x ] && [ "$endmarker" = "$1" ]; then
	endmarker=""
    else
	case "$1" in
	    -D*|-I*|-U*)
		echo $n " '$1'$c" >> $ARGS
		;;

	    -g|-O)	# ignore so we can just pass $(CFLAGS) in
		;;

	    *)
		if [ "$endmarker"x = x ]; then
		    case "$1" in
			-w)
			    width="$2"
			    shift
			    ;;
			-s)
			    magic_string="$2"
			    shift
			    ;;
			-f*)
			    if [ "$1" = "-f-" ]; then
				makefile="-"
			    elif [ "$1" = "-f" ]; then
				makefile="$2"
				shift
			    else
				echo "$1" | sed 's/^\-f//' >${TMP}arg
				makefile="`cat ${TMP}arg`"
				rm -f ${TMP}arg
			    fi
			    ;;
			-o)
			    objsuffix="$2"
			    shift
			    ;;

			--*)
			    echo "$1" | sed 's/^\-\-//' >${TMP}end
			    endmarker="`cat ${TMP}end`"
			    rm -f ${TMP}end
			    if [ "$endmarker"x = x ]; then
				endmarker="--"
			    fi
			    ;;
			-v)
			    verbose="y"
			    ;;

			-a)
			    append="y"
			    ;;

			-cc)
			    CC="$2"
			    shift
			    ;;

			# Flag to tell compiler to output dependencies directly
			# For example, with Sun compilers, -xM or -xM1 or
			# with gcc, -M
		        -d)
			    compilerlistsdepends="y"
			    compilerlistdependsflag="$2"
			    shift
			    ;;

			-*)
			    echo "Unknown option '$1' ignored" 1>&2
			    ;;
			*)
			    files="$files $1"
			    ;;
		    esac
		fi
		;;
	esac
    fi
    shift
done
echo ' $*' >> $ARGS

if [ "$compilerlistsdepends"x = "y"x ] ; then
  CC="$CC $compilerlistdependsflag"
fi

echo "#!/bin/sh" > $CPPCMD
echo "exec $CC `cat $ARGS`" >> $CPPCMD
chmod +x $CPPCMD
rm $ARGS

case "$makefile" in
    '')
	if [ -r makefile ]
	then
	    makefile=makefile
	elif [ -r Makefile ]
	then
	    makefile=Makefile
	else
	    echo 'no makefile or Makefile found' 1>&2
	    exit 1
	fi
	;;
    -)
	makefile=$TMPMAKEFILE
	;;
esac

if [ "$verbose"x = "y"x ]; then
    cat $CPPCMD
fi

echo '' > $DEPENDLINES

if [ "$compilerlistsdepends"x = "y"x ] ; then
    for i in $files
    do
	$CPPCMD $i >> $DEPENDLINES
    done
else
for i in $files
do
    $CPPCMD $i \
      | sed -n "/^#/s;^;$i ;p"
done \
  | sed -e 's|/[^/.][^/]*/\.\.||g' -e 's|/\.[^.][^/]*/\.\.||g' \
    -e 's|"||g' -e 's| \./| |' \
  | awk '{
	if ($1 != $4  &&  $2 != "#ident" && $2 != "#pragma")
	    {
	    numparts = split( $1, ofileparts, "\." )
	    ofile = ""
	    for ( i = 1; i < numparts; i = i+1 )
		{
		if (i != 1 )
		    ofile = ofile "."
		ofile = ofile ofileparts[i]
		}
	    print ofile "'"$objsuffix"'", $4
	    }
	}' \
  | sort -u \
  | awk '
	    {
	    newrec = rec " " $2
	    if ($1 != old1)
		{
		old1 = $1
		if (rec != "")
		    print rec
		rec = $1 ": " $2
		}
	    else if (length (newrec) > '"$width"')
		{
		print rec
		rec = $1 ": " $2
		}
	    else
		rec = newrec
	    }
	END \
	    {
	    if (rec != "")
		print rec
	    }' \
  | egrep -v '^[^:]*:[ 	]*$' >> $DEPENDLINES
fi

trap "" 1 2 13 15	# Now we are committed
case "$makefile" in
    $TMPMAKEFILE)
	;;
    *)
	rm -f $makefile.bak
	cp $makefile $makefile.bak
	echo "Appending dependencies to $makefile"
	;;
esac

XCOMM
XCOMM If not -a, append the magic string and a blank line so that
XCOMM /^$magic_string/+1,\$d can be used to delete everything from after
XCOMM the magic string to the end of the file.  Then, append a blank
XCOMM line again and then the dependencies.
XCOMM
if [ "$append" = "n" ]
then
    cat >> $makefile << END_OF_APPEND

$magic_string

END_OF_APPEND
    ed $silent $makefile << END_OF_ED_SCRIPT
/^$magic_string/+1,\$d
w
q
END_OF_ED_SCRIPT
    echo '' >>$makefile
fi

cat $DEPENDLINES >>$makefile

case "$makefile" in
    $TMPMAKEFILE)
	cat $TMPMAKEFILE
	;;

esac

rm -rf ${TMP}*
exit 0
