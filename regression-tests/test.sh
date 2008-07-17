#!/bin/sh

if [ -n "$UPART_TEST_CLEAN" ]
then
    cleanfiles=
    for ii in *.img
    do
        test="`basename "$ii" .img`"
        cleanfiles="$cleanfiles test-${test}.out test-${test}.err"
    done
    echo "rm -f$cleanfiles"
    rm -f $cleanfiles
    exit
fi

upart="../upart -fvv"
diff="diff -u"

failcount=0
totalcount=0
faillist=

if [ -n "$UPART_TEST_REGEN" ]
then
    for ii in *.img
    do
        test="`basename "$ii" .img`"
        testoutfile="$test.out"
        testerrfile="$test.err"
        testexitfile="$test.exit"
        rm -f "$testoutfile" "$testerrfile" "$testexitfile"
        $upart "$ii" > "$testoutfile" 2> "$testerrfile"
        exit=$?
        test -s "$testoutfile" || rm -f "$testoutfile"
        test -s "$testerrfile" || rm -f "$testerrfile"
        test 0 = $exit || echo $exit > "$testexitfile"
    done
    exit
fi

echo "running tests..."

for ii in *.img
do
    test="`basename "$ii" .img`"
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
    $upart "$ii" > "$testoutfile" 2> "$testerrfile"
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
