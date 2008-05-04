#!/bin/sh

if [ -n "$UPART_TEST_CLEAN" ]
then
    cleanfiles=
    for ii in *.img
    do
        cleanfiles="$cleanfiles test-${ii%.img}.out test-${ii%.img}.err"
    done
    echo "rm -f$cleanfiles"
    rm -f $cleanfiles
    exit
fi

upart="../upart -vv"
diff="diff -u"

failcount=0
totalcount=0
faillist=

echo "running tests..."

for ii in *.img
do
    test="${ii%.img}"
    testoutfile="test-$test.out"
    testerrfile="test-$test.err"

    if [ -e "$test.exit" ]
    then
        wantstatus="`cat "$test.exit"`"
    else
        wantstatus=0
    fi

    if [ ! -e "$test.out" ]
    then
        outfile=/dev/null
    else
        outfile="$test.out"
    fi

    if [ ! -e "$test.err" ]
    then
        errfile=/dev/null
    else
        errfile="$test.err"
    fi

    rm -f "$testoutfile" "$testerrfile"
    failed=n
    $upart "$ii" > "$testoutfile" 2> "$testerrfile"
    gotstatus=$?

    if [ x"$wantstatus" != x"$gotstatus" ]
    then
        failed=y
        echo "$test: expected exit status $wantstatus $gotstatus"
    fi

    if ! cmp "$outfile" "$testoutfile" >/dev/null 2>&1
    then
        failed=y
        echo "$test: unexpected standard output"
        $diff "$outfile" "$testoutfile"
    else
        rm -f "$testoutfile"
    fi

    if ! cmp "$errfile" "$testerrfile" >/dev/null 2>&1
    then
        failed=y
        echo "$test: unexpected standard error"
        $diff "$errfile" "$testerrfile"
    else
        rm -f "$testerrfile"
    fi

    totalcount=$(($totalcount + 1))
    if [ xy = x"$failed" ]
    then
        failcount=$(($failcount + 1))
        faillist="$faillist $test"
    fi
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
