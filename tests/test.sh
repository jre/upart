#!/bin/sh

upart="../upart -f"
diff="diff -u"

failcount=0
totalcount=0
faillist=

case "$*" in
    clean)
    for img in *.img
    do
        cleanfiles=
        for flag in '' -v -vv
        do
            test="`basename "$img" .img`$flag"
            cleanfiles="$cleanfiles test-${test}.out test-${test}.err"
        done
        echo "rm -f$cleanfiles"
        rm -f $cleanfiles
    done
    exit
    ;;

    regen)
    for img in *.img
    do
        for flag in '' -v -vv
        do
            test="`basename "$img" .img`$flag"
            testoutfile="$test.out"
            testerrfile="$test.err"
            testexitfile="$test.exit"
            rm -f "$testoutfile" "$testerrfile" "$testexitfile"
            $upart $flag "$img" > "$testoutfile" 2> "$testerrfile"
            exit=$?
            test -s "$testoutfile" || rm -f "$testoutfile"
            test -s "$testerrfile" || rm -f "$testerrfile"
            test 0 = $exit || echo $exit > "$testexitfile"
        done
    done
    exit
    ;;

    '')
    ;;

    *)
    echo "usage: `basename "$0"` [clean|regen]"
    exit 1
    ;;
esac

echo "running tests..."

for img in *.img
do
    for flag in '' -v -vv
    do
        test="`basename "$img" .img`$flag"
        testoutfile="test-$test.out"
        testerrfile="test-$test.err"

        if [ -r "$test.exit" ]
        then
            wantstatus="`cat "$test.exit"`"
        else
            wantstatus=0
        fi

        if [ ! -r "$test.out" ]
        then
            outfile=/dev/null
        else
            outfile="$test.out"
        fi

        if [ ! -r "$test.err" ]
        then
            errfile=/dev/null
        else
            errfile="$test.err"
        fi

        rm -f "$testoutfile" "$testerrfile"
        failed=n
        $upart $flag "$img" > "$testoutfile" 2> "$testerrfile"
        gotstatus=$?

        if [ x"$wantstatus" != x"$gotstatus" ]
        then
            failed=y
            echo "$test: expected exit status $wantstatus $gotstatus"
        fi

        if cmp "$outfile" "$testoutfile" >/dev/null 2>&1
        then
            rm -f "$testoutfile"
        else
            failed=y
            echo "$test: unexpected standard output"
            $diff "$outfile" "$testoutfile"
        fi

        if cmp "$errfile" "$testerrfile" >/dev/null 2>&1
        then
            rm -f "$testerrfile"
        else
            failed=y
            echo "$test: unexpected standard error"
            $diff "$errfile" "$testerrfile"
        fi

        totalcount="`expr "$totalcount" \+ 1`"
        if [ xy = x"$failed" ]
        then
            failcount="`expr "$failcount" \+ 1`"
            faillist="$faillist $test"
        fi
    done
done

if [ 1 = $failcount ]
then
    nitpick=test
else
    nitpick=tests
fi

echo
echo "$failcount $nitpick failed out of $totalcount total."
if [ 0 != $failcount ]
then
    echo "The following $nitpick failed:"
    echo " $faillist"
    exit 1
fi
